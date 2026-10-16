#pragma once
#include <string.h>
#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define F(s) (reinterpret_cast<const __FlashStringHelper*>(s))
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper*>(p))
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#define pgm_read_word(addr) (*(const unsigned short*)(addr))
#define pgm_read_dword(addr) (*(const unsigned long*)(addr))
#define pgm_read_ptr(addr) (*(void* const*)(addr))
#define pgm_read_float(addr) (*(const float*)(addr))
#define strlen_P strlen
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define strstr_P strstr
#define memcpy_P memcpy
#define snprintf_P snprintf
#define sprintf_P sprintf
#define vsnprintf_P vsnprintf
#ifdef __cplusplus
class __FlashStringHelper;
#endif
