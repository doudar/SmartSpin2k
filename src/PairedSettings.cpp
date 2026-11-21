/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "PairedSettings.h"
#include "SettingsUpdate.h"
#include "ByteUtils.h"
#include "Main.h"
#include "ss2k_protocol.h"
#include "ss2k_secure.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <esp_mac.h>
#include <esp_random.h>
#include <nvs.h>
#include <mbedtls/ccm.h>
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <mbedtls/ecdh.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#if !CONFIG_MBEDTLS_HARDWARE_AES
#error The paired settings channel requires hardware-backed AES.
#endif

namespace PairedSettings {
namespace {
constexpr uint32_t handshakeTimeout = 60000;
constexpr uint32_t buttonHold       = 2000;
constexpr size_t maximumPlain       = SS2K_SECURE_MAX_INNER;
constexpr size_t maximumWire        = SS2K_SECURE_MAX_WIRE;
constexpr size_t maxBonds           = 8;
enum class State { Idle, Button, PairProof, ResumeProof, Ready };
struct Bond {
  uint8_t id[8]{}, key[32]{};
};
struct Secret {
  Bond bond;
  uint8_t material[64]{}, transcript[32]{};
  ~Secret() { mbedtls_platform_zeroize(this, sizeof(*this)); }
};
struct Peer {
  uint16_t id;
  uint64_t generation;
  uint64_t nextPublication;
  size_t capacity;
  State state       = State::Idle;
  uint32_t activity = 0, nextTx = 0, lastRx = 0;
  bool hasRx = false;
  ss2k_fragment_state fragmentState{};
  uint32_t fragmentAt = 0;
  Bytes fragments, request;
  // Allocated only on links requiring outbound fragmentation.
  std::unique_ptr<std::vector<Bytes>> outbound;
  size_t sent       = 0;
  uint8_t sendIndex = 0;
  uint32_t lastSend = 0;
  std::unique_ptr<Secret> secret;
};
std::list<Peer> peers;
// Scan notifications can originate on nimble_host. Only move their bytes into
// this bounded queue there. Each authenticated peer has its own read cursor.
struct Publication {
  uint64_t sequence;
  std::shared_ptr<const Bytes> packet;
};
std::list<Publication> publications;
size_t publicationBytes  = 0;
uint64_t nextPublication = 1;
std::mutex publicationMutex;
std::atomic<unsigned> authenticatedPeers{0};
std::recursive_mutex mutex;
Emit emitFrame;
Alive isAlive;
Dispatch dispatch;
uint8_t deviceId[8]{};
bool idReady = false, released = false, holding = false, rejectHold = false;
uint32_t holdStart;
struct Snapshot {
  uint16_t peer = UINT16_MAX, chunk = 0;
  uint64_t generation = 0;
  std::string json;
  size_t stride = 0;
  uint32_t sent = 0;
} snapshot;

struct Nvs {
  nvs_handle_t handle{};
  ~Nvs() {
    if (handle) nvs_close(handle);
  }
  bool open() { return nvs_open("ss2k-pairs", NVS_READWRITE, &handle) == ESP_OK; }
};
// Treat corrupt or inaccessible data as a storage failure. Never silently
// evict a working pairing or turn failure to read into a successful forget.
bool bondSlot(nvs_handle_t handle, const uint8_t* id, Bond& found, char (&name)[8], bool& exists) {
  int freeSlot = -1;
  for (unsigned i = 0; i < maxBonds; ++i) {
    snprintf(name, sizeof(name), "key%u", i);
    size_t size = sizeof(found);
    auto err    = nvs_get_blob(handle, name, &found, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      if (freeSlot < 0) freeSlot = i;
      continue;
    }
    if (err != ESP_OK || size != sizeof(found)) return false;
    if (!memcmp(found.id, id, 8)) {
      exists = true;
      return true;
    }
  }
  exists = false;
  if (freeSlot >= 0)
    snprintf(name, sizeof(name), "key%u", unsigned(freeSlot));
  else
    name[0] = 0;
  return true;
}
enum class BondOp { Read, Save, Forget };
enum class BondResult { Ok, Unknown, StorageError };
BondResult storeBond(BondOp op, Bond& bond) {
  Nvs db;
  Secret scratch;
  char name[8];
  bool exists = false;
  if (!db.open() || !bondSlot(db.handle, bond.id, scratch.bond, name, exists)) return BondResult::StorageError;
  if (op == BondOp::Read) {
    if (exists) bond = scratch.bond;
    return exists ? BondResult::Ok : BondResult::Unknown;
  }
  if (op == BondOp::Forget) return exists && nvs_erase_key(db.handle, name) == ESP_OK && nvs_commit(db.handle) == ESP_OK ? BondResult::Ok : BondResult::StorageError;
  // A second pairing must never silently replace another app's credentials.
  return !exists && name[0] && nvs_set_blob(db.handle, name, &bond, sizeof(bond)) == ESP_OK && nvs_commit(db.handle) == ESP_OK ? BondResult::Ok : BondResult::StorageError;
}
const mbedtls_md_info_t* sha() { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); }
int randomBytes(void*, unsigned char* out, size_t n) {
  esp_fill_random(out, n);
  return 0;
}
bool derive(const uint8_t* key, const uint8_t* salt, const char* label, uint8_t* out, size_t n) {
  return !mbedtls_hkdf(sha(), salt, 32, key, 32, reinterpret_cast<const uint8_t*>(label), strlen(label), out, n);
}
bool proof(const Secret& secret, const char* label, bool session, uint8_t* out) {
  uint8_t input[40], digest[32];
  size_t n = strlen(label);
  memcpy(input, label, n);
  if (session) {
    memcpy(input + n, secret.transcript, 32);
    n += 32;
  }
  int result = mbedtls_md_hmac(sha(), secret.bond.key, 32, input, n, digest);
  if (!result) memcpy(out, digest, 16);
  mbedtls_platform_zeroize(digest, sizeof(digest));
  return !result;
}
bool pairedKey(Peer& peer, Bytes& response) {
  mbedtls_ecp_group group;
  mbedtls_ecp_point remote, local;
  mbedtls_mpi priv, shared;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&remote);
  mbedtls_ecp_point_init(&local);
  mbedtls_mpi_init(&priv);
  mbedtls_mpi_init(&shared);
  uint8_t x[32], salt[32];
  size_t written = 0;
  bool ok        = !mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) && !mbedtls_ecp_point_read_binary(&group, &remote, peer.request.data() + 9, 65) &&
            !mbedtls_ecp_check_pubkey(&group, &remote) && !mbedtls_ecdh_gen_public(&group, &priv, &local, randomBytes, nullptr) &&
            !mbedtls_ecp_point_write_binary(&group, &local, MBEDTLS_ECP_PF_UNCOMPRESSED, &written, response.data() + 1, 65) && written == 65 &&
            !mbedtls_ecdh_compute_shared(&group, &shared, &remote, &priv, randomBytes, nullptr) && !mbedtls_mpi_write_binary(&shared, x, sizeof(x));
  mbedtls_mpi_free(&shared);
  mbedtls_mpi_free(&priv);
  mbedtls_ecp_point_free(&local);
  mbedtls_ecp_point_free(&remote);
  mbedtls_ecp_group_free(&group);
  if (ok) {
    Bytes transcript(peer.request);
    transcript.insert(transcript.end(), response.begin(), response.end());
    transcript.insert(transcript.end(), deviceId, deviceId + 8);
    ok = !mbedtls_sha256(transcript.data(), transcript.size(), salt, 0) && derive(x, salt, "SS2K/pair/v1", peer.secret->bond.key, 32);
  }
  mbedtls_platform_zeroize(x, sizeof(x));
  return ok;
}

void clear(Peer& p) {
  if (p.state == State::Ready) --authenticatedPeers;
  SettingsUpdate::disconnect(p.id);
  if (snapshot.peer == p.id) snapshot = {};
  p.secret.reset();
  p.state         = State::Idle;
  p.nextTx        = 0;
  p.hasRx         = false;
  p.fragments     = {};
  p.fragmentState = {};
  p.request       = {};
  p.outbound.reset();
  p.sent = p.sendIndex = 0;
}
bool send(Peer& p, const Bytes& packet, Bytes* immediate) {
  if (!isAlive(p.id, p.generation) || packet.empty()) return false;
  if (!p.outbound && packet.size() <= p.capacity) {
    if (immediate) {
      *immediate = packet;
      return true;
    }
    return emitFrame(p.id, p.generation, packet.data(), packet.size());
  }
  if (!p.outbound) {
    p.outbound = std::make_unique<std::vector<Bytes>>();
    p.lastSend = millis();
  }
  // Never interleave records on an ordered fragment stream.
  if (p.outbound->size() >= 4) return false;
  p.outbound->push_back(packet);
  return true;
}
void flush(Peer& p) {
  if (!p.outbound) return;
  if (uint32_t(millis() - p.lastSend) >= 5000) {
    clear(p);
    return;
  }
  const auto& packet = p.outbound->front();
  size_t n           = std::min(p.capacity - 3, packet.size() - p.sent);
  if (packet.size() <= p.capacity) {
    if (!emitFrame(p.id, p.generation, packet.data(), packet.size())) return;
    p.sent = packet.size();
  } else {
    Bytes part(3 + n);
    part[0] = SS2K_SECURE_FRAGMENT;
    part[1] = (p.sent ? 0 : 1) | (p.sent + n == packet.size() ? 2 : 0);
    part[2] = p.sendIndex;
    memcpy(part.data() + 3, packet.data() + p.sent, n);
    if (!emitFrame(p.id, p.generation, part.data(), part.size())) return;
    p.sent += n;
    ++p.sendIndex;
  }
  p.lastSend = millis();
  if (p.sent == packet.size()) {
    p.sent = p.sendIndex = 0;
    p.outbound->erase(p.outbound->begin());
    if (p.outbound->empty()) p.outbound.reset();
  }
}

bool crypt(const Peer& p, bool outbound, const uint8_t* in, size_t n, uint8_t* out, uint8_t* tag, const uint8_t* header) {
  uint8_t nonce[12];
  memcpy(nonce, p.secret->material + (outbound ? 40 : 32), 8);
  memcpy(nonce + 8, header + 1, 4);
  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);
  int err = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, p.secret->material + (outbound ? 16 : 0), 128);
  if (!err) {
    err = outbound ? mbedtls_ccm_encrypt_and_tag(&ctx, n, nonce, sizeof(nonce), header, 5, in, out, tag, 8)
                   : mbedtls_ccm_auth_decrypt(&ctx, n, nonce, sizeof(nonce), header, 5, in, out, tag, 8);
  }
  mbedtls_ccm_free(&ctx);
  return !err;
}
bool seal(Peer& p, const Bytes& plain, Bytes* immediate = nullptr) {
  if (plain.empty() || plain.size() > maximumPlain || p.state != State::Ready) return false;
  if (p.nextTx == UINT32_MAX) {
    clear(p);
    return false;
  }
  Bytes packet(SS2K_SECURE_OVERHEAD + plain.size());
  packet[0] = SS2K_SECURE_DATA;
  put_le32(packet.data() + 1, p.nextTx++);
  return crypt(p, true, plain.data(), plain.size(), packet.data() + 5, packet.data() + 5 + plain.size(), packet.data()) && send(p, packet, immediate);
}

Bytes snapshotChunk(Peer& p, bool start) {
  if (start) {
    if (snapshot.peer != UINT16_MAX) return {SS2K_ERROR, SS2K_ID_ALL_SETTINGS};
    String json         = userConfig->returnJSON();
    snapshot            = {};
    snapshot.peer       = p.id;
    snapshot.generation = p.generation;
    snapshot.json.assign(json.c_str(), json.length());
    // Small MTUs use the secure fragmentation layer. Larger MTUs fit one
    // complete authenticated record per ATT notification / DirCon message.
    snapshot.stride = std::max<size_t>(16, p.capacity > 20 ? p.capacity - 20 : 0);
  }
  size_t pos = snapshot.chunk * snapshot.stride;
  size_t n   = std::min(snapshot.stride, snapshot.json.size() - pos);
  Bytes chunk(7 + n);
  chunk[0] = SS2K_SUCCESS;
  chunk[1] = SS2K_ID_ALL_SETTINGS;
  chunk[2] = 1;
  put_le16(chunk.data() + 3, snapshot.chunk++);
  put_le16(chunk.data() + 5, (snapshot.json.size() + snapshot.stride - 1) / snapshot.stride);
  memcpy(chunk.data() + 7, snapshot.json.data() + pos, n);
  snapshot.sent = millis();
  if (pos + n == snapshot.json.size()) snapshot = {};
  return chunk;
}

void publishNext(Peer& p) {
  if (p.state != State::Ready || ss2k->isUpdating || (p.outbound && p.outbound->size() > 1)) return;
  std::shared_ptr<const Bytes> packet;
  {
    std::lock_guard<std::mutex> lock(publicationMutex);
    for (auto& event : publications) {
      if (event.sequence >= p.nextPublication) {
        packet            = event.packet;
        p.nextPublication = event.sequence + 1;
        break;
      }
    }
  }
  if (packet) seal(p, *packet);
}

void dataPacket(Peer& p, const uint8_t* packet, size_t n, Bytes* immediate) {
  if (p.state != State::Ready || n < 15 || n > maximumWire) return;
  uint32_t seq = get_le32(packet + 1);
  if (p.hasRx && seq <= p.lastRx) return;
  Bytes plain(n - SS2K_SECURE_OVERHEAD);
  if (!crypt(p, false, packet + 5, plain.size(), plain.data(), const_cast<uint8_t*>(packet + n - 8), packet)) {
    clear(p);
    return;
  }
  p.lastRx = seq;
  p.hasRx  = true;
  if (ss2k_validate_request(plain.data(), plain.size()) != SS2K_OK) {
    seal(p, {SS2K_ERROR, plain[1]}, immediate);
    return;
  }
  if (plain[0] == SS2K_WRITE && plain[1] == SS2K_ID_FORGET_PEER && plain.size() == 2) {
    uint8_t id[8];
    memcpy(id, p.secret->bond.id, 8);
    if (p.outbound) {
      seal(p, {SS2K_ERROR, SS2K_ID_FORGET_PEER}, immediate);
      return;
    }
    bool removed = storeBond(BondOp::Forget, p.secret->bond) == BondResult::Ok;
    seal(p, {static_cast<uint8_t>(removed ? SS2K_SUCCESS : SS2K_ERROR), SS2K_ID_FORGET_PEER}, immediate);
    if (removed)
      for (auto& other : peers)
        if (other.secret && !memcmp(other.secret->bond.id, id, 8)) clear(other);
    return;
  }
  Bytes reply;
  if (plain[1] == SS2K_ID_FIRMWARE_STREAM)
    reply = SettingsUpdate::command(p.id, plain.data(), plain.size());
  else if (ss2k->isUpdating || ss2k->resetDefaultsFlag || ss2k->rebootFlag)
    reply = {SS2K_ERROR, plain[1]};
  else if (plain[0] == SS2K_READ && plain[1] == SS2K_ID_ALL_SETTINGS && plain.size() == 2)
    reply = snapshotChunk(p, true);
  else
    reply = dispatch(std::string(reinterpret_cast<const char*>(plain.data()), plain.size()));
  if (!reply.empty()) seal(p, reply, immediate);
}

void process(Peer& p, const uint8_t* data, size_t n, Bytes* immediate) {
  auto error = [&](uint8_t reason) { send(p, {SS2K_SECURE_ERROR, reason}, immediate); };
  if (!n || n > maximumWire || !idReady) return;
  if (data[0] == SS2K_SECURE_DATA) {
    dataPacket(p, data, n, immediate);
    return;
  }
  if (n == 2 && data[0] == SS2K_READ && data[1] == SS2K_ID_CAPABILITIES) {
    send(p, dispatch(std::string(reinterpret_cast<const char*>(data), n)), immediate);
    return;
  }
  if (data[0] == SS2K_SECURE_INFO && n == 1) {
    Bytes info(9, SS2K_SECURE_IDENTITY);
    memcpy(info.data() + 1, deviceId, 8);
    send(p, info, immediate);
    return;
  }
  if (ss2k->isUpdating || ss2k->rebootFlag || ss2k->resetDefaultsFlag) {
    error(2);
    return;
  }
  if (data[0] == SS2K_SECURE_PAIR && n == 74 && data[9] == 4) {
    for (auto& other : peers)
      if (other.state == State::Button || other.state == State::PairProof) {
        clear(other);
        rejectHold = true;
        if (&other != &p) send(other, {SS2K_SECURE_ERROR, 2}, nullptr);
        error(2);
        return;
      }
    clear(p);
    p.secret = std::make_unique<Secret>();
    memcpy(p.secret->bond.id, data + 1, 8);
    p.request.assign(data, data + n);
    p.state    = State::Button;
    p.activity = millis();
    released = holding = false;
    return;
  }
  if (data[0] == SS2K_SECURE_CONFIRM && n == 17 && p.state == State::PairProof) {
    uint8_t expected[16];
    Bytes reply(17, SS2K_SECURE_PAIRED);
    bool valid = proof(*p.secret, "client", false, expected) && !mbedtls_ct_memcmp(expected, data + 1, 16);
    if (valid && proof(*p.secret, "device", false, reply.data() + 1) && storeBond(BondOp::Save, p.secret->bond) == BondResult::Ok)
      send(p, reply, immediate);
    else
      error(valid ? 5 : 1);
    clear(p);
    return;
  }
  if (data[0] == SS2K_SECURE_RESUME && n == 25) {
    clear(p);
    p.secret = std::make_unique<Secret>();
    memcpy(p.secret->bond.id, data + 1, 8);
    auto result = storeBond(BondOp::Read, p.secret->bond);
    if (result != BondResult::Ok) {
      clear(p);
      error(result == BondResult::Unknown ? 3 : 5);
      return;
    }
    Bytes reply(33, SS2K_SECURE_CHALLENGE);
    esp_fill_random(reply.data() + 1, 16);
    uint8_t transcript[49];
    memcpy(transcript, deviceId, 8);
    memcpy(transcript + 8, data, 25);
    memcpy(transcript + 33, reply.data() + 1, 16);
    if (mbedtls_sha256(transcript, sizeof(transcript), p.secret->transcript, 0) || !derive(p.secret->bond.key, p.secret->transcript, "SS2K/session/v1", p.secret->material, 64) ||
        !proof(*p.secret, "server", true, reply.data() + 17)) {
      clear(p);
      error(1);
      return;
    }
    p.state    = State::ResumeProof;
    p.activity = millis();
    send(p, reply, immediate);
    return;
  }
  if (data[0] == SS2K_SECURE_PROVE && n == 17 && p.state == State::ResumeProof) {
    uint8_t expected[16];
    Bytes ready(17, SS2K_SECURE_READY);
    if (!proof(*p.secret, "client", true, expected) || mbedtls_ct_memcmp(expected, data + 1, 16) || !proof(*p.secret, "ready", true, ready.data() + 1)) {
      clear(p);
      error(1);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(publicationMutex);
      p.nextPublication = nextPublication;
    }
    p.state = State::Ready;
    ++authenticatedPeers;
    send(p, ready, immediate);
    return;
  }
  error(1);
}
}  // namespace

void initialize(Emit emit, Alive alive, Dispatch handlers) {
  emitFrame = emit;
  isAlive   = alive;
  dispatch  = handlers;
  idReady   = esp_efuse_mac_get_default(deviceId) == ESP_OK;
}
void receive(uint16_t peer, uint64_t gen, size_t capacity, const uint8_t* bytes, size_t n, Bytes* immediate) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!n || n > capacity || capacity < 20 || !isAlive(peer, gen)) return;
  auto it = std::find_if(peers.begin(), peers.end(), [&](const Peer& p) { return p.id == peer; });
  if (it != peers.end() && it->generation != gen) {
    clear(*it);
    peers.erase(it);
    it = peers.end();
  }
  if (it == peers.end()) it = peers.emplace(peers.end(), Peer{peer, gen, 0, capacity});
  Peer& p    = *it;
  p.capacity = std::min<size_t>(capacity, 512);
  if (bytes[0] == SS2K_SECURE_FRAGMENT && p.fragments.empty()) p.fragments.resize(maximumWire);
  const uint8_t* record = nullptr;
  size_t size           = 0;
  auto result           = ss2k_secure_accept(&p.fragmentState, p.fragments.data(), p.fragments.size(), bytes, n, &record, &size);
  p.fragmentAt          = millis();
  if (result == SS2K_FRAME_COMPLETE) {
    Bytes assembled;
    if (record == p.fragments.data()) assembled.swap(p.fragments);
    process(p, record, size, immediate);
  }
  if (result != SS2K_FRAME_PENDING) Bytes{}.swap(p.fragments);
}

void disconnect(uint16_t peer) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  peers.remove_if([&](Peer& p) {
    if (p.id != peer) return false;
    clear(p);
    return true;
  });
}

void tick() {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  SettingsUpdate::tick();
  for (auto it = peers.begin(); it != peers.end();) {
    Peer& p = *it;
    if (!isAlive(p.id, p.generation)) {
      clear(p);
      it = peers.erase(it);
      continue;
    }
    if (p.state != State::Ready && p.state != State::Idle && uint32_t(millis() - p.activity) >= handshakeTimeout) {
      clear(p);
      send(p, {SS2K_SECURE_ERROR, 4}, nullptr);
    }
    if (p.fragmentState.next && uint32_t(millis() - p.fragmentAt) >= 5000) {
      p.fragmentState = {};
      Bytes{}.swap(p.fragments);
    }
    flush(p);
    publishNext(p);
    if (!p.outbound && snapshot.peer == p.id && snapshot.generation == p.generation && uint32_t(millis() - snapshot.sent) >= 40)
      if (!seal(p, snapshotChunk(p, false))) snapshot = {};
    ++it;
  }
  std::lock_guard<std::mutex> queueLock(publicationMutex);
  auto firstUnread = nextPublication;
  for (const auto& p : peers)
    if (p.state == State::Ready) firstUnread = std::min(firstUnread, p.nextPublication);
  while (!publications.empty() && publications.front().sequence < firstUnread) {
    publicationBytes -= publications.front().packet->size();
    publications.pop_front();
  }
}

bool buttons(bool up, bool down) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (rejectHold) {
    rejectHold = up || down;
    return rejectHold;
  }
  for (auto& p : peers) {
    if (p.state != State::Button) continue;
    if (!up && !down) released = true;
    if (!released || !up || !down) {
      holding = false;
      return up && down;
    }
    if (!holding) {
      holding   = true;
      holdStart = millis();
    } else if (uint32_t(millis() - holdStart) >= buttonHold) {
      Bytes reply(66, SS2K_SECURE_OFFER);
      if (pairedKey(p, reply) && send(p, reply, nullptr)) {
        p.state    = State::PairProof;
        p.activity = millis();
      } else {
        clear(p);
        send(p, {SS2K_SECURE_ERROR, 1}, nullptr);
      }
      holding    = false;
      rejectHold = true;
    }
    return true;
  }
  return false;
}

bool resetPending() {
  Nvs db;
  uint8_t marker = 0;
  return db.open() && nvs_get_u8(db.handle, "reset", &marker) == ESP_OK && marker == 1;
}
bool beginReset() {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  Nvs db;
  if (!db.open() || nvs_set_u8(db.handle, "reset", 1) != ESP_OK || nvs_commit(db.handle) != ESP_OK) return false;
  for (auto& p : peers) clear(p);
  return true;
}
bool finishReset() {
  Nvs db;
  return db.open() && nvs_erase_all(db.handle) == ESP_OK && nvs_commit(db.handle) == ESP_OK;
}
bool hasRecipients() { return authenticatedPeers.load(std::memory_order_relaxed) != 0; }
void publish(Bytes plain) {
  if (!hasRecipients()) return;
  if (plain.empty() || plain[0] != SS2K_SUCCESS) return;
  plain[0] = SS2K_SECURE_NOTIFICATION;
  if (plain.size() > maximumPlain) return;
  std::lock_guard<std::mutex> lock(publicationMutex);
  // Prefer recent measurements if a peer cannot drain a burst. Application
  // command replies and snapshots use their own ordered response path.
  while (!publications.empty() && (publications.size() >= 16 || publicationBytes + plain.size() > 2048)) {
    publicationBytes -= publications.front().packet->size();
    publications.pop_front();
  }
  publicationBytes += plain.size();
  publications.push_back({nextPublication++, std::make_shared<Bytes>(std::move(plain))});
}
}  // namespace PairedSettings
