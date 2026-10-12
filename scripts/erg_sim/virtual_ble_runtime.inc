/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */


void NimBLEClient::disconnect() {
  if(!connected)return;
  connected=false;
  for(auto*s:services)for(auto*c:s->chars)c->subscribed=false;
  MyClientCallback().onDisconnect(this,0);
}
void initializeVirtualBle(const std::string& profile) {
  static NimBLERemoteService cps(0x1818), csc(0x1816),ftms(0x1826);
  static NimBLERemoteCharacteristic power(0x2a63),cadence(0x2a5b),indoor(0x2ad2);
  auto& client=NimBLEDevice::client;
  client.disconnect();
  cps.chars={&power};csc.chars={&cadence};ftms.chars={&indoor};
  if(profile=="cps_csc")client.services={&cps,&csc};
  else if(profile=="cps")client.services={&cps};
  else if(profile=="ftms")client.services={&ftms};
  else if(profile=="all")client.services={&cps,&csc,&ftms};
  else throw std::invalid_argument("Unsupported virtual BLE profile");
  for(auto*s:client.services){s->client=&client;for(auto*c:s->chars){c->service=s;c->callback=nullptr;c->subscribed=false;}}
  client.connected=true;
  NimBLEAdvertisedDevice advertisement;
  auto* primary=client.services.front();
  spinBLEClient.myBLEDevices[0].set(&advertisement,1,primary->uuid,primary->chars.front()->uuid);
  spinBLEClient.myBLEDevices[0].isPostConnected=subscribeToAllNotifications(&client);
}
struct VirtualBleStats {
  uint64_t attempted=0, accepted=0, rejected=0, queueDropped=0, processed=0, bytes=0;
} virtualBleStats;
// Legacy firmware decoders assume valid field lengths. The test link rejects
// malformed frames rather than allowing the host process to read out of bounds.
static bool validVirtualPacket(const NimBLEUUID&id,const std::vector<uint8_t>&p){
  if(p.size()>NOTIFY_DATA_QUEUE_SIZE)return false;
  if(id==CYCLINGPOWERMEASUREMENT_UUID){
    if(p.size()<4)return false;
    unsigned f=p[0]|(unsigned(p[1])<<8);size_t n=4;
    const unsigned bits[]={0,2,4,5,6,7,8,9,10,11};
    const size_t sizes[]={1,2,6,4,4,4,3,2,2,2};
    for(size_t i=0;i<10;i++)if(f&(1u<<bits[i]))n+=sizes[i];
    return p.size()==n;
  }
  if(id==CSCMEASUREMENT_UUID){
    if(p.empty()||(p[0]&~3))return false;
    return p.size()==size_t(1+((p[0]&1)?6:0)+((p[0]&2)?4:0));
  }
  if(id==FITNESSMACHINEINDOORBIKEDATA_UUID){
    if(p.size()<2)return false;
    unsigned f=p[0]|(unsigned(p[1])<<8);size_t n=(f&1)?2:4;
    const size_t sizes[]={2,2,2,3,2,2,2,5,1,1,2,2};
    for(size_t i=0;i<12;i++)if(f&(1u<<(i+1)))n+=sizes[i];
    return p.size()==n;
  }
  return false;
}
bool injectVirtualBle(const std::string& uuid,const std::vector<uint8_t>& packet) {
  auto& client=NimBLEDevice::client;
  ++virtualBleStats.attempted;
  NimBLEUUID id(uuid);
  if(!client.connected||!validVirtualPacket(id,packet)){++virtualBleStats.rejected;return false;}
  for(auto*s:client.services)for(auto*c:s->chars)if(c->uuid==id&&c->subscribed&&c->callback){
    // Characteristic data remains bytes until the production sensor decoder.
    auto bytes=packet;
    const bool space=uxQueueSpacesAvailable(spinBLEClient.myBLEDevices[0].dataBufferQueue)>0;
    c->callback(c,bytes.data(),bytes.size(),true);
    if(space){++virtualBleStats.accepted;virtualBleStats.bytes+=bytes.size();}else ++virtualBleStats.queueDropped;
    return space;
  }
  ++virtualBleStats.rejected;return false;
}
void pollVirtualBle(){
  auto*q=spinBLEClient.myBLEDevices[0].dataBufferQueue;
  const size_t before=q?q->values.size():0;
  const bool eligible=NimBLEDevice::client.connected && (!spinBLEClient.myBLEDevices[0].lastDataUpdateTime ||
    millis()-spinBLEClient.myBLEDevices[0].lastDataUpdateTime<=BLE_CLIENT_DISCONNECT_TIMEOUT);
  BLECommunications();
  if(eligible&&q)virtualBleStats.processed+=before-q->values.size();
}
void disconnectVirtualBle(){NimBLEDevice::client.disconnect();}

void virtualBleConnect(const std::string& profile){initializeVirtualBle(profile);}
bool virtualBleNotify(uint16_t uuid,const std::vector<uint8_t>&packet){char id[5];std::snprintf(id,sizeof(id),"%04x",static_cast<unsigned>(uuid));return injectVirtualBle(id,packet);}
