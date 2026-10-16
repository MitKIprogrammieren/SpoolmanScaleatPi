// Flash partitions emulated as files in the data dir (sized from partitions.csv).
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

typedef enum { ESP_PARTITION_TYPE_APP = 0x00, ESP_PARTITION_TYPE_DATA = 0x01, ESP_PARTITION_TYPE_ANY = 0xff } esp_partition_type_t;
typedef enum {
  ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
  ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
  ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
  ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,
  ESP_PARTITION_SUBTYPE_DATA_PHY = 0x01,
  ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
  ESP_PARTITION_SUBTYPE_DATA_COREDUMP = 0x03,
  ESP_PARTITION_SUBTYPE_DATA_UNDEFINED = 0x06,
  ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81,
  ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82,
  ESP_PARTITION_SUBTYPE_DATA_LITTLEFS = 0x83,
  ESP_PARTITION_SUBTYPE_ANY = 0xff
} esp_partition_subtype_t;

typedef struct {
  esp_partition_type_t type;
  esp_partition_subtype_t subtype;
  uint32_t address;
  uint32_t size;
  uint32_t erase_size;
  char label[17];
  bool encrypted;
  void* priv;
} esp_partition_t;

const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype,
                                                const char* label);
esp_err_t esp_partition_read(const esp_partition_t* p, size_t off, void* dst, size_t len);
esp_err_t esp_partition_write(const esp_partition_t* p, size_t off, const void* src, size_t len);
esp_err_t esp_partition_erase_range(const esp_partition_t* p, size_t off, size_t len);
