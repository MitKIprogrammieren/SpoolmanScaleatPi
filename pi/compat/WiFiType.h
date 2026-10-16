#pragma once
#include <cstdint>

typedef enum {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6,
  WL_STOPPED = 254,
} wl_status_t;

typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3, WIFI_MODE_NULL = 0 } wifi_mode_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
               WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK } wifi_auth_mode_t;
typedef enum { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN } wifi_scan_method_t;
typedef enum { WIFI_CONNECT_AP_BY_SIGNAL = 0, WIFI_CONNECT_AP_BY_SECURITY } wifi_sort_method_t;
typedef enum { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

typedef enum {
  ARDUINO_EVENT_NONE = 0,
  ARDUINO_EVENT_WIFI_STA_START,
  ARDUINO_EVENT_WIFI_STA_CONNECTED,
  ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
  ARDUINO_EVENT_WIFI_STA_GOT_IP,
  ARDUINO_EVENT_WIFI_STA_LOST_IP,
  ARDUINO_EVENT_WIFI_SCAN_DONE,
  ARDUINO_EVENT_MAX
} arduino_event_id_t;
typedef arduino_event_id_t WiFiEvent_t;

typedef struct {
  struct { uint8_t reason; } wifi_sta_disconnected;
} arduino_event_info_t;
typedef arduino_event_info_t WiFiEventInfo_t;

typedef struct {
  arduino_event_id_t event_id;
  arduino_event_info_t event_info;
} arduino_event_t;
