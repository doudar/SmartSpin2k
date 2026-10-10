# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only

"""Native multipart/query parsing and cross-transport OTA ownership regressions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_tmc_recovery import function

ROOT = Path(__file__).resolve().parents[1]

class TestNativeHttp(unittest.TestCase):
    def test_streaming_uploads_and_queries(self):
        source = (ROOT / 'src/NativeHttpServer.cpp').read_text()
        receive = function(source, 'bool NativeHttpServer::receiveUpload(')
        harness = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "HttpRequestParsing.h"
#include "FirmwareUpdateOwner.h"
constexpr int ESP_OK=0;
constexpr size_t BUFFER_SIZE=2048;
struct FakeRequest { size_t content_len=0, cursor=0, chunk=2048; std::string input, type="multipart/form-data; boundary=example"; };
int httpd_req_get_hdr_value_str(FakeRequest* req, const char*, char* out, size_t size) {
 if(req->type.size()>=size) return -1;
 std::strcpy(out,req->type.c_str()); return 0;
}
int httpd_req_recv(FakeRequest* req, char* out, size_t size) {
 auto n=std::min({size,req->chunk,req->input.size()-req->cursor});
 std::memcpy(out,req->input.data()+req->cursor,n);req->cursor+=n;return n;
}
struct NativeHttpServer {
 enum {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
 struct Upload { int status=0; std::string filename; uint8_t* buf=nullptr;size_t currentSize=0,totalSize=0; };
 struct Request { FakeRequest* req; int status=200;std::string body;Upload upload;bool rejected=false,ownsUpload=false; };
 bool stopping=false, rejectStart=false, rejectWrite=false;
 unsigned starts=0,ends=0,aborts=0;
 std::string received;
 bool dispatch(Request& req,bool) {
  if(req.upload.status==UPLOAD_FILE_START){++starts;if(rejectStart)req.rejected=true;else req.ownsUpload=true;}
  if(req.upload.status==UPLOAD_FILE_WRITE){assert(req.upload.currentSize<=2048); received.append((char*)req.upload.buf,req.upload.currentSize);if(rejectWrite)req.rejected=true;}
  if(req.upload.status==UPLOAD_FILE_END)++ends;
  if(req.upload.status==UPLOAD_FILE_ABORTED)++aborts;
  return true;
 }
 bool receiveUpload(Request& request);
};
METHOD
std::string multipart(const std::string& bytes) {
 return "--example\r\nContent-Disposition: form-data; name=\"update\"; filename=\"firmware.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n"+bytes+"\r\n--example--\r\n";
}
int main() {
 std::string bytes;for(int i=0;i<9000;++i)bytes+=char(i%256);
 bytes+="\r\n--exampleXbinary";
 for(size_t chunk:{1u,2u,7u,17u,2048u}) {
  FakeRequest req;req.input=multipart(bytes);req.content_len=req.input.size();req.chunk=chunk;
  NativeHttpServer server;NativeHttpServer::Request request{&req};
  assert(server.receiveUpload(request));assert(server.received==bytes);
  assert(server.starts==1 && server.ends==1 && server.aborts==0);
  assert(request.upload.totalSize==bytes.size());
 }
 for(int mode=0;mode<7;++mode) {
  FakeRequest req;req.input=multipart(bytes);req.content_len=req.input.size();
  NativeHttpServer server;NativeHttpServer::Request request{&req};
  if(mode==0)req.input.resize(req.input.size()-7); // Truncated connection.
  if(mode==1)req.input.replace(req.input.size()-4,4,"\r\nXX"); // Second part, no final boundary.
  if(mode==2)req.type="application/octet-stream";
  if(mode==3)server.rejectStart=true; // Another transport owns OTA; do not abort it.
  if(mode==4)server.rejectWrite=true;
  if(mode==5){req.input="--example\r\n"+std::string(3000,'x');req.content_len=req.input.size();}
  if(mode==6){req.type="multipart/form-data; boundary="+std::string(71,'x');}
  assert(!server.receiveUpload(request));assert(server.ends==0);
  if(mode==0||mode==1||mode==4)assert(server.aborts==1);
  if(mode==3)assert(server.aborts==0);
 }
 std::vector<std::pair<std::string,std::string>> args;
 assert(HttpRequestParsing::query("ssid=My+Bike&password=a%26b%3Dc&gearTeeth=%5B5034%5D",args));
 assert(args[0].second=="My Bike" && args[1].second=="a&b=c" && args[2].second=="[5034]");
 for(const auto* q:{"x=%", "x=%zz", "x=%00", "x=%0d"}){args.clear();assert(!HttpRequestParsing::query(q,args));}
 using namespace FirmwareUpdateOwner;
 assert(acquire(Owner::Web));assert(!acquire(Owner::Ble));
 release(Owner::Ble);assert(current==Owner::Web);
 release(Owner::Web);assert(acquire(Owner::Ble));release(Owner::Ble);
}
'''.replace('METHOD', receive)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'native_http.cpp';exe=Path(directory)/'native_http'
            cpp.write_text(harness)
            subprocess.run([shutil.which('g++'),'-std=c++17','-I',str(ROOT/'lib/SS2K/include'),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_maintenance_handoff_and_cancellation(self):
        source=(ROOT/'src/NativeHttpServer.cpp').read_text()
        methods='\n'.join(function(source, signature) for signature in (
            'bool NativeHttpServer::dispatch(', 'void NativeHttpServer::processPending('))
        harness=r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
using TickType_t=uint32_t;
constexpr int pdTRUE=1;
#define pdMS_TO_TICKS(x) (x)
uint32_t clockMs=0;
uint32_t xTaskGetTickCount(){return clockMs;}
struct Queue{void* value=nullptr;};
struct Semaphore{bool ready=false;};
std::function<void()> onWait;
int xQueueSend(Queue* q,void* value,int){if(q->value)return 0;q->value=*(void**)value;return 1;}
int xQueueReceive(Queue* q,void* out,int){if(!q->value)return 0;*(void**)out=q->value;q->value=nullptr;return 1;}
int xSemaphoreTake(Semaphore* s,int ticks){clockMs+=ticks;if(onWait)onWait();if(!s->ready)return 0;s->ready=false;return 1;}
void xSemaphoreGive(Semaphore* s){s->ready=true;}
struct Controller{bool isUpdating=false,rebootFlag=false;} controller;
auto* ss2k=&controller;
struct NativeHttpServer {
 struct Route{std::function<void()> handler,upload;};
 struct Request{bool uploadEvent=false,completedUpload=false;Route* route=nullptr;Semaphore sem;Semaphore* done=&sem;int status=200;};
 Queue queue;Queue* pending=&queue;
 bool stopping=false;
 std::atomic<bool> rebootPending{false};
 Request* active=nullptr;
 std::function<void()> notFound=[]{};
 void send(int status,const char*,const char*){assert(active);active->status=status;}
 bool dispatch(Request&,bool uploadEvent=false);
 void processPending();
};
METHODS
int main(){
 NativeHttpServer server;NativeHttpServer::Route route;
 unsigned calls=0;route.handler=[&]{++calls;};route.upload=[&]{++calls;};
 NativeHttpServer::Request request;request.route=&route;
 onWait=[&]{server.processPending();};
 assert(server.dispatch(request));assert(calls==1 && !server.active);
 controller.isUpdating=true;
 assert(server.dispatch(request));assert(calls==1 && request.status==503);
 request.completedUpload=true;
 assert(server.dispatch(request));assert(calls==2);
 assert(server.dispatch(request,true));assert(calls==3);
 onWait={};request.completedUpload=false;
 assert(!server.dispatch(request));assert(!server.queue.value); // Unclaimed timeout cancels safely.
 server.processPending();assert(calls==3); // Canceled work never executes later.
 onWait=[&]{server.stopping=true;};
 assert(!server.dispatch(request));assert(!server.queue.value);
 server.rebootPending=true;server.processPending();assert(controller.rebootFlag);
}
'''.replace('METHODS',methods)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'handoff.cpp';exe=Path(directory)/'handoff'
            cpp.write_text(harness)
            subprocess.run([shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
