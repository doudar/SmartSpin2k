/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "NativeHttpServer.h"
#include "HttpRequestParsing.h"
#include "Main.h"
#include "SS2KLog.h"
#include <LittleFS.h>
#include <lwip/sockets.h>
#include <memory>
#include <new>
#include <cstring>
#include <algorithm>
#include <mbedtls/sha256.h>

namespace {
constexpr size_t QUEUE_DEPTH = 3;
constexpr size_t BUFFER_SIZE = 2048;
esp_err_t unavailable(httpd_req_t* req, const char* message) {
  httpd_resp_set_status(req, "503 Service Unavailable");
  return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}
const char* statusText(int code) {
  switch (code) {
    case 200: return "200 OK"; case 400: return "400 Bad Request";
    case 404: return "404 Not Found"; case 413: return "413 Payload Too Large";
    case 503: return "503 Service Unavailable"; default: return "500 Internal Server Error";
  }
}
}
struct NativeHttpServer::Request {
  httpd_req_t* req = nullptr;
  TickType_t acceptedAt = xTaskGetTickCount();
  Route* route = nullptr;
  String path, body, type = "text/plain", file;
  std::vector<std::pair<std::string, std::string>> args;
  int status = 200;
  Upload upload;
  bool uploadEvent = false, rejected = false, reboot = false, completedUpload = false, ownsUpload = false;
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  ~Request() { if (done) vSemaphoreDelete(done); }
};

void NativeHttpServer::on(const char* path, httpd_method_t method, Handler handler, Handler upload) {
  routes.push_back({path, method, std::move(handler), std::move(upload)});
}
bool NativeHttpServer::begin() {
  if (handle_) return true;
  stopping = false;
  incoming = xQueueCreate(QUEUE_DEPTH, sizeof(Request*));
  pending = xQueueCreate(1, sizeof(Request*));
  workerStopped = xSemaphoreCreateBinary();
  admission = xSemaphoreCreateMutex();
  if (!incoming || !pending || !workerStopped || !admission) { stop(); return false; }
  if (xTaskCreate(workerEntry, "http_transfer", 6144, this, 1, &worker) != pdPASS) { worker = nullptr; stop(); return false; }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 4096;
  config.max_open_sockets = 4;
  config.max_uri_handlers = 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  config.uri_match_fn = httpd_uri_match_wildcard;
  if (httpd_start(&handle_, &config) != ESP_OK) { stop(); return false; }
  for (auto method : {HTTP_GET, HTTP_POST, HTTP_OPTIONS}) {
    httpd_uri_t handler{};
    handler.uri = "/*"; handler.method = method; handler.handler = accept; handler.user_ctx = this;
    if (httpd_register_uri_handler(handle_, &handler) != ESP_OK) { stop(); return false; }
  }
  return true;
}
void NativeHttpServer::stop() {
  if (admission) xSemaphoreTake(admission, portMAX_DELAY);
  stopping = true;
  if (admission) xSemaphoreGive(admission);
  // Worker cancels pending callbacks itself while maintenance is inside stop().
  if (worker) { xSemaphoreTake(workerStopped, portMAX_DELAY); worker = nullptr; }
  if (handle_) { httpd_stop(handle_); handle_ = nullptr; }
  if (incoming) { vQueueDelete(incoming); incoming = nullptr; }
  if (pending) { vQueueDelete(pending); pending = nullptr; }
  if (workerStopped) { vSemaphoreDelete(workerStopped); workerStopped = nullptr; }
  if (admission) { vSemaphoreDelete(admission); admission = nullptr; }
}
esp_err_t NativeHttpServer::accept(httpd_req_t* req) {
  auto& server = *static_cast<NativeHttpServer*>(req->user_ctx);
  struct AdmissionGuard {
    SemaphoreHandle_t mutex;
    explicit AdmissionGuard(SemaphoreHandle_t value) : mutex(value) { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~AdmissionGuard() { xSemaphoreGive(mutex); }
  } guard(server.admission);
  if (server.stopping) return unavailable(req, "Server stopping");
  if (req->method == HTTP_OPTIONS) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    return httpd_resp_send(req, nullptr, 0);
  }
  std::unique_ptr<Request> request(new (std::nothrow) Request);
  if (!request || !request->done) return unavailable(req, "Out of memory");
  if (httpd_req_async_handler_begin(req, &request->req) != ESP_OK) return ESP_FAIL;
  Request* queued = request.get();
  if (xQueueSend(server.incoming, &queued, 0) != pdTRUE) {
    unavailable(request->req, "Server busy");
    const int socket = httpd_req_to_sockfd(request->req);
    httpd_req_async_handler_complete(request->req);
    httpd_sess_trigger_close(server.handle_, socket);
    return ESP_OK;
  }
  request.release();
  return ESP_OK;
}
void NativeHttpServer::workerEntry(void* context) { static_cast<NativeHttpServer*>(context)->workerLoop(); }
void NativeHttpServer::workerLoop() {
  for (;;) {
    Request* raw = nullptr;
    if (xQueueReceive(incoming, &raw, pdMS_TO_TICKS(50)) != pdTRUE) {
      if (stopping) break;
      continue;
    }
    std::unique_ptr<Request> request(raw);
    const TickType_t startedAt = xTaskGetTickCount();
    if (stopping) { request->status = 503; request->body = "Server stopping"; }
    else handle(*request);
    respond(*request);
    const int socket = httpd_req_to_sockfd(request->req);
    httpd_req_async_handler_complete(request->req);
    httpd_sess_trigger_close(handle_, socket);
    if (request->reboot) rebootPending = true;
    const auto elapsed = (xTaskGetTickCount() - request->acceptedAt) * portTICK_PERIOD_MS;
    if (elapsed >= 500) {
      SS2K_LOG("NativeHTTP", "%s status=%d queued=%lu ms total=%lu ms", request->path.c_str(), request->status,
               static_cast<unsigned long>((startedAt - request->acceptedAt) * portTICK_PERIOD_MS), static_cast<unsigned long>(elapsed));
    }
  }
  xSemaphoreGive(workerStopped);
  vTaskDelete(nullptr);
}
bool NativeHttpServer::dispatch(Request& request, bool uploadEvent) {
  request.uploadEvent = uploadEvent;
  Request* ptr = &request;
  if (stopping || xQueueSend(pending, &ptr, 0) != pdTRUE) return false;
  const TickType_t began = xTaskGetTickCount();
  while (xSemaphoreTake(request.done, pdMS_TO_TICKS(20)) != pdTRUE) {
    if (stopping || xTaskGetTickCount() - began > pdMS_TO_TICKS(2000)) {
      Request* canceled = nullptr;
      if (xQueueReceive(pending, &canceled, 0) == pdTRUE) return false;
      // Maintenance has taken ownership; wait for its completion before freeing.
      // stop() is called on maintenance, so a queued cancellation is safe there.
    }
  }
  return true;
}
void NativeHttpServer::processPending() {
  if (rebootPending.exchange(false)) ss2k->rebootFlag = true;
  Request* request = nullptr;
  if (!pending || xQueueReceive(pending, &request, 0) != pdTRUE) return;
  active = request;
  if (request->uploadEvent) request->route->upload();
  else if (ss2k->isUpdating && !request->completedUpload) send(503, "text/plain", "Update in progress");
  else if (request->route) request->route->handler();
  else notFound();
  active = nullptr;
  xSemaphoreGive(request->done);
}
void NativeHttpServer::handle(Request& request) {
  std::string uri(request.req->uri);
  auto queryPos = uri.find('?');
  request.path = uri.substr(0, queryPos).c_str();
  if (queryPos != std::string::npos && !HttpRequestParsing::query(uri.substr(queryPos + 1), request.args)) {
    request.status = 400; request.body = "Invalid query"; return;
  }
  for (auto& route : routes) {
    if (route.path == request.path && route.method == request.req->method) { request.route = &route; break; }
  }
  if (request.req->method == HTTP_POST) {
    if (httpd_req_get_hdr_value_len(request.req, "Origin") > 0) {
      char origin[192]{}, host[128]{};
      if (httpd_req_get_hdr_value_str(request.req, "Origin", origin, sizeof(origin)) != ESP_OK ||
          httpd_req_get_hdr_value_str(request.req, "Host", host, sizeof(host)) != ESP_OK || std::string(origin) != std::string("http://") + host) {
        request.status = 400; request.body = "Cross-origin writes are not allowed"; return;
      }
    }
    if (!request.route) { request.status = 404; request.body = "Unknown route"; return; }
    if (request.route->upload) {
      const timeval uploadTimeout{5, 0};
      setsockopt(httpd_req_to_sockfd(request.req), SOL_SOCKET, SO_RCVTIMEO, &uploadTimeout, sizeof(uploadTimeout));
      if (!receiveUpload(request)) return;
      request.completedUpload = true;
    } else {
      char contentType[96]{};
      if (request.req->content_len > 2048) { request.status = 413; request.body = "Form too large"; return; }
      if (httpd_req_get_hdr_value_str(request.req, "Content-Type", contentType, sizeof(contentType)) != ESP_OK ||
          std::string(contentType).find("application/x-www-form-urlencoded") != 0 || queryPos != std::string::npos) {
        request.status = 400; request.body = "Expected form body without URL parameters"; return;
      }
      std::string form(request.req->content_len, '\0');
      size_t offset = 0;
      while (offset < form.size()) {
        int count = httpd_req_recv(request.req, &form[offset], form.size() - offset);
        if (count <= 0) { request.status = 400; request.body = "Incomplete form"; return; }
        offset += count;
      }
      if (!HttpRequestParsing::query(form, request.args)) { request.status = 400; request.body = "Invalid form"; return; }
    }
  }
  if (!dispatch(request)) { request.status = 503; request.body = "Control task unavailable"; }
}
// Receive one browser multipart file, bounded to a small header/tail and 2 KiB
// data chunks. No image-sized buffers, temporary files, or Arduino HTTP parser.
bool NativeHttpServer::receiveUpload(Request& request) {
  char contentType[160]{};
  if (httpd_req_get_hdr_value_str(request.req, "Content-Type", contentType, sizeof(contentType)) != ESP_OK) {
    request.status = 400; request.body = "Missing Content-Type"; return false;
  }
  std::string type(contentType);
  auto boundaryPos = type.find("boundary=");
  if (type.find("multipart/form-data") != 0 || boundaryPos == std::string::npos) {
    request.status = 400; request.body = "Expected multipart file upload"; return false;
  }
  std::string boundary = type.substr(boundaryPos + 9);
  if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"') boundary = boundary.substr(1, boundary.size() - 2);
  if (boundary.empty() || boundary.size() > 70 || boundary.find_first_of("\r\n") != std::string::npos) {
    request.status = 400; request.body = "Invalid boundary"; return false;
  }
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[BUFFER_SIZE]);
  if (!buffer) { request.status = 503; request.body = "Out of memory"; return false; }
  size_t remaining = request.req->content_len;
  std::string held;
  bool started = false, ended = false;
  const std::string delimiter = "\r\n--" + boundary;
  auto emit = [&](size_t count) {
    while (count) {
      const size_t chunk = std::min(count, BUFFER_SIZE);
      request.upload.status = UPLOAD_FILE_WRITE;
      request.upload.buf = reinterpret_cast<uint8_t*>(&held[0]);
      request.upload.currentSize = chunk;
      request.upload.totalSize += chunk;
      if (!dispatch(request, true) || request.rejected) return false;
      held.erase(0, chunk);
      count -= chunk;
    }
    return true;
  };
  while (remaining && !stopping) {
    int received = httpd_req_recv(request.req, reinterpret_cast<char*>(buffer.get()), std::min(remaining, BUFFER_SIZE));
    if (received <= 0) break; // A no-progress timeout aborts, never busy-retries.
    remaining -= received;
    held.append(reinterpret_cast<char*>(buffer.get()), received);
    if (!started) {
      const auto end = held.find("\r\n\r\n");
      if (end == std::string::npos) { if (held.size() > 2048) break; continue; }
      std::string filename;
      if (end > 2048 || held.find("--" + boundary + "\r\n") != 0 || !HttpRequestParsing::filename(held.substr(0, end), filename)) break;
      request.upload.filename = filename.c_str();
      request.upload.status = UPLOAD_FILE_START;
      if (!dispatch(request, true) || request.rejected) break;
      started = true;
      held.erase(0, end + 4);
    }
    auto end = held.find(delimiter);
    while (end != std::string::npos) {
      const auto suffix = end + delimiter.size();
      if (held.size() < suffix + 2) break;
      if (held.compare(suffix, 2, "--") == 0 || held.compare(suffix, 2, "\r\n") == 0) break;
      // A boundary prefix followed by another character is ordinary file data.
      end = held.find(delimiter, end + 1);
    }
    if (end != std::string::npos) {
      const auto suffix = held.substr(end + delimiter.size());
      if (remaining && suffix.size() < 4) continue;
      // Reject additional parts or bytes after the closing delimiter.
      if (remaining != 0 || (suffix != "--" && suffix != "--\r\n")) break;
      if (end && !emit(end)) break;
      ended = true;
      break;
    }
    if (held.size() > delimiter.size() + 4 && !emit(held.size() - delimiter.size() - 4)) break;
  }
  if (!ended || !started) {
    if (request.ownsUpload) { request.upload.status = UPLOAD_FILE_ABORTED; dispatch(request, true); }
    request.status = 400; request.body = "Upload rejected, interrupted, or malformed";
    return false;
  }
  request.upload.status = UPLOAD_FILE_END;
  if (!dispatch(request, true)) { request.status = 503; request.body = "Server stopping"; return false; }
  return true;
}
void NativeHttpServer::respond(Request& request) {
  httpd_resp_set_status(request.req, statusText(request.status));
  httpd_resp_set_type(request.req, request.type.c_str());
  httpd_resp_set_hdr(request.req, "Connection", "close");
  if (!request.file.isEmpty()) {
    File file = LittleFS.open(request.file, FILE_READ);
    if (!file) { httpd_resp_send_err(request.req, HTTPD_404_NOT_FOUND, "File unavailable"); return; }
    if (request.file.endsWith(".gz")) httpd_resp_set_hdr(request.req, "Content-Encoding", "gzip");
    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[BUFFER_SIZE]);
    if (!buffer) { unavailable(request.req, "Out of memory"); return; }
    // Content validators remain correct across filesystem updates, even for same-size files.
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool hashed = mbedtls_sha256_starts(&hash, 0) == 0;
    while (hashed && file.available()) {
      const size_t count = file.read(buffer.get(), BUFFER_SIZE);
      hashed = count && mbedtls_sha256_update(&hash, buffer.get(), count) == 0;
    }
    unsigned char digest[32];
    hashed = hashed && mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    char etag[67]{};
    if (hashed) {
      etag[0] = '"';
      for (size_t i = 0; i < sizeof(digest); ++i) snprintf(etag + 1 + 2 * i, 3, "%02x", digest[i]);
      etag[65] = '"';
      httpd_resp_set_hdr(request.req, "ETag", etag);
      httpd_resp_set_hdr(request.req, "Cache-Control", "no-cache");
      char supplied[96]{};
      if (httpd_req_get_hdr_value_str(request.req, "If-None-Match", supplied, sizeof(supplied)) == ESP_OK && strcmp(supplied, etag) == 0) {
        request.status = 304;
        httpd_resp_set_status(request.req, "304 Not Modified");
        httpd_resp_send(request.req, nullptr, 0);
        return;
      }
    }
    if (!file.seek(0)) { httpd_resp_send_err(request.req, HTTPD_500_INTERNAL_SERVER_ERROR, "File seek failed"); return; }
    while (file.available() && !stopping) {
      size_t count = file.read(buffer.get(), BUFFER_SIZE);
      if (!count || httpd_resp_send_chunk(request.req, reinterpret_cast<char*>(buffer.get()), count) != ESP_OK) return;
    }
    if (!stopping) httpd_resp_send_chunk(request.req, nullptr, 0);
  } else {
    httpd_resp_set_hdr(request.req, "Cache-Control", "no-store");
    httpd_resp_send(request.req, request.body.c_str(), request.body.length());
  }
}
String NativeHttpServer::arg(const char* name) const {
  for (const auto& arg : active->args) if (arg.first == name) return arg.second.c_str();
  return "";
}
bool NativeHttpServer::hasArg(const char* name) const {
  for (const auto& arg : active->args) if (arg.first == name) return true;
  return false;
}
String NativeHttpServer::uri() const { return active->path; }
bool NativeHttpServer::isPost() const { return active->req->method == HTTP_POST; }
void NativeHttpServer::send(int status, const char* type, const String& body) { active->status = status; active->type = type; active->body = body; }
void NativeHttpServer::sendFile(const String& path, const char* type) { active->file = path; active->type = type; }
void NativeHttpServer::rebootAfterResponse() { active->reboot = true; }
NativeHttpServer::Upload& NativeHttpServer::upload() { return active->upload; }
void NativeHttpServer::rejectUpload() { if (active) active->rejected = true; }
void NativeHttpServer::acceptUpload() { active->ownsUpload = true; }
