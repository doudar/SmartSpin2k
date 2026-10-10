/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>
#include <functional>
#include <vector>
#include <string>

// Native ESP-IDF transport. Route callbacks run only on maintenance; socket I/O
// runs on a bounded worker. This is not Arduino WebServer or an AsyncTCP wrapper.
class NativeHttpServer {
 public:
  enum UploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
  struct Upload {
    UploadStatus status = UPLOAD_FILE_START;
    String filename;
    uint8_t* buf = nullptr;
    size_t currentSize = 0, totalSize = 0;
  };
  using Handler = std::function<void()>;
  void on(const char* path, Handler handler) { on(path, HTTP_GET, std::move(handler)); }
  void on(const char* path, httpd_method_t method, Handler handler, Handler upload = {});
  void onNotFound(Handler handler) { notFound = std::move(handler); }
  bool begin();
  void stop();
  void processPending();
  String arg(const char* name) const;
  bool hasArg(const char* name) const;
  String uri() const;
  bool isPost() const;
  void send(int status, const char* type, const String& body);
  void sendFile(const String& path, const char* type);
  void rebootAfterResponse();
  Upload& upload();
  void rejectUpload();
  void acceptUpload();
 private:
  struct Route { String path; httpd_method_t method; Handler handler, upload; };
  struct Request;
  static esp_err_t accept(httpd_req_t* req);
  static void workerEntry(void* context);
  void workerLoop();
  bool dispatch(Request& request, bool uploadEvent = false);
  void handle(Request& request);
  bool receiveUpload(Request& request);
  void respond(Request& request);
  std::vector<Route> routes;
  Handler notFound;
  httpd_handle_t handle_ = nullptr;
  QueueHandle_t incoming = nullptr, pending = nullptr;
  SemaphoreHandle_t workerStopped = nullptr, admission = nullptr;
  TaskHandle_t worker = nullptr;
  std::atomic<bool> stopping{false};
  std::atomic<bool> rebootPending{false};
  Request* active = nullptr; // Maintenance task only.
};
