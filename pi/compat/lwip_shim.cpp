#include "lwip/dns.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <thread>

err_t dns_gethostbyname(const char* hostname, ip_addr_t* addr, dns_found_callback found, void* arg) {
  if (!hostname || !*hostname) return ERR_ARG;
  struct in_addr a;
  if (inet_pton(AF_INET, hostname, &a) == 1) {
    addr->u_addr_ip4.addr = a.s_addr;
    return ERR_OK;
  }
  std::string h = hostname;
  std::thread([h, found, arg] {
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(h.c_str(), nullptr, &hints, &res) != 0 || !res) {
      if (found) found(h.c_str(), nullptr, arg);
      return;
    }
    ip_addr_t ip;
    ip.u_addr_ip4.addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    if (found) found(h.c_str(), &ip, arg);
  }).detach();
  return ERR_INPROGRESS;
}
