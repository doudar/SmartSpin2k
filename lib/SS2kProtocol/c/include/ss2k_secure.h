#ifndef SS2K_SECURE_H
#define SS2K_SECURE_H
#include "ss2k_protocol_generated.h"
#include <stddef.h>
#include <string.h>

/* Caller owns its buffer and sets state to {0,0} on disconnect or timeout.
 * Non-fragmented records are passed through without allocating or copying. */
typedef struct { size_t used; unsigned next; } ss2k_fragment_state;
typedef enum { SS2K_FRAME_INVALID = -1, SS2K_FRAME_PENDING, SS2K_FRAME_COMPLETE } ss2k_frame_result;
static inline ss2k_frame_result ss2k_secure_accept(ss2k_fragment_state *s, uint8_t *buf, size_t cap,
    const uint8_t *wire, size_t len, const uint8_t **record, size_t *size) {
  unsigned flags, index;
  if (!s || !record || !size) return SS2K_FRAME_INVALID;
  *record = NULL; *size = 0;
  if (!wire || !len || len > SS2K_SECURE_MAX_WIRE) goto invalid;
  if (wire[0] != SS2K_SECURE_FRAGMENT) {
    if (s->next) goto invalid;
    *record = wire; *size = len; return SS2K_FRAME_COMPLETE;
  }
  if (len <= SS2K_SECURE_FRAGMENT_HEADER || !buf) goto invalid;
  flags = wire[1]; index = wire[2];
  if (flags > 3 || ((flags & 1u) != (index == 0)) || index != s->next) goto invalid;
  if (!index && wire[3] == SS2K_SECURE_FRAGMENT) goto invalid;
  if (index == 255 && !(flags & 2u)) goto invalid;
  if (cap > SS2K_SECURE_MAX_WIRE) cap = SS2K_SECURE_MAX_WIRE;
  len -= SS2K_SECURE_FRAGMENT_HEADER;
  if (s->used > cap || len > cap - s->used) goto invalid;
  memcpy(buf + s->used, wire + SS2K_SECURE_FRAGMENT_HEADER, len);
  s->used += len;
  if (flags & 2u) {
    *record = buf; *size = s->used;
    s->used = 0; s->next = 0; return SS2K_FRAME_COMPLETE;
  }
  ++s->next;
  return SS2K_FRAME_PENDING;
invalid:
  s->used = 0; s->next = 0;
  return SS2K_FRAME_INVALID;
}
#endif
