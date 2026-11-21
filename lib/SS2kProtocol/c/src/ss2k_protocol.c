#include "ss2k_protocol.h"
#include <string.h>

static const ss2k_descriptor descriptors[] = {
#include "descriptors.inc"
};

const ss2k_descriptor *ss2k_find(uint8_t id) {
  size_t lo = 0, hi = SS2K_COMMAND_COUNT;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (descriptors[mid].id == id) return &descriptors[mid];
    if (descriptors[mid].id < id) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
}

uint16_t ss2k_get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
int16_t ss2k_get_i16(const uint8_t *p) {
  uint16_t v = ss2k_get_u16(p);
  return (int16_t)(v <= INT16_MAX ? (int32_t)v : (int32_t)v - 65536);
}
int32_t ss2k_get_i32(const uint8_t *p) {
  uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  return v <= INT32_MAX ? (int32_t)v : (int32_t)((int64_t)v - INT64_C(4294967296));
}
void ss2k_put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
void ss2k_put_i32(uint8_t *p, int32_t value) {
  uint32_t v = (uint32_t)value;
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int valid_pair(uint16_t pair) {
  unsigned front = pair / SS2K_GEAR_PAIR_MULTIPLIER;
  unsigned rear = pair % SS2K_GEAR_PAIR_MULTIPLIER;
  unsigned ratio;
  if (!front || !rear || front > SS2K_GEAR_MAX_TOOTH || rear > SS2K_GEAR_MAX_TOOTH) return 0;
  ratio = (front * SS2K_GEAR_RATIO_SCALE + rear / 2) / rear;
  return ratio >= SS2K_GEAR_MIN_RATIO && ratio <= SS2K_GEAR_MAX_RATIO;
}

/* Payload-relative offsets: the status/ID prefix is not part of p. */
static int valid_payload(ss2k_format format, const uint8_t *p, size_t n, int request) {
  size_t i, j;
  if (n && !p) return 0;
  switch (format) {
    case SS2K_FORMAT_CAPABILITIES: return n == 5 && p[0] == SS2K_CAPABILITIES_VERSION;
    case SS2K_FORMAT_OTA_REQUEST:
      if (!n) return 0;
      switch (p[0]) {
        case SS2K_OTA_BEGIN: return n == 37 && (p[1] | p[2] | p[3] | p[4]);
        case SS2K_OTA_CHUNK: return n > 5 && n <= SS2K_SECURE_MAX_INNER - 2;
        case SS2K_OTA_FINISH:
        case SS2K_OTA_ABORT: return n == 1;
        default: return 0;
      }
    case SS2K_FORMAT_OTA_REPLY: return n == 5 && p[0] <= SS2K_OTA_ABORT;
    case SS2K_FORMAT_EMPTY: return n == 0;
    case SS2K_FORMAT_BOOL: return n == 1 && (!request || p[0] <= 1);
    case SS2K_FORMAT_U16:
    case SS2K_FORMAT_I16: return n == 2;
    case SS2K_FORMAT_I32: return n == 4;
    case SS2K_FORMAT_STRING:
      return !request || n == 0 || memchr(p, 0, n) == NULL;
    case SS2K_FORMAT_ROW_INDEX: return n == 1 && p[0] < SS2K_POWER_TABLE_ROWS;
    case SS2K_FORMAT_POWER_TABLE:
      return n == 1 + 2 * SS2K_POWER_TABLE_COLUMNS && p[0] < SS2K_POWER_TABLE_ROWS;
    case SS2K_FORMAT_GEAR_INDEX:
      return n == 0 || (n == 1 && p[0] < SS2K_GEAR_MAX_COUNT);
    case SS2K_FORMAT_GEAR_WRITE:
      if (n < 1 || p[0] > SS2K_GEAR_MAX_COUNT || p[0] == 1 || n != 1u + 2u * p[0]) return 0;
      for (i = 0; i < p[0]; ++i) {
        if (!valid_pair(ss2k_get_u16(p + 1 + 2 * i))) return 0;
        for (j = 0; j < i; ++j) if (ss2k_get_u16(p + 1 + 2 * i) == ss2k_get_u16(p + 1 + 2 * j)) return 0;
      }
      return 1;
    case SS2K_FORMAT_GEAR_REPLY:
      return (n == 1 || n == 4) && p[0] <= SS2K_GEAR_MAX_COUNT && p[0] != 1 &&
        (n == 1 || (p[1] < p[0] && valid_pair(ss2k_get_u16(p + 2))));
    case SS2K_FORMAT_SNAPSHOT:
      return n >= SS2K_SNAPSHOT_HEADER_LENGTH - 2 && p[0] == SS2K_SNAPSHOT_VERSION &&
        ss2k_get_u16(p + 3) != 0 && ss2k_get_u16(p + 1) < ss2k_get_u16(p + 3);
    case SS2K_FORMAT_SCAN:
      if (n < SS2K_SCAN_HEADER_LENGTH - 2 || p[0] != SS2K_SCAN_VERSION ||
          p[1] > SS2K_SCAN_END || p[7] == 0 || p[6] >= p[7]) return 0;
      return p[1] == SS2K_SCAN_DEVICE || (n == SS2K_SCAN_HEADER_LENGTH - 2 && p[6] == 0 && p[7] == 1);
    default: return 0;
  }
}

static ss2k_result request_format(uint8_t op, uint8_t id, ss2k_format *format) {
  const ss2k_descriptor *d = ss2k_find(id);
  if (!d) return SS2K_UNKNOWN_ID;
  if (op != SS2K_READ && op != SS2K_WRITE) return SS2K_INVALID;
  *format = (ss2k_format)(op == SS2K_READ ? d->read_request : d->write_request);
  return *format == SS2K_FORMAT_NONE ? SS2K_UNSUPPORTED : SS2K_OK;
}
ss2k_result ss2k_validate_request(const uint8_t *p, size_t n) {
  ss2k_format f;
  ss2k_result r;
  if (!p || n < 2) return SS2K_INVALID;
  r = request_format(p[0], p[1], &f);
  if (r != SS2K_OK) return r;
  return valid_payload(f, p + 2, n - 2, 1) ? SS2K_OK : SS2K_INVALID;
}
ss2k_result ss2k_encode_request(uint8_t op, uint8_t id, const uint8_t *p, size_t n,
    uint8_t *out, size_t capacity, size_t *written) {
  ss2k_format f;
  ss2k_result r;
  if (!written) return SS2K_INVALID;
  *written = 0;
  r = request_format(op, id, &f);
  if (r != SS2K_OK) return r;
  if (!valid_payload(f, p, n, 1)) return SS2K_INVALID;
  if (capacity < 2 || n > capacity - 2) return SS2K_BUFFER_TOO_SMALL;
  if (!out) return SS2K_INVALID;
  /* memmove permits a payload view into out. Prefix is written after copying. */
  if (n) memmove(out + 2, p, n);
  out[0] = op; out[1] = id; *written = n + 2;
  return SS2K_OK;
}
ss2k_result ss2k_encode_scalar_write(uint8_t id, int64_t value, uint8_t *out, size_t capacity, size_t *written) {
  ss2k_format f;
  uint8_t p[4];
  size_t n;
  ss2k_result r;
  if (!written) return SS2K_INVALID;
  *written = 0;
  r = request_format(SS2K_WRITE, id, &f);
  if (r != SS2K_OK) return r;
  switch (f) {
    case SS2K_FORMAT_BOOL:
      if (value < 0 || value > 1) return SS2K_INVALID;
      n = 1; p[0] = (uint8_t)value; break;
    case SS2K_FORMAT_U16:
      if (value < 0 || value > UINT16_MAX) return SS2K_INVALID;
      n = 2; ss2k_put_u16(p, (uint16_t)value); break;
    case SS2K_FORMAT_I16:
      if (value < INT16_MIN || value > INT16_MAX) return SS2K_INVALID;
      n = 2; ss2k_put_u16(p, (uint16_t)value); break;
    case SS2K_FORMAT_I32:
      if (value < INT32_MIN || value > INT32_MAX) return SS2K_INVALID;
      n = 4; ss2k_put_i32(p, (int32_t)value); break;
    default: return SS2K_UNSUPPORTED;
  }
  return ss2k_encode_request(SS2K_WRITE, id, p, n, out, capacity, written);
}
ss2k_result ss2k_decode_response(const uint8_t *p, size_t n, ss2k_reply_kind kind,
    int legacy_boolean_echo, ss2k_packet *view) {
  const ss2k_descriptor *d;
  ss2k_format f;
  if (view) memset(view, 0, sizeof(*view));
  if (!p || !view || n < 2 || kind < SS2K_REPLY_READ || kind > SS2K_NOTIFICATION) return SS2K_INVALID;
  if (p[0] == SS2K_ERROR) {
    view->status = p[0]; view->id = p[1]; view->payload = p + 2; view->payload_length = n - 2;
    return SS2K_REMOTE_ERROR;
  }
  if (p[0] != SS2K_SUCCESS) return SS2K_INVALID;
  d = ss2k_find(p[1]);
  if (!d) return SS2K_UNKNOWN_ID;
  f = (ss2k_format)(kind == SS2K_REPLY_READ ? d->read_response : kind == SS2K_REPLY_WRITE ? d->write_response : d->notification);
  if (f == SS2K_FORMAT_NONE) return SS2K_UNSUPPORTED;
  if (legacy_boolean_echo && kind == SS2K_REPLY_WRITE && f == SS2K_FORMAT_BOOL && n == 4 && p[3] == 0) --n;
  if (!valid_payload(f, p + 2, n - 2, 0)) return SS2K_INVALID;
  view->status = p[0]; view->id = p[1]; view->format = f; view->payload = p + 2; view->payload_length = n - 2;
  return SS2K_OK;
}
ss2k_result ss2k_decode_scalar(const ss2k_packet *view, int64_t *value) {
  if (!view || !value || view->status != SS2K_SUCCESS || !valid_payload(view->format, view->payload, view->payload_length, 0)) return SS2K_INVALID;
  switch (view->format) {
    case SS2K_FORMAT_BOOL: *value = view->payload[0] != 0; break;
    case SS2K_FORMAT_U16: *value = ss2k_get_u16(view->payload); break;
    case SS2K_FORMAT_I16: *value = ss2k_get_i16(view->payload); break;
    case SS2K_FORMAT_I32: *value = ss2k_get_i32(view->payload); break;
    default: return SS2K_UNSUPPORTED;
  }
  return SS2K_OK;
}

ss2k_result ss2k_encode_response(uint8_t status, uint8_t id, ss2k_reply_kind kind,
    const uint8_t *p, size_t n, uint8_t *out, size_t capacity, size_t *written) {
  const ss2k_descriptor *d;
  ss2k_format f;
  if (!written) return SS2K_INVALID;
  *written = 0;
  if ((n && !p) || kind < SS2K_REPLY_READ || kind > SS2K_NOTIFICATION) return SS2K_INVALID;
  if (status != SS2K_ERROR) {
    if (status != SS2K_SUCCESS) return SS2K_INVALID;
    d = ss2k_find(id);
    if (!d) return SS2K_UNKNOWN_ID;
    f = (ss2k_format)(kind == SS2K_REPLY_READ ? d->read_response : kind == SS2K_REPLY_WRITE ? d->write_response : d->notification);
    if (f == SS2K_FORMAT_NONE) return SS2K_UNSUPPORTED;
    if (!valid_payload(f, p, n, 0)) return SS2K_INVALID;
  }
  if (capacity < 2 || n > capacity - 2) return SS2K_BUFFER_TOO_SMALL;
  if (!out) return SS2K_INVALID;
  if (n) memmove(out + 2, p, n);
  out[0] = status; out[1] = id; *written = n + 2;
  return SS2K_OK;
}
