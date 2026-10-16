#pragma once
#include "lwip/ip_addr.h"
typedef void (*dns_found_callback)(const char* name, const ip_addr_t* ipaddr, void* arg);
// Resolves in a helper thread and calls back from there, like lwIP's tcpip task.
err_t dns_gethostbyname(const char* hostname, ip_addr_t* addr, dns_found_callback found, void* arg);
