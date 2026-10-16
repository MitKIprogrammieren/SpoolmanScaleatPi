// A plain TCP client (used by the web server for accepted sockets) that can
// also carry an HTTP response body handed over by HTTPClient.
#pragma once

#include <memory>
#include <string>

#include "Arduino.h"
#include "IPAddress.h"

class WiFiClient : public Stream {
 public:
  WiFiClient() {}
  explicit WiFiClient(int fd);
  WiFiClient(const WiFiClient& o);
  WiFiClient& operator=(const WiFiClient& o);
  virtual ~WiFiClient();

  virtual int connect(const char* host, uint16_t port, int32_t timeout_ms = 5000);
  virtual int connect(IPAddress ip, uint16_t port, int32_t timeout_ms = 5000);
  virtual void stop();
  virtual uint8_t connected();
  operator bool() { return connected(); }
  bool operator==(const WiFiClient& o) const { return sock_ == o.sock_; }

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  int available() override;
  int read() override;
  virtual int read(uint8_t* buf, size_t n);
  int peek() override;
  void flush() override {}
  using Stream::readBytes;
  size_t readBytes(char* buf, size_t len) override;

  IPAddress remoteIP() const;
  uint16_t remotePort() const;
  IPAddress localIP() const;
  void setNoDelay(bool) {}
  int fd() const;

  // HTTPClient puts a finished response body here; reads then come from it.
  void setBody(std::string body);
  bool hasBody() const { return body_ != nullptr; }

 protected:
  struct Sock;
  std::shared_ptr<Sock> sock_;
  std::shared_ptr<std::string> body_;
  size_t body_pos_ = 0;
};

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port = 80) : port_(port) {}
  ~WiFiServer() { end(); }
  void begin(uint16_t port = 0);
  void end();
  void stop() { end(); }
  bool hasClient();
  WiFiClient accept();
  WiFiClient available() { return accept(); }
  void setNoDelay(bool) {}
  operator bool() const { return fd_ >= 0; }
 private:
  uint16_t port_;
  int fd_ = -1;
};
