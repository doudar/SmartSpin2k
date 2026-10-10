"""Exercise the production recovery downloader with HTTP and filesystem failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]


class TestHttpDownload(unittest.TestCase):
    def test_streaming_and_failure_cleanup(self):
        method = function((ROOT / 'src/HTTP_Server_Basic.cpp').read_text(), 'bool downloadWebFile(')
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
using String = std::string;
using esp_err_t = int;
constexpr int ESP_OK = 0, FILE_WRITE = 1;
const char* rootCACertificate = "test root";
#define SS2K_LOGE(...) ((void)0)
struct Scenario {
  bool initFails=false, fileFails=false, shortWrite=false, complete=true;
  int openResult=0, status=200, terminalRead=0;
  int64_t length=9000;
  size_t cursor=0, fragment=713;
  unsigned cleanups=0, opens=0, closes=0;
  std::string body=std::string(9000, 'x');
} scenario;
struct File {
  std::string* bytes;
  explicit operator bool() const { return bytes != nullptr; }
  size_t write(const uint8_t* data, size_t size) {
    if(scenario.shortWrite) --size;
    bytes->append(reinterpret_cast<const char*>(data), size);
    return size;
  }
  void close() { ++scenario.closes; }
};
struct Filesystem {
  std::map<std::string,std::string> files;
  void remove(const std::string& path) { files.erase(path); }
  File open(const std::string& path, int, bool) {
    ++scenario.opens;
    return {scenario.fileFails ? nullptr : &files[path]};
  }
} LittleFS;
struct esp_http_client_config_t {
  const char *url=nullptr, *cert_pem=nullptr;
  int timeout_ms=0, buffer_size=0;
  bool disable_auto_redirect=false;
};
using esp_http_client_handle_t = Scenario*;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
  assert(std::string(config->url)=="https://example.test/asset");
  assert(config->cert_pem==rootCACertificate && config->timeout_ms>0);
  assert(config->buffer_size<=2048 && config->disable_auto_redirect);
  return scenario.initFails ? nullptr : &scenario;
}
int esp_http_client_open(Scenario*, int n) { assert(n==0); return scenario.openResult; }
int64_t esp_http_client_fetch_headers(Scenario*) { return scenario.length; }
int esp_http_client_get_status_code(Scenario*) { return scenario.status; }
int esp_http_client_read(Scenario*, char* buffer, int capacity) {
  assert(capacity<=2048);
  size_t n=std::min({size_t(capacity), scenario.fragment, scenario.body.size()-scenario.cursor});
  if(!n) return scenario.terminalRead;
  std::memcpy(buffer,scenario.body.data()+scenario.cursor,n);
  scenario.cursor+=n;
  return int(n);
}
bool esp_http_client_is_complete_data_received(Scenario*) { return scenario.complete; }
void esp_http_client_cleanup(Scenario*) { ++scenario.cleanups; }
METHOD
int main() {
  for(int mode=0;mode<12;++mode) {
    scenario=Scenario{};
    LittleFS.files={{"/asset.download","stale"},{"/existing","preserve"}};
    if(mode==1) scenario.length=0; // Chunked response has no declared length.
    if(mode==2) {scenario.length=0;scenario.body.clear();}
    if(mode==3) scenario.initFails=true;
    if(mode==4) scenario.openResult=-1;
    if(mode==5) scenario.length=-1;
    if(mode==6) scenario.status=404;
    if(mode==7) scenario.status=302;
    if(mode==8) scenario.fileFails=true;
    if(mode==9) scenario.shortWrite=true;
    if(mode==10) scenario.complete=false; // EOF before full body/final chunk.
    if(mode==11) scenario.terminalRead=-1; // Timeout after some bytes arrive.
    const bool success=downloadWebFile("https://example.test/asset","/asset.download");
    assert(success==(mode<3));
    assert(scenario.cleanups==(mode==3 ? 0u : 1u));
    assert(LittleFS.files.at("/existing")=="preserve");
    if(success) assert(LittleFS.files.at("/asset.download")==scenario.body);
    else assert(!LittleFS.files.count("/asset.download"));
    if(mode>=4 && mode<=7) assert(scenario.opens==0);
    if(mode==9) assert(scenario.cursor==scenario.fragment); // Stop on first short write.
    if(mode<3 || mode>=9) assert(scenario.closes==1);
  }
}
'''.replace('METHOD', method)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'download.cpp'
            exe = Path(directory) / 'download'
            cpp.write_text(harness)
            subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
