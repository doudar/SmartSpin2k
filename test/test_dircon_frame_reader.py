"""Exercise bounded production TCP reads for normal settings and encrypted updates."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from protocol_headers import protocol_include

ROOT = Path(__file__).resolve().parents[1]


class TestDirConFrameReader(unittest.TestCase):
    def test_segmented_coalesced_frames_and_ota_ownership(self):
        program = r"""
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>
#include "DirConFrameReader.h"
#include "ss2k_secure.h"
using Reader = DirConFrameReader<256>;
using Bytes = std::vector<uint8_t>;
struct Socket {
  Bytes bytes;
  size_t pos=0, segment=3000, calls=0;
  bool available() const { return pos<bytes.size(); }
  int read(uint8_t* out, size_t n) {
    n=std::min({n, segment, bytes.size()-pos});
    std::memcpy(out,bytes.data()+pos,n);
    ++calls; pos+=n; return n;
  }
};
int main() {
  constexpr size_t limit=22+SS2K_DIRCON_OTA_MAX_WIRE;
  Bytes large(limit,0x71);
  large[0]=1;large[1]=4;large[4]=(limit-6)>>8;large[5]=(limit-6)&255;
  Bytes small{1,1,0,0,0,0};
  for(size_t transportSegment:{1,5,129,1460,3000}) {
    Reader reader;
    Socket socket{large,0,transportSegment};
    socket.bytes.insert(socket.bytes.end(),small.begin(),small.end());
    while(reader.read(socket,limit)==Reader::Pending) {}
    assert(reader.size()==limit && !std::memcmp(reader.data(),large.data(),limit));
    if(transportSegment>=limit) assert(socket.calls==2);
    reader.consume();
    while(reader.read(socket,256)==Reader::Pending) {}
    assert(reader.size()==small.size() && socket.pos==socket.bytes.size());
    assert(!std::memcmp(reader.data(),small.data(),small.size()));
  }
  Reader reader;
  Socket unauthorized{large};
  assert(reader.read(unauthorized,256)==Reader::Invalid && unauthorized.pos==6);
  reader.reset();
  Socket oversize{large};
  ++oversize.bytes[5];
  assert(reader.read(oversize,limit)==Reader::Invalid && oversize.pos==6);
  reader.reset();
  Socket timeout{large,0,128};
  assert(reader.read(timeout,limit)==Reader::Pending && timeout.pos==134);
  assert(reader.read(timeout,256)==Reader::Invalid);
  reader.reset();
  Socket next{small};
  assert(reader.read(next,256)==Reader::Complete);
}
"""
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source = folder / 'reader.cpp'
            source.write_text(program)
            includes = [ROOT / 'include', ROOT / 'lib/SS2K/include',
                        ROOT / 'lib/ArduinoCompat/include', protocol_include(ROOT)]
            flags = ['-I' + str(path) for path in includes]
            compiler = shutil.which('g++')
            self.assertIsNotNone(compiler)
            endian = folder / 'endian.o'
            subprocess.run([compiler, '-x', 'c', *flags, '-c', str(ROOT / 'lib/ArduinoCompat/src/os/endian.c'),
                            '-o', str(endian)], check=True)
            executable = folder / 'reader.exe'
            subprocess.run([compiler, '-std=c++17', *flags, str(source), str(endian), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
