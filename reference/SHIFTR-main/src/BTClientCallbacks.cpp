#include <BTClientCallbacks.h>
#include <BTDeviceManager.h>

void BTClientCallbacks::onConnect(NimBLEClient* pClient) {
  BTDeviceManager::connected = true;
};

void BTClientCallbacks::onDisconnect(NimBLEClient* pClient, int reason) {
  log_e("Disconnected from %s, reason %d", pClient->getPeerAddress().toString().c_str(), reason);
  BTDeviceManager::connected = false;
  BTDeviceManager::connectedDeviceName = "";
  BTDeviceManager::startScan();
};

bool BTClientCallbacks::onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) {
  return true;
}
