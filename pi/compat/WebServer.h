// A single-threaded HTTP/1.1 server with the ESP32 WebServer API. handleClient()
// is polled from the main loop, so handlers run where they ran on the ESP.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Arduino.h"
#include "WiFiClient.h"

typedef enum { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS } HTTPMethod;
typedef enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED } HTTPUploadStatus;
typedef enum { HC_NONE, HC_WAIT_READ, HC_WAIT_CLOSE } HTTPClientStatus;

#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#define CONTENT_LENGTH_NOT_SET ((size_t)-2)
#define HTTP_MAX_SEND_WAIT 5000
#define HTTP_UPLOAD_BUFLEN 1436

struct HTTPUpload {
  HTTPUploadStatus status;
  String filename;
  String name;
  String type;
  size_t totalSize;
  size_t currentSize;
  uint8_t buf[HTTP_UPLOAD_BUFLEN];
};

class WebServer {
 public:
  typedef std::function<void(void)> THandlerFunction;

  explicit WebServer(int port = 80) : port_(port) {}
  virtual ~WebServer() { stop(); }

  void begin();
  void begin(uint16_t port) { port_ = port; begin(); }
  void stop();
  void close() { stop(); }
  virtual void handleClient();

  void on(const String& uri, THandlerFunction fn) { on(uri, HTTP_ANY, fn); }
  void on(const String& uri, HTTPMethod method, THandlerFunction fn) { on(uri, method, fn, nullptr); }
  void on(const String& uri, HTTPMethod method, THandlerFunction fn, THandlerFunction ufn);
  void onNotFound(THandlerFunction fn) { not_found_ = fn; }
  void onFileUpload(THandlerFunction fn) { file_upload_ = fn; }

  String uri() const { return String(uri_); }
  HTTPMethod method() const { return method_; }
  WiFiClient& client() { return client_; }
  size_t clientContentLength() const { return req_content_length_; }
  HTTPUpload& upload() { return *upload_; }

  String arg(const String& name) const;
  String arg(int i) const;
  String argName(int i) const;
  int args() const { return (int)args_.size(); }
  bool hasArg(const String& name) const;
  void collectHeaders(const char* keys[], size_t count);
  String header(const String& name) const;
  String header(int i) const;
  String headerName(int i) const;
  int headers() const { return (int)req_headers_.size(); }
  bool hasHeader(const String& name) const;
  String hostHeader() const { return header("Host"); }

  bool authenticate(const char* user, const char* pass);
  void requestAuthentication(int mode = 0, const char* realm = nullptr, const String& fail = String());

  void send(int code, const char* type = nullptr, const String& content = String());
  void send(int code, const String& type, const String& content) { send(code, type.c_str(), content); }
  void send(int code, const char* type, const char* content) { send(code, type, String(content)); }
  void send(int code, const char* type, const uint8_t* content, size_t len);
  void send(int code, const char* type, const char* content, size_t len) { send(code, type, (const uint8_t*)content, len); }
  void send_P(int code, const char* type, const char* content) { send(code, type, String(content)); }
  void send_P(int code, const char* type, const char* content, size_t len) { send(code, type, (const uint8_t*)content, len); }
  void setContentLength(size_t len) { content_length_ = len; }
  void sendHeader(const String& name, const String& value, bool first = false);
  void sendContent(const String& content) { sendContent(content.c_str(), content.length()); }
  void sendContent(const char* content, size_t len);
  void sendContent_P(const char* content) { sendContent(content, strlen(content)); }
  void sendContent_P(const char* content, size_t len) { sendContent(content, len); }
  void enableCORS(bool on = true) { cors_ = on; }
  void enableCrossOrigin(bool on = true) { cors_ = on; }
  void enableDelay(bool) {}
  template <typename T> size_t streamFile(T& file, const String& type, int code = 200);

 protected:
  struct Route {
    std::string uri;
    HTTPMethod method;
    THandlerFunction fn;
    THandlerFunction ufn;
  };
  bool readRequest();
  void dispatch();
  void sendRaw(const std::string& s);
  void startResponse(int code, const char* type, size_t len);
  void parseQuery(const std::string& q);
  bool parseMultipart(const std::string& body, const std::string& boundary, const Route* r);
  static std::string urlDecode(const std::string& s);

  int port_;
  int listen_fd_ = -1;
  WiFiClient client_;
  std::vector<Route> routes_;
  THandlerFunction not_found_;
  THandlerFunction file_upload_;
  std::unique_ptr<HTTPUpload> upload_{new HTTPUpload()};

  HTTPMethod method_ = HTTP_GET;
  std::string uri_;
  std::string raw_body_;
  std::vector<std::pair<std::string, std::string>> args_;
  std::vector<std::pair<std::string, std::string>> req_headers_;
  std::vector<std::string> collect_keys_;
  std::vector<std::pair<std::string, std::string>> resp_headers_;
  size_t content_length_ = CONTENT_LENGTH_NOT_SET;
  size_t req_content_length_ = 0;
  bool chunked_ = false;
  bool headers_sent_ = false;
  bool cors_ = false;
};

template <typename T>
size_t WebServer::streamFile(T& file, const String& type, int code) {
  const size_t len = file.size();
  setContentLength(len);
  send(code, type.c_str(), "");
  char buf[4096];
  size_t total = 0;
  while (total < len) {
    int n = file.read((uint8_t*)buf, sizeof(buf));
    if (n <= 0) break;
    client_.write((const uint8_t*)buf, (size_t)n);
    total += (size_t)n;
  }
  return total;
}
