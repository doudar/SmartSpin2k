/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Native-only GATT API facade. This models sessions and subscriptions, not RF/HCI.
#include <deque>
#include <queue>
#include <stdexcept>
#include <memory>
#include <cstring>
#include <vector>
#include <Constants.h>
#include <sensors/SensorDataFactory.h>
#include <sensors/CyclePowerData.h>
using BLEUUID = NimBLEUUID;
constexpr int BLE_HS_CONN_HANDLE_NONE = 65535;
constexpr int pdTRUE = 1, pdFALSE = 0;
constexpr int HEX = 16;
#ifndef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 7
#endif
#define NOTIFY_DATA_QUEUE_SIZE 25
#define BLE_CLIENT_LOG_TAG "BLE_Client"
#define BLE_COMMON_LOG_TAG "BLE_Common"
#ifndef SS2K_LOGD
#define SS2K_LOGD(...) ((void)0)
#endif
#ifndef SS2K_LOGE
#define SS2K_LOGE SS2K_LOG
#endif
struct VirtualQueue { size_t capacity, size; std::deque<std::vector<unsigned char>> values; };
using QueueHandle_t = VirtualQueue*;
inline QueueHandle_t xQueueCreate(size_t cap,size_t size) { return new VirtualQueue{cap,size,{}}; }
inline size_t uxQueueSpacesAvailable(QueueHandle_t q) { return q ? q->capacity-q->values.size() : 0; }
inline int xQueueSendToBack(QueueHandle_t q,const void* p,int) { if(!uxQueueSpacesAvailable(q))return pdFALSE; const auto* b=static_cast<const unsigned char*>(p); q->values.emplace_back(b,b+q->size); return pdTRUE; }
inline int xQueueReceive(QueueHandle_t q,void* p,int) { if(!q||q->values.empty())return pdFALSE; std::memcpy(p,q->values.front().data(),q->size);q->values.pop_front();return pdTRUE; }
inline void xQueueReset(QueueHandle_t q) { if(q)q->values.clear(); }
struct NimBLEAddress { std::string value; NimBLEAddress(std::string s=""):value(s){} bool operator==(const NimBLEAddress& a)const{return value==a.value;} bool operator!=(const NimBLEAddress&a)const{return !(*this==a);} std::string toString()const{return value;} };
struct NimBLEAdvertisedDevice { NimBLEAddress address{"c0:00:00:00:00:01"}; NimBLEAddress getAddress()const{return address;} };
struct NimBLEClient;
struct NimBLERemoteService;
struct NimBLERemoteCharacteristic {
  NimBLEUUID uuid; NimBLERemoteService* service=nullptr;
  using Callback=void(*)(NimBLERemoteCharacteristic*,uint8_t*,size_t,bool);
  Callback callback=nullptr; bool subscribed=false;
  NimBLERemoteCharacteristic(uint16_t id):uuid(id){}
  NimBLEUUID getUUID()const{return uuid;} int getHandle()const{return 1;}
  bool canNotify()const{return true;} bool canIndicate()const{return false;}
  bool subscribe(bool,Callback cb){callback=cb;subscribed=true;return true;}
  NimBLERemoteService* getRemoteService(){return service;}
  NimBLEClient* getClient();
};
struct NimBLERemoteService {
  NimBLEUUID uuid; NimBLEClient* client=nullptr; std::vector<NimBLERemoteCharacteristic*> chars;
  NimBLERemoteService(uint16_t id):uuid(id){}
  NimBLEUUID getUUID()const{return uuid;}
  NimBLERemoteCharacteristic* getCharacteristic(const NimBLEUUID&id){for(auto*c:chars)if(c->uuid==id)return c;return nullptr;}
};
struct NimBLEClient {
  bool connected=false; NimBLEAddress address{"c0:00:00:00:00:01"}; std::vector<NimBLERemoteService*> services;
  bool isConnected()const{return connected;} NimBLEAddress getPeerAddress()const{return address;}
  int getConnHandle()const{return 1;} void disconnect();
  NimBLERemoteService* getService(const NimBLEUUID&id){for(auto*s:services)if(s->uuid==id)return s;return nullptr;}
  const std::vector<NimBLERemoteService*>& getServices(bool){return services;}
};
inline NimBLEClient* NimBLERemoteCharacteristic::getClient(){return service->client;}
using BLEClient=NimBLEClient;
struct VirtualAdvertising {bool isAdvertising()const{return true;}};
struct NimBLEDevice {
  static NimBLEClient client;
  static NimBLEClient* getClientByPeerAddress(const NimBLEAddress&a){return a==client.address?&client:nullptr;}
  static NimBLEClient* getClientByHandle(int h){return h==1?&client:nullptr;}
  static VirtualAdvertising* getAdvertising(){return nullptr;}
  static std::vector<NimBLEClient*> getConnectedClients(){return client.connected?std::vector<NimBLEClient*>{&client}:std::vector<NimBLEClient*>{};}
  static void startAdvertising(){}
};
using BLEDevice=NimBLEDevice;
struct VirtualSerial {template<class T>void print(T,int=0){} }; static VirtualSerial Serial;
struct NotifyData { NimBLEUUID serviceUUID; NimBLEUUID charUUID; uint8_t data[NOTIFY_DATA_QUEUE_SIZE];size_t length; };
struct SpinBLEAdvertisedDevice {
  QueueHandle_t dataBufferQueue=nullptr; std::shared_ptr<const NimBLEAdvertisedDevice> advertisedDevice;
  NimBLEAddress peerAddress; std::string uniqueName; int connectedClientID=BLE_HS_CONN_HANDLE_NONE;
  BLEUUID serviceUUID{uint16_t(0)},charUUID{uint16_t(0)}; Measurement batt;
  bool isHRM=false,isPM=false,isCSC=false,isCT=false,isRemote=false,isGrupetto=false,doConnect=false,isPostConnected=false;
  unsigned long lastDataUpdateTime=0;
  ~SpinBLEAdvertisedDevice(){delete dataBufferQueue;}
  auto getAdvertisement()const{return advertisedDevice;}
  void storeAdvertisement(std::shared_ptr<const NimBLEAdvertisedDevice> a){advertisedDevice=std::move(a);}
  void clearState(bool);void reset(bool=true);
  void set(const NimBLEAdvertisedDevice*,int=BLE_HS_CONN_HANDLE_NONE,BLEUUID=uint16_t(0),BLEUUID=uint16_t(0));
  bool enqueueData(uint8_t*,size_t,NimBLEUUID,NimBLEUUID);NotifyData dequeueData();
};
struct SpinBLEClient {
  bool connectedPM=false,connectedHRM=false,connectedCD=false,connectedCT=false,connectedSpeed=false,connectedRemote=false;
  SpinBLEAdvertisedDevice myBLEDevices[NUM_BLE_DEVICES];
  String adevName2UniqueName(const NimBLEAdvertisedDevice*){return "Virtual bike 01";}
  void keepAliveBLE_HID(NimBLEClient*){}
  void FTMSControlPointWrite(const uint8_t*,int){}
};
struct SpinBLEServer {std::queue<std::string> writeCache;int spinDownFlag=0;void update(){}int connectedClientCount(){return 0;}};
struct MyClientCallback {void onDisconnect(NimBLEClient*,int);};
struct BLEServiceInfo {BLEUUID serviceUUID;BLEUUID characteristicUUID;String name;};
namespace BLEServices {extern const std::vector<BLEServiceInfo> SUPPORTED_SERVICES;}
static SpinBLEClient spinBLEClient;
static SpinBLEServer spinBLEServer;
NimBLEClient NimBLEDevice::client;
inline bool bleDeviceIdentifierEquals(const char*a,const char*b){return std::strcmp(a,b)==0;}
void initializeVirtualBle(const std::string& profile="all");
bool injectVirtualBle(const std::string&,const std::vector<uint8_t>&);
void pollVirtualBle();
void disconnectVirtualBle();
