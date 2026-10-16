#include "WiFiClient.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

struct WiFiClient::Sock {
  int fd;
  int peeked = -1;
  bool closed = false;
  explicit Sock(int f) : fd(f) {}
  ~Sock() { if (fd >= 0) ::close(fd); }
};

WiFiClient::WiFiClient(int fd) : sock_(std::make_shared<Sock>(fd)) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}
WiFiClient::WiFiClient(const WiFiClient& o) : Stream(o), sock_(o.sock_), body_(o.body_), body_pos_(o.body_pos_) {}
WiFiClient& WiFiClient::operator=(const WiFiClient& o) {
  sock_ = o.sock_;
  body_ = o.body_;
  body_pos_ = o.body_pos_;
  timeout_ = o.timeout_;
  return *this;
}
WiFiClient::~WiFiClient() {}

int WiFiClient::fd() const { return sock_ ? sock_->fd : -1; }

void WiFiClient::setBody(std::string body) {
  body_ = std::make_shared<std::string>(std::move(body));
  body_pos_ = 0;
}

int WiFiClient::connect(const char* host, uint16_t port, int32_t timeout_ms) {
  stop();
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char ps[8];
  snprintf(ps, sizeof(ps), "%u", port);
  if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) return 0;
  int fd = socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { freeaddrinfo(res); return 0; }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  int r = ::connect(fd, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (r != 0 && errno == EINPROGRESS) {
    struct pollfd p = {fd, POLLOUT, 0};
    if (poll(&p, 1, timeout_ms) == 1) {
      int err = 0;
      socklen_t l = sizeof(err);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
      r = err == 0 ? 0 : -1;
    }
  }
  if (r != 0) { ::close(fd); return 0; }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
  sock_ = std::make_shared<Sock>(fd);
  return 1;
}

int WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeout_ms) {
  return connect(ip.toString().c_str(), port, timeout_ms);
}

void WiFiClient::stop() {
  if (sock_ && sock_->fd >= 0) {
    ::shutdown(sock_->fd, SHUT_RDWR);
    sock_->closed = true;
  }
  sock_.reset();
  body_.reset();
  body_pos_ = 0;
}

uint8_t WiFiClient::connected() {
  if (body_) return body_pos_ < body_->size();
  if (!sock_ || sock_->fd < 0 || sock_->closed) return 0;
  if (sock_->peeked >= 0) return 1;
  struct pollfd p = {sock_->fd, POLLIN, 0};
  if (poll(&p, 1, 0) == 1) {
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      int n = 0;
      ioctl(sock_->fd, FIONREAD, &n);
      return n > 0;
    }
    char c;
    ssize_t r = recv(sock_->fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (r == 0) { sock_->closed = true; return 0; }
  }
  return 1;
}

size_t WiFiClient::write(const uint8_t* buf, size_t n) {
  if (!sock_ || sock_->fd < 0) return 0;
  size_t done = 0;
  const unsigned long t0 = millis();
  while (done < n) {
    ssize_t w = ::send(sock_->fd, buf + done, n - done, MSG_NOSIGNAL);
    if (w > 0) { done += (size_t)w; continue; }
    if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
      if (millis() - t0 > timeout_ + 5000) break;
      struct pollfd p = {sock_->fd, POLLOUT, 0};
      poll(&p, 1, 100);
      continue;
    }
    sock_->closed = true;
    break;
  }
  return done;
}

int WiFiClient::available() {
  if (body_) return (int)(body_->size() - body_pos_);
  if (!sock_ || sock_->fd < 0) return 0;
  int n = 0;
  ioctl(sock_->fd, FIONREAD, &n);
  return n + (sock_->peeked >= 0 ? 1 : 0);
}

int WiFiClient::read() {
  uint8_t c;
  return read(&c, 1) == 1 ? c : -1;
}

int WiFiClient::read(uint8_t* buf, size_t n) {
  if (!n) return 0;
  if (body_) {
    size_t left = body_->size() - body_pos_;
    if (!left) return -1;
    if (n > left) n = left;
    memcpy(buf, body_->data() + body_pos_, n);
    body_pos_ += n;
    return (int)n;
  }
  if (!sock_ || sock_->fd < 0) return -1;
  size_t got = 0;
  if (sock_->peeked >= 0) { buf[got++] = (uint8_t)sock_->peeked; sock_->peeked = -1; }
  if (got < n) {
    ssize_t r = recv(sock_->fd, buf + got, n - got, MSG_DONTWAIT);
    if (r > 0) got += (size_t)r;
    else if (r == 0) sock_->closed = true;
  }
  return got ? (int)got : -1;
}

int WiFiClient::peek() {
  if (body_) return body_pos_ < body_->size() ? (uint8_t)(*body_)[body_pos_] : -1;
  if (!sock_) return -1;
  if (sock_->peeked < 0) {
    uint8_t c;
    if (recv(sock_->fd, &c, 1, MSG_DONTWAIT) == 1) sock_->peeked = c;
  }
  return sock_->peeked;
}

size_t WiFiClient::readBytes(char* buf, size_t len) {
  if (body_) {
    int r = read((uint8_t*)buf, len);
    return r > 0 ? (size_t)r : 0;
  }
  size_t got = 0;
  const unsigned long t0 = millis();
  while (got < len && millis() - t0 < timeout_) {
    int r = read((uint8_t*)buf + got, len - got);
    if (r > 0) { got += (size_t)r; continue; }
    if (!sock_ || sock_->closed) break;
    struct pollfd p = {sock_->fd, POLLIN, 0};
    poll(&p, 1, 20);
  }
  return got;
}

IPAddress WiFiClient::remoteIP() const {
  if (!sock_ || sock_->fd < 0) return IPAddress();
  struct sockaddr_in a = {};
  socklen_t l = sizeof(a);
  if (getpeername(sock_->fd, (struct sockaddr*)&a, &l) != 0) return IPAddress();
  return IPAddress((uint32_t)a.sin_addr.s_addr);
}
uint16_t WiFiClient::remotePort() const {
  if (!sock_ || sock_->fd < 0) return 0;
  struct sockaddr_in a = {};
  socklen_t l = sizeof(a);
  if (getpeername(sock_->fd, (struct sockaddr*)&a, &l) != 0) return 0;
  return ntohs(a.sin_port);
}
IPAddress WiFiClient::localIP() const {
  if (!sock_ || sock_->fd < 0) return IPAddress();
  struct sockaddr_in a = {};
  socklen_t l = sizeof(a);
  if (getsockname(sock_->fd, (struct sockaddr*)&a, &l) != 0) return IPAddress();
  return IPAddress((uint32_t)a.sin_addr.s_addr);
}

// ---- WiFiServer -------------------------------------------------------------
void WiFiServer::begin(uint16_t port) {
  if (port) port_ = port;
  end();
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port_);
  if (bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(fd, 8) != 0) {
    fprintf(stderr, "WiFiServer: cannot listen on port %u (%s)\n", port_, strerror(errno));
    ::close(fd);
    return;
  }
  fd_ = fd;
}

void WiFiServer::end() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool WiFiServer::hasClient() {
  if (fd_ < 0) return false;
  struct pollfd p = {fd_, POLLIN, 0};
  return poll(&p, 1, 0) == 1;
}

WiFiClient WiFiServer::accept() {
  if (fd_ < 0) return WiFiClient();
  int c = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
  if (c < 0) return WiFiClient();
  return WiFiClient(c);
}
