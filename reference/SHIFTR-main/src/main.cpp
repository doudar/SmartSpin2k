#include <Arduino.h>
#include <AsyncTCP.h>
#include <BTDeviceManager.h>
#include <Config.h>
#include <DirConManager.h>
#include <ESPmDNS.h>
#include <ETH.h>
#include <IotWebConf.h>
#include <IotWebConfESP32HTTPUpdateServer.h>
#include <IotWebConfUsing.h>
#include <NimBLEDevice.h>
#include <SettingsManager.h>
#include <UUIDs.h>
#include <Utils.h>
#include <Version.h>
#include <WiFi.h>

void networkEvent(WiFiEvent_t event);
void handleWebServerFile(const String& fileName);
void handleWebServerStatus();
void handleWebServerSettings();
void handleWebServerSettingsPost();
void handleWebServerDebug();

bool isMDNSStarted = false;
bool isBLEConnected = false;
bool isEthernetConnected = false;
bool isWiFiConnected = false;

DNSServer dnsServer;
WebServer webServer(WEB_SERVER_PORT);
HTTPUpdateServer updateServer;
IotWebConf iotWebConf(Utils::getHostName().c_str(), &dnsServer, &webServer, Utils::getHostName().c_str(), WIFI_CONFIG_VERSION);

ServiceManager serviceManager;

void setup() {
  log_i(DEVICE_NAME_PREFIX " " VERSION " starting...");
  log_i("Device name: %s, host name: %s", Utils::getDeviceName().c_str(), Utils::getFQDN().c_str());

  // initialize bluetooth device manager
  BTDeviceManager::setLocalDeviceName(Utils::getDeviceName());
  BTDeviceManager::setServiceManager(&serviceManager);
  if (!BTDeviceManager::start()) {
    log_e("Startup failed: Unable to start bluetooth device manager");
    ESP.restart();
  }
  log_i("Bluetooth device manager initialized");

  // initialize network events
  WiFi.onEvent(networkEvent);
  log_i("Network events initialized");

  // initialize ethernet interface
  ETH.begin();
  log_i("Ethernet interface initialized");

  // initialize settings manager
  SettingsManager::initialize(&iotWebConf);

  // initialize wifi manager and web server
  iotWebConf.setStatusPin(WIFI_STATUS_PIN);
  iotWebConf.setConfigPin(WIFI_CONFIG_PIN);
  iotWebConf.addParameterGroup(SettingsManager::getIoTWebConfSettingsParameterGroup());
  iotWebConf.setupUpdateServer(
      [](const char* updatePath) { updateServer.setup(&webServer, updatePath, SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str()); },
      [](const char* userName, char* password) { updateServer.updateCredentials(userName, password); });
  iotWebConf.init();

  // workaround for missing thing name
  strncpy(iotWebConf.getThingNameParameter()->valueBuffer, Utils::getHostName().c_str(), iotWebConf.getThingNameParameter()->getLength());

  webServer.on("/debug", handleWebServerDebug);
  webServer.on("/status", handleWebServerStatus);
  webServer.on("/favicon.ico", [] { handleWebServerFile("favicon.ico"); });
  webServer.on("/style.css", [] { handleWebServerFile("style.css"); });
  webServer.on("/", [] { handleWebServerFile("index.html"); });
  webServer.on("/settings", HTTP_GET, [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerFile("settings.html"); });
  webServer.on("/settings", HTTP_POST, [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerSettingsPost(); });
  webServer.on("/devicesettings", [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    handleWebServerSettings(); });
  webServer.on("/config", [] { 
    if (!webServer.authenticate(SettingsManager::getUsername().c_str(), SettingsManager::getAPPassword().c_str())) {
      return webServer.requestAuthentication();
    }
    iotWebConf.handleConfig(); });
  
  webServer.onNotFound([]() { iotWebConf.handleNotFound(); });
  log_i("WiFi manager and web server initialized");

  // initialize service manager internal service if enabled
  if (SettingsManager::isVirtualShiftingEnabled()) {
    Service* zwiftCustomService = new Service(NimBLEUUID(ZWIFT_CUSTOM_SERVICE_UUID), true, true);
    zwiftCustomService->addCharacteristic(new Characteristic(NimBLEUUID(ZWIFT_ASYNC_CHARACTERISTIC_UUID), NOTIFY));
    zwiftCustomService->addCharacteristic(new Characteristic(NimBLEUUID(ZWIFT_SYNCRX_CHARACTERISTIC_UUID), WRITE));
    zwiftCustomService->addCharacteristic(new Characteristic(NimBLEUUID(ZWIFT_SYNCTX_CHARACTERISTIC_UUID), INDICATE));
    serviceManager.addService(zwiftCustomService);
  }

  // initialize service manager FTMS service if enabled
  if (SettingsManager::isFTMSEnabled()) {
    Service* fitnessMachineService = new Service(NimBLEUUID(FITNESS_MACHINE_SERVICE_UUID), true, true);
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(FITNESS_MACHINE_FEATURE_CHARACTERISTIC_UUID), READ));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(INDOOR_BIKE_DATA_CHARACTERISTIC_UUID), NOTIFY));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(TRAINING_STATUS_CHARACTERISTIC_UUID), READ | NOTIFY));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(FITNESS_MACHINE_CONTROL_POINT_CHARACTERISTIC_UUID), WRITE | INDICATE));
    fitnessMachineService->addCharacteristic(new Characteristic(NimBLEUUID(FITNESS_MACHINE_STATUS_CHARACTERISTIC_UUID), NOTIFY));
    serviceManager.addService(fitnessMachineService);
  }
  log_i("Service manager initialized");

  // set BTDeviceManager selected trainer device
  BTDeviceManager::setRemoteDeviceNameFilter(SettingsManager::getTrainerDeviceName());

  // initialize MDNS
  if (!MDNS.begin(Utils::getHostName().c_str())) {
    log_e("Startup failed: Unable to start MDNS");
    ESP.restart();
  }
  MDNS.setInstanceName(Utils::getDeviceName().c_str());
  isMDNSStarted = true;
  log_i("MDNS initialized");

  // initialize DirCon manager
  DirConManager::setServiceManager(&serviceManager);
  if (!DirConManager::start()) {
    log_e("Startup failed: Unable to start DirCon manager");
    ESP.restart();
  }
  log_i("DirCon Manager initialized");

  log_i("Startup finished");
}

void loop() {
  BTDeviceManager::update();
  DirConManager::update();
  iotWebConf.doLoop();
}

void networkEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      log_d("Ethernet started");
      ETH.setHostname(Utils::getHostName().c_str());
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      log_i("Ethernet connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      log_i("Ethernet DHCP successful with IP %u.%u.%u.%u", ETH.localIP()[0], ETH.localIP()[1], ETH.localIP()[2], ETH.localIP()[3]);
      isEthernetConnected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      log_i("Ethernet disconnected");
      isEthernetConnected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      log_d("Ethernet stopped");
      isEthernetConnected = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      log_i("WiFi DHCP successful with IP %u.%u.%u.%u", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
      isWiFiConnected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      log_i("WiFi disconnected");
      isWiFiConnected = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      log_d("WiFi stopped");
      isWiFiConnected = false;
      break;
    default:
      break;
  }
}

extern const uint8_t favicon_ico_start[] asm("_binary_src_web_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_src_web_favicon_ico_end");

extern const uint8_t index_html_start[] asm("_binary_src_web_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_src_web_index_html_end");

extern const uint8_t settings_html_start[] asm("_binary_src_web_settings_html_start");
extern const uint8_t settings_html_end[] asm("_binary_src_web_settings_html_end");

extern const uint8_t style_css_start[] asm("_binary_src_web_style_css_start");
extern const uint8_t style_css_end[] asm("_binary_src_web_style_css_end");

void handleWebServerFile(const String& fileName) {
  if (iotWebConf.handleCaptivePortal()) {
    return;
  }

  if (fileName.equals("index.html")) {
    webServer.send_P(200, "text/html", (char*)index_html_start, (index_html_end - index_html_start));
  }
  if (fileName.equals("settings.html")) {
    webServer.send_P(200, "text/html", (char*)settings_html_start, (settings_html_end - settings_html_start));
  }
  if (fileName.equals("style.css")) {
    webServer.send_P(200, "text/css", (char*)style_css_start, (style_css_end - style_css_start));
  }
  if (fileName.equals("favicon.ico")) {
    webServer.send_P(200, "image/x-icon", (char*)favicon_ico_start, (favicon_ico_end - favicon_ico_start));
  }
}

void handleWebServerSettings() {
  String json = "{";
  json += "\"device_name\": \"";
  json += Utils::getDeviceName().c_str();
  json += "\",";

  json += "\"trainer_device\": \"";
  json += SettingsManager::getTrainerDeviceName().c_str();
  json += "\",";

  String devices_json = "\"trainer_devices\": [";
  devices_json += "\"\",";
  for (size_t deviceIndex = 0; deviceIndex < BTDeviceManager::getScannedDevices()->size(); deviceIndex++) {
    if (BTDeviceManager::getScannedDevices()->at(deviceIndex).haveName()) {
      devices_json += "\"";
      devices_json += BTDeviceManager::getScannedDevices()->at(deviceIndex).getName().c_str();
      devices_json += "\",";
    }
  }

  if (devices_json.endsWith(",")) {
    devices_json.remove(devices_json.length() - 1);
  }
  devices_json += "],";

  json += devices_json;

  json += "\"virtual_shifting\": ";
  if (SettingsManager::isVirtualShiftingEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += ",";

  json += "\"virtual_shifting_mode\": ";
  json += SettingsManager::getVirtualShiftingMode();
  json += ",";

  std::map<size_t, std::string> modes = SettingsManager::getVirtualShiftingModes();
  String modes_json = "\"virtual_shifting_modes\": [";
  for (auto mode = modes.begin(); mode != modes.end(); mode++) {
      modes_json += "{\"name\": \"";
      modes_json += mode->second.c_str();
      modes_json += "\", \"value\": ";
      modes_json += mode->first;
      modes_json += "},";
  }
  if (modes_json.endsWith(",")) {
    modes_json.remove(modes_json.length() - 1);
  }
  modes_json += "],";

  json += modes_json;

  json += "\"chainring_teeth\": ";
  json += SettingsManager::getChainringTeeth();
  json += ",";

  json += "\"sprocket_teeth\": ";
  json += SettingsManager::getSprocketTeeth();
  json += ",";

  json += "\"grade_smoothing\": ";
  if (SettingsManager::isGradeSmoothingEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += ",";

  json += "\"difficulty\": ";
  json += SettingsManager::getDifficulty();
  json += ",";
  
  json += "\"ftms_emulation\": ";
  if (SettingsManager::isFTMSEnabled()) {
    json += "true";
  } else {
    json += "false";
  }
  json += "";

  json += "}";

  webServer.send(200, "application/json", json);
}

void handleWebServerSettingsPost() {
  if (webServer.args() > 0) {
    if (webServer.hasArg("trainer_device")) {
      SettingsManager::setTrainerDeviceName(webServer.arg("trainer_device").c_str());
    }
    if (webServer.hasArg("virtual_shifting")) {
      SettingsManager::setVirtualShiftingEnabled(true);
    } else {
      SettingsManager::setVirtualShiftingEnabled(false);
    }
    if (webServer.hasArg("virtual_shifting_mode")) {
      SettingsManager::setVirtualShiftingMode((VirtualShiftingMode)std::atoi(webServer.arg("virtual_shifting_mode").c_str()));
    }
    if (webServer.hasArg("chainring_teeth")) {
      SettingsManager::setChainringTeeth(std::atoi(webServer.arg("chainring_teeth").c_str()));
    }
    if (webServer.hasArg("sprocket_teeth")) {
      SettingsManager::setSprocketTeeth(std::atoi(webServer.arg("sprocket_teeth").c_str()));
    }
    if (webServer.hasArg("grade_smoothing")) {
      SettingsManager::setGradeSmoothingEnabled(true);
    } else {
      SettingsManager::setGradeSmoothingEnabled(false);
    }
    if (webServer.hasArg("difficulty")) {
      SettingsManager::setDifficulty(std::atoi(webServer.arg("difficulty").c_str()));
    }
    if (webServer.hasArg("ftms_emulation")) {
      SettingsManager::setFTMSEnabled(true);
      SettingsManager::setVirtualShiftingEnabled(false);
    } else {
      SettingsManager::setFTMSEnabled(false);
    }
    iotWebConf.saveConfig();
    delay(500);
    ESP.restart();
  }
}

void handleWebServerStatus() {
  String json = "{";
  json += "\"device_name\": \"";
  json += Utils::getDeviceName().c_str();
  json += "\",";

  json += "\"version\": \"";
  json += VERSION;
  json += "\",";

  json += "\"build_timestamp\": \"";
  json += VERSION_TIMESTAMP;
  json += "\",";

  json += "\"hostname\": \"";
  json += Utils::getFQDN().c_str();
  json += "\",";

  json += "\"ethernet_status\": \"";
  if (isEthernetConnected) {
    json += "Connected";
    json += ", IP: ";
    json += ETH.localIP().toString();
  } else {
    json += "Not connected";
  }
  json += "\",";

  json += "\"wifi_status\": \"";
  switch (iotWebConf.getState()) {
    case iotwebconf::NetworkState::ApMode:
      json += "Access-Point mode";
      break;
    case iotwebconf::NetworkState::Boot:
      json += "Booting";
      break;
    case iotwebconf::NetworkState::Connecting:
      json += "Connecting";
      break;
    case iotwebconf::NetworkState::NotConfigured:
      json += "Not configured";
      break;
    case iotwebconf::NetworkState::OffLine:
      json += "Disconnected";
      break;
    case iotwebconf::NetworkState::OnLine:
      json += "Connected";
      json += ", SSID: ";
      json += WiFi.SSID();
      json += ", IP: ";
      json += WiFi.localIP().toString();
      break;
    default:
      json += "Unknown";
      break;
  }
  json += "\",";

  json += "\"service_status\": \"";
  json += serviceManager.getStatusMessage().c_str();
  json += "\",";

  json += "\"dircon_status\": \"";
  json += DirConManager::getStatusMessage().c_str();
  json += "\",";

  json += "\"ble_status\": \"";
  json += BTDeviceManager::getStatusMessage().c_str();
  json += "\",";

  json += "\"mode\": \"";
  json += "Pass-through";
  if (SettingsManager::isFTMSEnabled()) {
    json += " + FTMS emulation";
  }
  if (SettingsManager::isVirtualShiftingEnabled()) {
    json += " + virtual shifting";
  }
  json += "\",";

  json += "\"free_heap\": ";
  json += ESP.getFreeHeap();
  json += "}";

  webServer.send(200, "application/json", json);
}

void handleWebServerDebug() {

  String json = "{";
  json += "\"zwift_trainer_mode\": \"";
  switch (DirConManager::getZwiftTrainerMode()) {
    case TrainerMode::SIM_MODE:
      json += "SIM mode";
      break;
    case TrainerMode::SIM_MODE_VIRTUAL_SHIFTING:
      json += "SIM + VS mode";
      break;
    default:
      json += "ERG mode";
      break;
  }
  json += "\",";

  String services_json = "\"ble_services\": {";
  for (Service* service : serviceManager.getServices()) {
    services_json += "\"";
    services_json += service->UUID.to128().toString().c_str();
    services_json += "\": {";
    for (Characteristic* characteristic : service->getCharacteristics()) {
      services_json += "\"";
      services_json += characteristic->UUID.to128().toString().c_str();
      services_json += "\": ";
      services_json += characteristic->getSubscriptions().size();
      services_json += ",";
    }
    if (services_json.endsWith(",")) {
      services_json.remove(services_json.length() - 1);
    }
    services_json += "},";
  }
  if (services_json.endsWith(",")) {
    services_json.remove(services_json.length() - 1);
  }
  services_json += "}";

  json += services_json;

  json += "}";

  webServer.send(200, "application/json", json);
}