#include "HTTPClient.h"

#include <curl/curl.h>
#include <mutex>
#include <strings.h>

namespace {
std::mutex s_share_locks[CURL_LOCK_DATA_LAST];

void shareLock(CURL*, curl_lock_data d, curl_lock_access, void*) { s_share_locks[d].lock(); }
void shareUnlock(CURL*, curl_lock_data d, void*) { s_share_locks[d].unlock(); }

// One connection cache, DNS cache and TLS session cache for every request,
// so a backend reached over https does not pay a handshake each time.
CURLSH* share() {
  static CURLSH* sh = [] {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURLSH* s = curl_share_init();
    curl_share_setopt(s, CURLSHOPT_LOCKFUNC, shareLock);
    curl_share_setopt(s, CURLSHOPT_UNLOCKFUNC, shareUnlock);
    curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    curl_share_setopt(s, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    return s;
  }();
  return sh;
}

size_t onBody(char* p, size_t sz, size_t n, void* ud) {
  static_cast<std::string*>(ud)->append(p, sz * n);
  return sz * n;
}

struct HeaderCtx {
  std::vector<std::pair<std::string, std::string>>* collect;
  String* location;
};

std::string trimmed(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) b++;
  while (e > b && (s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == ' ')) e--;
  return s.substr(b, e - b);
}

size_t onHeader(char* p, size_t sz, size_t n, void* ud) {
  HeaderCtx* ctx = static_cast<HeaderCtx*>(ud);
  std::string line(p, sz * n);
  if (line.rfind("HTTP/", 0) == 0) {
    // A new response (after a redirect or a 100): forget earlier values.
    for (auto& kv : *ctx->collect) kv.second.clear();
    return sz * n;
  }
  size_t colon = line.find(':');
  if (colon == std::string::npos) return sz * n;
  std::string name = trimmed(line.substr(0, colon));
  std::string value = trimmed(line.substr(colon + 1));
  if (strcasecmp(name.c_str(), "Location") == 0) *ctx->location = value.c_str();
  // libcurl has already removed the chunk framing, so the firmware must not
  // see "chunked" and try to take size lines out a second time.
  if (strcasecmp(name.c_str(), "Transfer-Encoding") == 0) return sz * n;
  for (auto& kv : *ctx->collect)
    if (strcasecmp(kv.first.c_str(), name.c_str()) == 0) kv.second = value;
  return sz * n;
}
}  // namespace

HTTPClient::HTTPClient() {}
HTTPClient::~HTTPClient() {}

bool HTTPClient::begin(const String& url) {
  url_ = url.c_str();
  client_ = &own_client_;
  insecure_ = false;
  begun_ = !url_.empty();
  return begun_;
}

bool HTTPClient::begin(WiFiClient& client, const String& url) {
  begin(url);
  client_ = &client;
  WiFiClientSecure* sec = dynamic_cast<WiFiClientSecure*>(&client);
  insecure_ = sec && sec->insecure();
  return begun_;
}

bool HTTPClient::begin(const String& host, uint16_t port, const String& uri, bool https) {
  String u = String(https ? "https://" : "http://") + host + ":" + String((unsigned)port) + uri;
  return begin(u);
}

void HTTPClient::end() {
  req_headers_.clear();
  body_.clear();
  body_taken_ = false;
  begun_ = false;
  size_ = _size = -1;
  _returnCode = 0;
}

bool HTTPClient::connected() {
  return client_ && client_->hasBody() && client_->available() > 0;
}

void HTTPClient::setAuthorization(const char* user, const char* pass) {
  auth_user_ = user ? user : "";
  auth_pass_ = pass ? pass : "";
  auth_raw_.clear();
}

void HTTPClient::setAuthorization(const char* auth) {
  auth_raw_ = auth ? auth : "";
  auth_user_.clear();
  auth_pass_.clear();
}

void HTTPClient::addHeader(const String& name, const String& value, bool first, bool replace) {
  if (replace) {
    for (auto& kv : req_headers_)
      if (strcasecmp(kv.first.c_str(), name.c_str()) == 0) { kv.second = value.c_str(); return; }
  }
  auto kv = std::make_pair(std::string(name.c_str()), std::string(value.c_str()));
  if (first) req_headers_.insert(req_headers_.begin(), kv);
  else req_headers_.push_back(kv);
}

void HTTPClient::collectHeaders(const char* keys[], size_t count) {
  collect_.clear();
  for (size_t i = 0; i < count; i++) collect_.emplace_back(keys[i], "");
}

String HTTPClient::header(const char* name) {
  for (auto& kv : collect_)
    if (strcasecmp(kv.first.c_str(), name) == 0) return String(kv.second);
  return String();
}
String HTTPClient::header(size_t i) { return i < collect_.size() ? String(collect_[i].second) : String(); }
String HTTPClient::headerName(size_t i) { return i < collect_.size() ? String(collect_[i].first) : String(); }
bool HTTPClient::hasHeader(const char* name) { return header(name).length() > 0; }

int HTTPClient::GET() { return perform("GET", nullptr, 0); }
int HTTPClient::POST(const uint8_t* p, size_t n) { return perform("POST", p, n); }
int HTTPClient::PUT(const uint8_t* p, size_t n) { return perform("PUT", p, n); }
int HTTPClient::PATCH(const uint8_t* p, size_t n) { return perform("PATCH", p, n); }
int HTTPClient::sendRequest(const char* type, const String& payload) {
  return perform(type, (const uint8_t*)payload.c_str(), payload.length());
}
int HTTPClient::sendRequest(const char* type, const uint8_t* p, size_t n) { return perform(type, p, n); }

int HTTPClient::perform(const char* method, const uint8_t* payload, size_t size) {
  if (!begun_) return HTTPC_ERROR_NOT_CONNECTED;
  body_.clear();
  body_taken_ = false;
  location_ = "";
  CURL* c = curl_easy_init();
  if (!c) return HTTPC_ERROR_TOO_LESS_RAM;

  curl_easy_setopt(c, CURLOPT_SHARE, share());
  curl_easy_setopt(c, CURLOPT_URL, url_.c_str());
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent_.c_str());
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)(connect_timeout_ms_ > 0 ? connect_timeout_ms_ : 5000));
  // The ESP timeout is per read; a whole request gets a generous multiple.
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, (long)((timeout_ms_ + 999) / 1000));
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeout_ms_ * 12 + 30000L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &body_);
  HeaderCtx hctx{&collect_, &location_};
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, &hctx);
  curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  if (follow_ != HTTPC_DISABLE_FOLLOW_REDIRECTS) {
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
  }
  if (insecure_) {
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  if (!auth_user_.empty() || !auth_pass_.empty()) {
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(c, CURLOPT_USERNAME, auth_user_.c_str());
    curl_easy_setopt(c, CURLOPT_PASSWORD, auth_pass_.c_str());
  }

  struct curl_slist* hdrs = nullptr;
  if (!auth_raw_.empty()) hdrs = curl_slist_append(hdrs, ("Authorization: Basic " + auth_raw_).c_str());
  for (auto& kv : req_headers_) hdrs = curl_slist_append(hdrs, (kv.first + ": " + kv.second).c_str());
  hdrs = curl_slist_append(hdrs, "Expect:");
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

  if (strcmp(method, "GET") == 0) {
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
  } else if (strcmp(method, "HEAD") == 0) {
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
  } else {
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    if (payload && size) {
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, (const char*)payload);
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)size);
    } else if (strcmp(method, "DELETE") != 0) {
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, "");
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, 0L);
    }
  }

  CURLcode rc = curl_easy_perform(c);
  long code = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(c);

  int ret;
  if (rc == CURLE_OK) {
    ret = (int)code;
  } else {
    switch (rc) {
      case CURLE_COULDNT_RESOLVE_HOST:
      case CURLE_COULDNT_CONNECT: ret = HTTPC_ERROR_CONNECTION_REFUSED; break;
      case CURLE_OPERATION_TIMEDOUT: ret = code ? HTTPC_ERROR_READ_TIMEOUT : HTTPC_ERROR_CONNECTION_REFUSED; break;
      case CURLE_SEND_ERROR: ret = HTTPC_ERROR_SEND_PAYLOAD_FAILED; break;
      case CURLE_RECV_ERROR:
      case CURLE_PARTIAL_FILE: ret = HTTPC_ERROR_CONNECTION_LOST; break;
      case CURLE_GOT_NOTHING:
      case CURLE_WEIRD_SERVER_REPLY: ret = HTTPC_ERROR_NO_HTTP_SERVER; break;
      default: ret = HTTPC_ERROR_CONNECTION_REFUSED; break;
    }
  }
  _returnCode = ret;
  size_ = _size = (ret > 0) ? (int)body_.size() : -1;
  if (client_) client_->setBody(body_);
  return ret;
}

WiFiClient& HTTPClient::getStream() { return *client_; }

String HTTPClient::getString() {
  // Whatever getStream() has not consumed yet.
  if (!client_ || !client_->hasBody()) return String();
  String s;
  s.reserve((size_t)client_->available());
  char buf[4096];
  while (client_->available() > 0) {
    int n = client_->read((uint8_t*)buf, sizeof(buf));
    if (n <= 0) break;
    s.concat(buf, (size_t)n);
  }
  return s;
}

int HTTPClient::writeToStream(Stream* s) {
  if (!s) return HTTPC_ERROR_NO_STREAM;
  String b = getString();
  return (int)s->write((const uint8_t*)b.c_str(), b.length());
}

String HTTPClient::errorToString(int e) {
  switch (e) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return "connection refused";
    case HTTPC_ERROR_SEND_HEADER_FAILED: return "send header failed";
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "send payload failed";
    case HTTPC_ERROR_NOT_CONNECTED: return "not connected";
    case HTTPC_ERROR_CONNECTION_LOST: return "connection lost";
    case HTTPC_ERROR_NO_STREAM: return "no stream";
    case HTTPC_ERROR_NO_HTTP_SERVER: return "no HTTP server";
    case HTTPC_ERROR_TOO_LESS_RAM: return "not enough ram";
    case HTTPC_ERROR_ENCODING: return "Transfer-Encoding not supported";
    case HTTPC_ERROR_STREAM_WRITE: return "Stream write error";
    case HTTPC_ERROR_READ_TIMEOUT: return "read Timeout";
    default: return String();
  }
}
