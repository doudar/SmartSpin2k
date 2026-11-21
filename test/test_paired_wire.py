"""The firmware must return large authenticated records on an actual 20-byte ATT payload."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from protocol_headers import protocol_include
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestPairedWire(unittest.TestCase):
    def test_negotiated_minimum_mtu_paces_complete_ordered_records(self):
        source = (ROOT/'src/PairedSettings.cpp').read_text()
        harness = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "ss2k_secure.h"
using Bytes = std::vector<uint8_t>;
struct Peer {
 uint16_t id=7;uint64_t generation=2;size_t capacity=20,sent=0;
 uint8_t sendIndex=0;uint32_t lastSend=0;
 std::unique_ptr<std::vector<Bytes>> outbound;
};
uint32_t now=1;
uint32_t millis(){return now;}
bool alive=true, blocked=true;
bool isAlive(uint16_t id,uint64_t generation){return alive && id==7 && generation==2;}
std::vector<Bytes> frames;
bool emitFrame(uint16_t peer,uint64_t generation,const uint8_t* b,size_t n){
 assert(n<=20 && isAlive(peer,generation));
 if(blocked)return false;
 frames.emplace_back(b,b+n);return true;
}
void clear(Peer& p){p.outbound.reset();p.sent=p.sendIndex=0;}
@SEND@
@FLUSH@
int main() {
 Peer peer;Bytes record(525,55),response;record[0]=SS2K_SECURE_DATA;
 Bytes ack{SS2K_SECURE_READY,7};
 assert(send(peer,record,nullptr));
 flush(peer);assert(peer.sent==0 && peer.sendIndex==0 && frames.empty());
 assert(send(peer,ack,&response));assert(response.empty());
 blocked=false;
 for(int i=0;peer.outbound;++i){
   auto previous=frames.size();++now;flush(peer);assert(frames.size()==previous+1 && i<40);
 }
 ss2k_fragment_state state{};uint8_t storage[SS2K_SECURE_MAX_WIRE];
 std::vector<Bytes> decoded;
 for(const auto& frame:frames){
  const uint8_t* ptr=nullptr;size_t size=0;
  auto result=ss2k_secure_accept(&state,storage,sizeof(storage),frame.data(),frame.size(),&ptr,&size);
  assert(result!=SS2K_FRAME_INVALID);
  if(result==SS2K_FRAME_COMPLETE)decoded.emplace_back(ptr,ptr+size);
 }
 assert(decoded==std::vector<Bytes>({record,ack}));assert(frames.size()==32);
 alive=false;assert(!send(peer,record,nullptr));assert(!peer.outbound);
 alive=true;assert(send(peer,record,nullptr));blocked=true;now+=5001;flush(peer);assert(!peer.outbound);
}
""".replace('@SEND@',function(source,'bool send(Peer&')).replace('@FLUSH@',function(source,'void flush(Peer&'))
        with tempfile.TemporaryDirectory() as temp:
            cpp,exe=Path(temp)/'wire.cpp',Path(temp)/'wire.exe'
            cpp.write_text(harness)
            compiler=shutil.which('g++')
            self.assertIsNotNone(compiler)
            subprocess.run([compiler,'-std=c++17','-I'+str(protocol_include(ROOT)),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
