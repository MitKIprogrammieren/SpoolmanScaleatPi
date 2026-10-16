#pragma once
#include <cstdint>
typedef int8_t err_t;
#define ERR_OK 0
#define ERR_MEM (-1)
#define ERR_INPROGRESS (-5)
#define ERR_VAL (-6)
#define ERR_ARG (-16)
typedef struct { uint32_t addr; } ip4_addr_t;
typedef struct { ip4_addr_t u_addr_ip4; } ip_addr_t;
#define ip_2_ip4(ipaddr) (&((ipaddr)->u_addr_ip4))
#define ip4_addr_get_u32(src_ipaddr) ((src_ipaddr)->addr)
