#include "WebServer.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define WS_READ_TIMEOUT_MS 3000
#define WS_MAX_BODY (16u * 1024 * 1024)

namespace {
const char* reason(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "";
  }
}

HTTPMethod methodOf(const std::string& m) {
  if (m == "GET") return HTTP_GET;
  if (m == "POST") return HTTP_POST;
  if (m == "PUT") return HTTP_PUT;
  if (m == "PATCH") return HTTP_PATCH;
  if (m == "DELETE") return HTTP_DELETE;
  if (m == "OPTIONS") return HTTP_OPTIONS;
  if (m == "HEAD") return HTTP_HEAD;
  return HTTP_ANY;
}

std::string lower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}
}  // namespace

void WebServer::begin() {
  stop();
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port_);
  if (bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(fd, 16) != 0) {
    fprintf(stderr, "WebServer: cannot listen on port %d (%s). Port 80 needs "
                    "CAP_NET_BIND_SERVICE; the systemd unit grants it.\n", port_, strerror(errno));
    ::close(fd);
    return;
  }
  listen_fd_ = fd;
}

void WebServer::stop() {
  if (listen_fd_ >= 0) ::close(listen_fd_);
  listen_fd_ = -1;
  client_.stop();
}

void WebServer::on(const String& uri, HTTPMethod method, THandlerFunction fn, THandlerFunction ufn) {
  routes_.push_back({uri.c_str(), method, fn, ufn});
}

std::string WebServer::urlDecode(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else o += s[i];
  }
  return o;
}

void WebServer::parseQuery(const std::string& q) {
  size_t p = 0;
  while (p <= q.size()) {
    size_t e = q.find('&', p);
    if (e == std::string::npos) e = q.size();
    std::string kv = q.substr(p, e - p);
    if (!kv.empty()) {
      size_t eq = kv.find('=');
      if (eq == std::string::npos) args_.emplace_back(urlDecode(kv), "");
      else args_.emplace_back(urlDecode(kv.substr(0, eq)), urlDecode(kv.substr(eq + 1)));
    }
    p = e + 1;
  }
}

static bool readSome(WiFiClient& c, std::string& buf, unsigned long deadline) {
  char tmp[4096];
  for (;;) {
    int n = c.read((uint8_t*)tmp, sizeof(tmp));
    if (n > 0) { buf.append(tmp, (size_t)n); return true; }
    if (!c.connected() || millis() > deadline) return false;
    struct pollfd p = {c.fd(), POLLIN, 0};
    poll(&p, 1, 50);
  }
}

bool WebServer::readRequest() {
  const unsigned long deadline = millis() + WS_READ_TIMEOUT_MS;
  std::string buf;
  size_t hdr_end;
  while ((hdr_end = buf.find("\r\n\r\n")) == std::string::npos) {
    if (buf.size() > 64 * 1024 || !readSome(client_, buf, deadline)) return false;
  }
  std::string head = buf.substr(0, hdr_end);
  std::string rest = buf.substr(hdr_end + 4);

  size_t le = head.find("\r\n");
  std::string reqline = head.substr(0, le);
  size_t s1 = reqline.find(' '), s2 = reqline.rfind(' ');
  if (s1 == std::string::npos || s2 == s1) return false;
  method_ = methodOf(reqline.substr(0, s1));
  std::string target = reqline.substr(s1 + 1, s2 - s1 - 1);

  args_.clear();
  req_headers_.clear();
  size_t qm = target.find('?');
  uri_ = urlDecode(target.substr(0, qm));
  if (qm != std::string::npos) parseQuery(target.substr(qm + 1));

  size_t content_len = 0;
  std::string ctype;
  size_t p = le == std::string::npos ? head.size() : le + 2;
  while (p < head.size()) {
    size_t e = head.find("\r\n", p);
    if (e == std::string::npos) e = head.size();
    std::string line = head.substr(p, e - p);
    p = e + 2;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value[0] == ' ') value.erase(0, 1);
    std::string ln = lower(name);
    if (ln == "content-length") content_len = (size_t)strtoul(value.c_str(), nullptr, 10);
    if (ln == "content-type") ctype = value;
    req_headers_.emplace_back(name, value);
  }
  req_content_length_ = content_len;
  if (content_len > WS_MAX_BODY) return false;
  while (rest.size() < content_len) {
    if (!readSome(client_, rest, deadline + 10000)) return false;
  }
  raw_body_ = rest.substr(0, content_len);

  const std::string lct = lower(ctype);
  if (lct.find("application/x-www-form-urlencoded") != std::string::npos) {
    parseQuery(raw_body_);
  } else if (lct.find("multipart/form-data") != std::string::npos) {
    // Handled in dispatch() once the route is known.
  } else if (!raw_body_.empty()) {
    args_.emplace_back("plain", raw_body_);
  }
  if (lct.find("multipart/form-data") != std::string::npos) {
    size_t b = ctype.find("boundary=");
    if (b != std::string::npos) req_headers_.emplace_back("X-Multipart-Boundary", ctype.substr(b + 9));
  }
  return true;
}

bool WebServer::parseMultipart(const std::string& body, const std::string& boundary_in, const Route* r) {
  std::string boundary = boundary_in;
  if (!boundary.empty() && boundary.front() == '"') boundary = boundary.substr(1, boundary.size() - 2);
  const std::string delim = "--" + boundary;
  size_t p = body.find(delim);
  while (p != std::string::npos) {
    p += delim.size();
    if (body.compare(p, 2, "--") == 0) break;
    p += 2;   // CRLF
    size_t he = body.find("\r\n\r\n", p);
    if (he == std::string::npos) break;
    std::string hdrs = body.substr(p, he - p);
    size_t data_start = he + 4;
    size_t next = body.find("\r\n" + delim, data_start);
    if (next == std::string::npos) break;
    std::string data = body.substr(data_start, next - data_start);
    std::string name, filename, type;
    size_t np = hdrs.find("name=\"");
    if (np != std::string::npos) name = hdrs.substr(np + 6, hdrs.find('"', np + 6) - np - 6);
    size_t fp = hdrs.find("filename=\"");
    if (fp != std::string::npos) filename = hdrs.substr(fp + 10, hdrs.find('"', fp + 10) - fp - 10);
    size_t tp = lower(hdrs).find("content-type:");
    if (tp != std::string::npos) {
      size_t te = hdrs.find("\r\n", tp);
      type = hdrs.substr(tp + 13, te == std::string::npos ? std::string::npos : te - tp - 13);
      while (!type.empty() && type[0] == ' ') type.erase(0, 1);
    }
    if (fp == std::string::npos) {
      args_.emplace_back(name, data);
    } else if (r && r->ufn) {
      HTTPUpload& u = *upload_;
      u.filename = filename.c_str();
      u.name = name.c_str();
      u.type = type.c_str();
      u.totalSize = 0;
      u.currentSize = 0;
      u.status = UPLOAD_FILE_START;
      r->ufn();
      size_t off = 0;
      while (off < data.size()) {
        size_t n = data.size() - off < HTTP_UPLOAD_BUFLEN ? data.size() - off : HTTP_UPLOAD_BUFLEN;
        memcpy(u.buf, data.data() + off, n);
        u.currentSize = n;
        u.status = UPLOAD_FILE_WRITE;
        r->ufn();
        u.totalSize += n;
        off += n;
      }
      u.currentSize = 0;
      u.status = UPLOAD_FILE_END;
      r->ufn();
    }
    p = next + 2;
  }
  return true;
}

void WebServer::dispatch() {
  resp_headers_.clear();
  content_length_ = CONTENT_LENGTH_NOT_SET;
  chunked_ = false;
  headers_sent_ = false;

  const Route* match = nullptr;
  for (auto& r : routes_) {
    if (r.uri != uri_) continue;
    if (r.method != HTTP_ANY && r.method != method_) continue;
    match = &r;
    break;
  }
  std::string boundary = header("X-Multipart-Boundary").c_str();
  if (!boundary.empty()) parseMultipart(raw_body_, boundary, match);

  if (match && match->fn) match->fn();
  else if (not_found_) not_found_();
  else send(404, "text/plain", "Not found");

  if (chunked_) sendRaw("0\r\n\r\n");
  else if (!headers_sent_) send(500, "text/plain", "");
}

void WebServer::handleClient() {
  if (listen_fd_ < 0) return;
  // Serve every connection that is waiting, one request each.
  for (int round = 0; round < 4; round++) {
    int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return;
    client_ = WiFiClient(fd);
    client_.setTimeout(HTTP_MAX_SEND_WAIT);
    if (readRequest()) dispatch();
    client_.stop();
  }
}

String WebServer::arg(const String& name) const {
  for (auto& kv : args_) if (kv.first == name.c_str()) return String(kv.second);
  return String();
}
String WebServer::arg(int i) const { return i >= 0 && i < (int)args_.size() ? String(args_[i].second) : String(); }
String WebServer::argName(int i) const { return i >= 0 && i < (int)args_.size() ? String(args_[i].first) : String(); }
bool WebServer::hasArg(const String& name) const {
  for (auto& kv : args_) if (kv.first == name.c_str()) return true;
  return false;
}

void WebServer::collectHeaders(const char* keys[], size_t count) {
  collect_keys_.clear();
  for (size_t i = 0; i < count; i++) collect_keys_.push_back(keys[i]);
}
String WebServer::header(const String& name) const {
  for (auto& kv : req_headers_) if (strcasecmp(kv.first.c_str(), name.c_str()) == 0) return String(kv.second);
  return String();
}
String WebServer::header(int i) const { return i >= 0 && i < (int)req_headers_.size() ? String(req_headers_[i].second) : String(); }
String WebServer::headerName(int i) const { return i >= 0 && i < (int)req_headers_.size() ? String(req_headers_[i].first) : String(); }
bool WebServer::hasHeader(const String& name) const {
  for (auto& kv : req_headers_) if (strcasecmp(kv.first.c_str(), name.c_str()) == 0) return true;
  return false;
}

bool WebServer::authenticate(const char*, const char*) { return false; }
void WebServer::requestAuthentication(int, const char* realm, const String& fail) {
  sendHeader("WWW-Authenticate", String("Basic realm=\"") + (realm ? realm : "Login Required") + "\"");
  send(401, "text/html", fail);
}

void WebServer::sendHeader(const String& name, const String& value, bool first) {
  auto kv = std::make_pair(std::string(name.c_str()), std::string(value.c_str()));
  if (first) resp_headers_.insert(resp_headers_.begin(), kv);
  else resp_headers_.push_back(kv);
}

void WebServer::sendRaw(const std::string& s) { client_.write((const uint8_t*)s.data(), s.size()); }

void WebServer::startResponse(int code, const char* type, size_t len) {
  std::string h = "HTTP/1.1 " + std::to_string(code) + " " + reason(code) + "\r\n";
  if (type && *type) h += std::string("Content-Type: ") + type + "\r\n";
  if (len == CONTENT_LENGTH_UNKNOWN) {
    chunked_ = true;
    h += "Transfer-Encoding: chunked\r\n";
  } else {
    h += "Content-Length: " + std::to_string(len) + "\r\n";
  }
  if (cors_) h += "Access-Control-Allow-Origin: *\r\n";
  for (auto& kv : resp_headers_) h += kv.first + ": " + kv.second + "\r\n";
  h += "Connection: close\r\n\r\n";
  sendRaw(h);
  headers_sent_ = true;
  resp_headers_.clear();
}

void WebServer::send(int code, const char* type, const String& content) {
  send(code, type, (const uint8_t*)content.c_str(), content.length());
}

void WebServer::send(int code, const char* type, const uint8_t* content, size_t len) {
  if (headers_sent_) return;
  // setContentLength() before send() decides the length; the body passed here
  // is then only the first part (or nothing).
  size_t total = (content_length_ == CONTENT_LENGTH_NOT_SET) ? len : content_length_;
  startResponse(code, type ? type : "text/html", total);
  if (len) sendContent((const char*)content, len);
}

void WebServer::sendContent(const char* content, size_t len) {
  if (!headers_sent_) {
    startResponse(200, "text/html", content_length_ == CONTENT_LENGTH_NOT_SET ? CONTENT_LENGTH_UNKNOWN : content_length_);
  }
  if (chunked_) {
    if (len == 0) {
      sendRaw("0\r\n\r\n");
      chunked_ = false;
      return;
    }
    char sz[24];
    snprintf(sz, sizeof(sz), "%zx\r\n", len);
    sendRaw(sz);
    client_.write((const uint8_t*)content, len);
    sendRaw("\r\n");
  } else if (len) {
    client_.write((const uint8_t*)content, len);
  }
}
