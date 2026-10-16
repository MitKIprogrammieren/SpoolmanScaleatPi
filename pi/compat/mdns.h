#pragma once
#include "esp_err.h"
typedef struct { const char* key; const char* value; } mdns_txt_item_t;
esp_err_t mdns_service_remove(const char* service, const char* proto);
esp_err_t mdns_service_txt_item_set(const char* service, const char* proto, const char* key, const char* value);
esp_err_t mdns_hostname_set(const char* hostname);
void mdns_free();
