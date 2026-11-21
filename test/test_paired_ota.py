"""Run the production authenticated OTA writer against bounded, in-memory ESP partitions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from protocol_headers import protocol_include

ROOT = Path(__file__).resolve().parents[1]


class TestPairedOta(unittest.TestCase):
    def test_owner_offsets_header_digest_and_activation(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path/'mbedtls').mkdir()
            (path/'mbedtls/sha256.h').write_text(r"""
#pragma once
#include <cstdint>
#include <cstring>
// The ESP supplies SHA-256. This deterministic test double records the exact
// stream and exercises finish/compare/error handling on the host.
struct mbedtls_sha256_context { uint8_t sum=0; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* h) { h->sum=0; }
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context*,int) { return 0; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* h,const uint8_t* p,size_t n) { while (n--) h->sum+=*p++; return 0; }
inline int mbedtls_sha256_finish(mbedtls_sha256_context* h,uint8_t* out) { memset(out,h->sum,32); return 0; }
""")
            (path/'esp_app_format.h').write_text(r"""
#pragma once
#include <cstdint>
enum esp_chip_id_t : uint16_t { CHIP=9, ESP_CHIP_ID_INVALID=0xffff };
struct esp_image_header_t { uint8_t magic,segment_count; esp_chip_id_t chip_id; uint8_t reserved[20]; };
constexpr unsigned ESP_IMAGE_HEADER_MAGIC=0xe9, ESP_IMAGE_MAX_SEGMENTS=16;
""")
            (path/'sdkconfig.h').write_text('#define CONFIG_IDF_FIRMWARE_CHIP_ID 9\n')
            (path/'Main.h').write_text(r"""
#pragma once
#include <cstdint>
struct Device { bool isUpdating=false,rebootFlag=false,resetDefaultsFlag=false; };
extern Device* ss2k;
uint32_t millis();
""")
            (path/'esp_ota_ops.h').write_text(r"""
#pragma once
#include <cstdint>
#include <cstddef>
struct esp_partition_t { uint32_t size; };
using esp_ota_handle_t = unsigned;
constexpr int ESP_OK=0, OTA_WITH_SEQUENTIAL_WRITES=-1;
const esp_partition_t* esp_ota_get_next_update_partition(void*);
int esp_ota_begin(const esp_partition_t*, int, esp_ota_handle_t*);
int esp_ota_write(esp_ota_handle_t,const void*,size_t);
int esp_ota_end(esp_ota_handle_t);
int esp_ota_abort(esp_ota_handle_t);
int esp_ota_set_boot_partition(const esp_partition_t*);
""")
            cpp = path/'test.cpp'
            cpp.write_text(r"""
#include "SettingsUpdate.h"
#include "Main.h"
#include "esp_ota_ops.h"
#include "ss2k_protocol.h"
#include <algorithm>
#include <cassert>
#include <cstring>
using Bytes=std::vector<uint8_t>;
Device device; Device* ss2k=&device;
esp_partition_t partition{1024}; Bytes flash;
bool live=false, booted=false;
uint32_t tickCount=0;
uint32_t millis() { return tickCount; }
const esp_partition_t* esp_ota_get_next_update_partition(void*) { return &partition; }
int esp_ota_begin(const esp_partition_t*,int,esp_ota_handle_t* h){assert(!live);live=true;*h=1;flash.clear();return 0;}
int esp_ota_write(esp_ota_handle_t,const void* data,size_t n){assert(live);auto p=static_cast<const uint8_t*>(data);flash.insert(flash.end(),p,p+n);return 0;}
int esp_ota_end(esp_ota_handle_t){assert(live);live=false;return 0;}
int esp_ota_abort(esp_ota_handle_t){assert(live);live=false;return 0;}
int esp_ota_set_boot_partition(const esp_partition_t*){assert(!live);booted=true;return 0;}
Bytes command(Bytes p,int who=7) {return SettingsUpdate::command(who,p.data(),p.size());}
Bytes image(95,0x21);
Bytes begin() {
  Bytes packet(39);packet[0]=2;packet[1]=SS2K_ID_FIRMWARE_STREAM;packet[2]=SS2K_OTA_BEGIN;
  packet[3]=image.size();uint8_t sum=0;for(auto b:image)sum+=b;std::fill(packet.begin()+7,packet.end(),sum);return packet;
}
Bytes chunk(size_t start,size_t n) {
  Bytes c{2,SS2K_ID_FIRMWARE_STREAM,SS2K_OTA_CHUNK,uint8_t(start),0,0,0};
  c.insert(c.end(),image.begin()+start,image.begin()+start+n);return c;
}
int main() {
 image[0]=0xe9;image[1]=2;image[2]=9;image[3]=0;
 const Bytes finish{2,SS2K_ID_FIRMWARE_STREAM,SS2K_OTA_FINISH};
 assert(command(begin())[0]==128 && live && device.isUpdating);
 assert(command(chunk(0,4),8)[0]==255 && flash.empty() && live);
 assert(command(chunk(0,4))[3]==4 && flash.empty());
 assert(command(chunk(0,4))[0]==255 && live);
 assert(command(chunk(4,45))[3]==49 && flash.size()==49);
 assert(command(chunk(49,46))[3]==95 && flash==image);
 assert(command(finish)[0]==128 && booted && device.rebootFlag && !device.isUpdating && !live);
 device.rebootFlag=false;booted=false;
 Bytes bad=begin();bad[7]^=1;
 assert(command(bad)[0]==128);
 assert(command(chunk(0,95))[0]==128);
 assert(command(finish)[0]==255 && !booted && !live);
 assert(command(begin())[0]==128);
 Bytes wrong=chunk(0,24);wrong[7+2]=3;
 assert(command(wrong)[0]==255 && !live && flash.empty() && !booted);
 assert(command(begin())[0]==128);
 SettingsUpdate::disconnect(8);assert(live);
 SettingsUpdate::disconnect(7);assert(!live && !device.isUpdating && !booted);
 assert(command(begin())[0]==128);
 tickCount += 30001;SettingsUpdate::tick();assert(!live && !booted && !device.isUpdating);
 assert(command(begin())[0]==128);assert(command(finish)[0]==255 && !live && !booted);
 assert(command(begin())[0]==128);
 assert(command({2,SS2K_ID_FIRMWARE_STREAM,SS2K_OTA_ABORT})[0]==128 && !live && !booted);
}
""")
            compiler=shutil.which('g++')
            self.assertIsNotNone(compiler)
            includes=[path,ROOT/'include',protocol_include(ROOT),ROOT/'lib/SS2K/include',ROOT/'lib/ArduinoCompat/include']
            (path/'endian.c').write_bytes((ROOT/'lib/ArduinoCompat/src/os/endian.c').read_bytes())
            obj=path/'endian.o'
            subprocess.run([compiler,'-x','c','-I'+str(includes[-1]),'-c',str(path/'endian.c'),'-o',str(obj)],check=True)
            exe=path/'update.exe'
            subprocess.run([compiler,'-std=c++17',*['-I'+str(p) for p in includes],str(ROOT/'src/SettingsUpdate.cpp'),str(cpp),str(obj),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
