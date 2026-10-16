#pragma once
#include <cstdint>
#include <cstddef>
uint32_t esp_random();
void esp_fill_random(void* buf, size_t len);
