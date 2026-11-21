#ifndef SS2K_PROTOCOL_H
#define SS2K_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include "ss2k_protocol_generated.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Allocation-free C99. Input/output buffers remain owned by the caller.
 * No transport, globals with mutable state, Arduino or platform dependencies.
 * Descriptors reside in read-only storage; returned packet views borrow input.
 */
typedef struct {
  uint8_t id, read_request, read_response, write_request, write_response, notification;
  uint16_t scale;
  uint8_t completion_notification;
} ss2k_descriptor;

typedef enum {
  SS2K_OK = 0, SS2K_INVALID, SS2K_UNKNOWN_ID, SS2K_UNSUPPORTED,
  SS2K_BUFFER_TOO_SMALL, SS2K_REMOTE_ERROR
} ss2k_result;
typedef enum { SS2K_REPLY_READ, SS2K_REPLY_WRITE, SS2K_NOTIFICATION } ss2k_reply_kind;
typedef struct {
  uint8_t status, id;
  ss2k_format format;
  const uint8_t *payload;
  size_t payload_length;
} ss2k_packet;

const ss2k_descriptor *ss2k_find(uint8_t id);
/* Canonical request validation, including read-only/action direction checks. */
ss2k_result ss2k_validate_request(const uint8_t *packet, size_t length);
/* Encoders do not touch output on failure; output length becomes zero. */
ss2k_result ss2k_encode_request(uint8_t operation, uint8_t id,
  const uint8_t *payload, size_t payload_length, uint8_t *out, size_t capacity, size_t *written);
/* Raw integer units. Divide a decoded value by descriptor.scale for engineering units. */
ss2k_result ss2k_encode_scalar_write(uint8_t id, int64_t value,
  uint8_t *out, size_t capacity, size_t *written);
ss2k_result ss2k_encode_response(uint8_t status, uint8_t id, ss2k_reply_kind kind,
  const uint8_t *payload, size_t payload_length, uint8_t *out, size_t capacity, size_t *written);
/* Context is required: logging write ACKs are bool; logging reads are text.
 * Authenticated, decrypted 0x81 packets require SS2K_NOTIFICATION; legacy
 * notifications use 0x80. view->status preserves the original wire status.
 * legacy_boolean_echo accepts a second, zero padding byte only on write ACKs.
 * Errors return SS2K_REMOTE_ERROR even for an unknown ID; view then holds error bytes.
 */
ss2k_result ss2k_decode_response(const uint8_t *packet, size_t length,
  ss2k_reply_kind kind, int legacy_boolean_echo, ss2k_packet *view);
ss2k_result ss2k_decode_scalar(const ss2k_packet *view, int64_t *value);

/* Length checks must precede these endian helpers. No alignment assumptions. */
uint16_t ss2k_get_u16(const uint8_t *p);
int16_t ss2k_get_i16(const uint8_t *p);
int32_t ss2k_get_i32(const uint8_t *p);
void ss2k_put_u16(uint8_t *p, uint16_t value);
void ss2k_put_i32(uint8_t *p, int32_t value);
#ifdef __cplusplus
}
#endif
#endif
