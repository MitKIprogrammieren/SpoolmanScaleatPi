// HTTPClient on libcurl. A request runs to completion inside GET()/POST()/...;
// the body is then served from memory through getString() or getStream().
#pragma once

#include <map>
#include <string>
#include <vector>

#include "Arduino.h"
#include "WiFiClient.h"
#include "WiFiClientSecure.h"

#define HTTPCLIENT_DEFAULT_TCP_TIMEOUT (5000)

#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_SEND_HEADER_FAILED (-2)
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
#define HTTPC_ERROR_NOT_CONNECTED (-4)
#define HTTPC_ERROR_CONNECTION_LOST (-5)
#define HTTPC_ERROR_NO_STREAM (-6)
#define HTTPC_ERROR_NO_HTTP_SERVER (-7)
#define HTTPC_ERROR_TOO_LESS_RAM (-8)
#define HTTPC_ERROR_ENCODING (-9)
#define HTTPC_ERROR_STREAM_WRITE (-10)
#define HTTPC_ERROR_READ_TIMEOUT (-11)

typedef enum {
  HTTP_CODE_CONTINUE = 100,
  HTTP_CODE_OK = 200,
  HTTP_CODE_CREATED = 201,
  HTTP_CODE_NO_CONTENT = 204,
  HTTP_CODE_MOVED_PERMANENTLY = 301,
  HTTP_CODE_FOUND = 302,
  HTTP_CODE_NOT_MODIFIED = 304,
  HTTP_CODE_BAD_REQUEST = 400,
  HTTP_CODE_UNAUTHORIZED = 401,
  HTTP_CODE_FORBIDDEN = 403,
  HTTP_CODE_NOT_FOUND = 404,
  HTTP_CODE_INTERNAL_SERVER_ERROR = 500,
  HTTP_CODE_BAD_GATEWAY = 502,
  HTTP_CODE_SERVICE_UNAVAILABLE = 503,
  HTTP_CODE_GATEWAY_TIMEOUT = 504,
} t_http_codes;

typedef enum { HTTPC_TE_IDENTITY, HTTPC_TE_CHUNKED } transferEncoding_t;
typedef enum {
  HTTPC_DISABLE_FOLLOW_REDIRECTS,
  HTTPC_STRICT_FOLLOW_REDIRECTS,
  HTTPC_FORCE_FOLLOW_REDIRECTS
} followRedirects_t;

class HTTPClient {
 public:
  HTTPClient();
  virtual ~HTTPClient();

  bool begin(const String& url);
  bool begin(const char* url) { return begin(String(url)); }
  bool begin(WiFiClient& client, const String& url);
  bool begin(WiFiClient& client, const char* url) { return begin(client, String(url)); }
  bool begin(const String& host, uint16_t port, const String& uri = "/", bool https = false);
  void end();
  bool connected();

  void setReuse(bool) {}
  void setUserAgent(const String& ua) { user_agent_ = ua.c_str(); }
  void setAuthorization(const char* user, const char* pass);
  void setAuthorization(const char* auth);
  void setTimeout(uint16_t ms) { timeout_ms_ = ms; }
  void setConnectTimeout(int32_t ms) { connect_timeout_ms_ = ms; }
  void setFollowRedirects(followRedirects_t f) { follow_ = f; }
  void useHTTP10(bool) {}

  void addHeader(const String& name, const String& value, bool first = false, bool replace = true);
  void collectHeaders(const char* keys[], size_t count);
  String header(const char* name);
  String header(size_t i);
  String headerName(size_t i);
  int headers() { return (int)collect_.size(); }
  bool hasHeader(const char* name);

  int GET();
  int POST(const uint8_t* payload, size_t size);
  int POST(const String& payload) { return POST((const uint8_t*)payload.c_str(), payload.length()); }
  int PUT(const uint8_t* payload, size_t size);
  int PUT(const String& payload) { return PUT((const uint8_t*)payload.c_str(), payload.length()); }
  int PATCH(const uint8_t* payload, size_t size);
  int PATCH(const String& payload) { return PATCH((const uint8_t*)payload.c_str(), payload.length()); }
  int sendRequest(const char* type, const String& payload);
  int sendRequest(const char* type, const uint8_t* payload = nullptr, size_t size = 0);

  int getSize() { return size_; }
  const String& getLocation() { return location_; }
  WiFiClient& getStream();
  WiFiClient* getStreamPtr() { return &getStream(); }
  String getString();
  int writeToStream(Stream* s);
  static String errorToString(int error);

 protected:
  int perform(const char* method, const uint8_t* payload, size_t size);

  std::string url_;
  bool insecure_ = false;
  WiFiClient* client_ = nullptr;   // the caller's client, receives the body
  WiFiClient own_client_;
  std::vector<std::pair<std::string, std::string>> req_headers_;
  std::vector<std::pair<std::string, std::string>> collect_;   // name -> value
  std::string user_agent_ = "SpoolmanScale-Pi";
  std::string auth_user_, auth_pass_, auth_raw_;
  uint32_t timeout_ms_ = HTTPCLIENT_DEFAULT_TCP_TIMEOUT;
  int32_t connect_timeout_ms_ = HTTPCLIENT_DEFAULT_TCP_TIMEOUT;
  followRedirects_t follow_ = HTTPC_DISABLE_FOLLOW_REDIRECTS;
  String location_;
  std::string body_;
  bool body_taken_ = false;
  bool begun_ = false;

  // Names kept from the ESP HTTPClient so subclasses that peek at them compile.
  int _returnCode = 0;
  int _size = -1;
  transferEncoding_t _transferEncoding = HTTPC_TE_IDENTITY;
  uint32_t _tcpTimeout = HTTPCLIENT_DEFAULT_TCP_TIMEOUT;
  int size_ = -1;
};
