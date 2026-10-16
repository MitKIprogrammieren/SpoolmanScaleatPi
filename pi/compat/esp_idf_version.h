#pragma once
#define ESP_IDF_VERSION_MAJOR 5
#define ESP_IDF_VERSION_MINOR 5
#define ESP_IDF_VERSION_PATCH 0
#define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 5, 0)
inline const char* esp_get_idf_version() { return "linux"; }
