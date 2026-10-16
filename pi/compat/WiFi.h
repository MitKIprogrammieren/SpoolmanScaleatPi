// WiFi on Linux: the network belongs to the OS (NetworkManager). This class
// reports what the OS has and asks nmcli for scans and connections.
#pragma once

#include <functional>
#include <vector>

#include "Arduino.h"
#include "IPAddress.h"
#include "WiFiType.h"
#include "WiFiClient.h"

typedef std::function<void(arduino_event_id_t, arduino_event_info_t)> WiFiEventFuncCb;
typedef void (*WiFiEventCb)(arduino_event_id_t);
typedef void (*WiFiEventSysCb)(arduino_event_t*);

typedef uint8_t wifi_err_reason_t;
struct WiFiSTAClass {
  const char* disconnectReasonName(wifi_err_reason_t r) { return r ? "disconnected" : "none"; }
};

class WiFiClass {
 public:
  WiFiSTAClass STA;
  // Station
  wl_status_t begin(const char* ssid, const char* pass = nullptr, int32_t channel = 0,
                    const uint8_t* bssid = nullptr, bool connect = true);
  wl_status_t begin(const String& ssid, const String& pass) { return begin(ssid.c_str(), pass.c_str()); }
  wl_status_t begin() { return status(); }
  bool disconnect(bool wifioff = false, bool eraseap = false);
  bool reconnect() { return true; }
  wl_status_t status();
  bool isConnected() { return status() == WL_CONNECTED; }
  bool mode(wifi_mode_t m) { mode_ = m; return true; }
  wifi_mode_t getMode() { return mode_; }
  bool setSleep(bool) { return true; }
  bool setSleep(wifi_ps_type_t) { return true; }
  bool setAutoReconnect(bool) { return true; }
  bool setHostname(const char*) { return true; }
  const char* getHostname();
  bool setTxPower(int) { return true; }
  void persistent(bool) {}
  void setScanMethod(wifi_scan_method_t) {}
  void setSortMethod(wifi_sort_method_t) {}
  void setMinSecurity(wifi_auth_mode_t) {}

  IPAddress localIP();
  IPAddress gatewayIP();
  IPAddress subnetMask();
  IPAddress dnsIP(uint8_t n = 0);
  String macAddress();
  uint8_t* macAddress(uint8_t* mac);
  String SSID();
  String SSID(uint8_t i);
  int8_t RSSI();
  int32_t RSSI(uint8_t i);
  uint8_t* BSSID();
  uint8_t* BSSID(uint8_t i);
  String BSSIDstr();
  int32_t channel();
  int32_t channel(uint8_t i);
  wifi_auth_mode_t encryptionType(uint8_t i);

  // Scanning, through nmcli in a background thread.
  int16_t scanNetworks(bool async = false, bool show_hidden = false, bool passive = false,
                       uint32_t max_ms_per_chan = 300, uint8_t channel = 0,
                       const char* ssid = nullptr, const uint8_t* bssid = nullptr);
  int16_t scanComplete();
  void scanDelete();

  // Soft AP is not offered on the Pi build: setup happens through the OS.
  bool softAP(const char*, const char* = nullptr, int = 1, int = 0, int = 4, bool = false) { return false; }
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { return false; }
  bool softAPdisconnect(bool = false) { return true; }
  IPAddress softAPIP() { return IPAddress(); }

  int hostByName(const char* host, IPAddress& out);

  int onEvent(WiFiEventFuncCb cb, arduino_event_id_t ev = ARDUINO_EVENT_MAX);
  int onEvent(WiFiEventCb cb, arduino_event_id_t ev = ARDUINO_EVENT_MAX);
  int onEvent(WiFiEventSysCb cb, arduino_event_id_t ev = ARDUINO_EVENT_MAX);
  void removeEvent(int) {}

 private:
  wifi_mode_t mode_ = WIFI_STA;
};

extern WiFiClass WiFi;
