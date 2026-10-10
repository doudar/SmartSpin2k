# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only

"""Exercise production BLE indication scheduling under congestion and handle reuse."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestBleTxScheduler(unittest.TestCase):
    def test_priority_ack_backpressure_expiry_and_reconnect(self):
        source = r'''
#include "BleTxScheduler.h"
#include <cassert>
using Tx = BleTxScheduler<2>;
int main() {
  Tx tx;
  uint64_t first = 1;
  auto connected = [&](uint16_t peer, uint64_t session) { return session == (peer == 1 ? first : 2); };
  std::vector<int> sent;
  bool pressure = false;
  auto send = [&](const Tx::Packet& packet) { if (pressure) return false; sent.push_back(packet.data[0]); return true; };
  auto add = [&](int value, Tx::Kind kind, uint16_t peer = 1, uint32_t now = 0) {
    assert(tx.enqueue({peer, peer == 1 ? first : 2, {uint8_t(value)}, kind, now}));
  };
  add(1, Tx::Kind::Log);
  add(2, Tx::Kind::Log); // Coalesce old diagnostics.
  add(3, Tx::Kind::Response);
  assert(tx.pump(250, true, connected, send));
  assert(sent.back() == 3);
  assert(!tx.pump(300, true, connected, send)); // Await ACK before another indication.
  add(4, Tx::Kind::Response, 2);
  assert(tx.pump(320, true, connected, send) && sent.back() == 4); // Other peer is independent.
  Tx::Kind kind;
  assert(!tx.complete(1, 99, kind));
  assert(tx.complete(1, 1, kind) && kind == Tx::Kind::Response);
  assert(!tx.pump(350, false, connected, send)); // Setup/scanning suppresses logs.
  assert(tx.pump(370, true, connected, send) && sent.back() == 2);
  assert(tx.complete(1, 1, kind) && kind == Tx::Kind::Log);
  add(5, Tx::Kind::Snapshot, 1, 400);
  pressure = true;
  assert(tx.pump(400, true, connected, send));
  assert(!tx.pump(401, true, connected, send)); // Failed sends are paced too.
  pressure = false;
  assert(tx.pump(420, true, connected, send) && sent.back() == 5);
  tx.cancelSnapshots();
  assert(tx.complete(1, 1, kind) && kind == Tx::Kind::Response); // Late ACK cannot advance a new snapshot.
  add(6, Tx::Kind::Log, 1, 430);
  assert(!tx.pump(1500, true, connected, send)); // Stale log expires.
  add(7, Tx::Kind::Response, 1, 1500);
  first = 3;
  assert(!tx.pump(1520, true, connected, send)); // Reused handle cannot receive old data.
  add(8, Tx::Kind::Response, 1, 1520);
  assert(tx.pump(1540, true, connected, send) && sent.back() == 8);
  add(9, Tx::Kind::Response, 1, 1550);
  assert(!tx.pump(7000, true, connected, send)); // Queued response deadline; busy remains until ACK.
  assert(tx.complete(1, 3, kind));
  assert(!tx.pump(7020, true, connected, send));
  // A failed peer must not starve another peer or reorder its own responses.
  Tx fair;
  assert(fair.enqueue({1, 3, {20}, Tx::Kind::Response, 0}));
  assert(fair.enqueue({1, 3, {21}, Tx::Kind::Response, 0}));
  assert(fair.enqueue({2, 2, {22}, Tx::Kind::Response, 0}));
  bool failFirst = true;
  auto fairSend = [&](const Tx::Packet& packet) {
    if (packet.peer == 1 && failFirst) return false;
    sent.push_back(packet.data[0]);
    return true;
  };
  assert(fair.pump(20, true, connected, fairSend));
  assert(fair.pump(40, true, connected, fairSend) && sent.back() == 22);
  failFirst = false;
  assert(fair.pump(60, true, connected, fairSend) && sent.back() == 20);
  assert(fair.complete(1, 3, kind));
  assert(fair.pump(80, true, connected, fairSend) && sent.back() == 21);
  for (unsigned i = 0; i < Tx::CAPACITY; ++i) add(10, Tx::Kind::Response, 1, 7100);
  assert(!tx.enqueue({1, 3, {11}, Tx::Kind::Response, 7100}));
  assert(!tx.enqueue({1, 3, {11}, Tx::Kind::Log, 7100}));
}
'''
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required")
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "scheduler.cpp"
            exe = Path(directory) / "scheduler"
            cpp.write_text(source)
            subprocess.run([compiler, "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "lib/SS2K/include"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
