// Minimal Arduino core for Linux (Raspberry Pi port of SpoolmanScale).
#pragma once

#ifndef ARDUINO
#define ARDUINO 10819
#endif
#define SPOOLMANSCALE_PI 1

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>
#include <unistd.h>

#ifdef __cplusplus
#include <algorithm>
using std::max;
using std::min;
#endif

#include "esp_attr.h"
#include "pgmspace.h"

#define ESP_ARDUINO_VERSION_MAJOR 3
#define ESP_ARDUINO_VERSION_MINOR 3
#define ESP_ARDUINO_VERSION_PATCH 0

#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
#ifdef __cplusplus
}
#endif

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define OUTPUT_OPEN_DRAIN 0x13

#define PI_CONST 3.14159265358979323846
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define bit(b) (1UL << (b))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))

#define SET_LOOP_TASK_STACK_SIZE(sz)
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) do {} while (0)
#define log_d(fmt, ...) do {} while (0)
#define log_v(fmt, ...) do {} while (0)

#ifdef __cplusplus
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

#include "WString.h"
#include "Printable.h"
#include "Print.h"
#include "Stream.h"
#include "HardwareSerial.h"
#include "Esp.h"
#include "IPAddress.h"

// What the ESP32 core's Arduino.h pulls in for every sketch.
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// esp32-hal-time
bool getLocalTime(struct tm* info, uint32_t ms = 5000);
void configTime(long gmt_offset_sec, int daylight_offset_sec, const char* server1,
                const char* server2 = nullptr, const char* server3 = nullptr);
void configTzTime(const char* tz, const char* server1, const char* server2 = nullptr,
                  const char* server3 = nullptr);

uint16_t makeWord(uint16_t w);
uint16_t makeWord(uint8_t h, uint8_t l);
#endif
