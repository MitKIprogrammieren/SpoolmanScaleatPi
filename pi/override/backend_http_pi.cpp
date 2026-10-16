// Raspberry Pi version of src/services/backend_http.cpp.
//
// On the ESP this file pools one TLS connection by hand, because a handshake
// costs seconds there. libcurl (behind HTTPClient on the Pi) keeps
// connections and TLS sessions by itself, so what is left is the trust setting
// and the keep-alive preference the settings screen shows. ChunkedStream is
// copied unchanged: bambu_catalog.cpp still uses it.

#include "services/backend_http.h"

#include <strings.h>

#include "services/prefs_store.h"

#define KEEP_MINUTES_DEFAULT 5

static bool s_insecure = false;
static uint8_t s_keep_min = KEEP_MINUTES_DEFAULT;

bool backendUrlIsHttps(const char* url) { return url && strncasecmp(url, "https://", 8) == 0; }

bool backendTlsInsecure() { return s_insecure; }

void backendSetTlsInsecure(bool on) {
  s_insecure = on;
  prefsPutBool("tls_insec", on);
}

uint8_t backendKeepMinutes() { return s_keep_min; }

void backendSetKeepMinutes(uint8_t minutes) {
  if (minutes != 1 && minutes != 5 && minutes != 30) minutes = KEEP_MINUTES_DEFAULT;
  s_keep_min = minutes;
  prefsPutUChar("tls_keep", minutes);
}

void backendTlsLoad() {
  s_insecure = prefsGetBool("tls_insec", false);
  const uint8_t k = prefsGetUChar("tls_keep", KEEP_MINUTES_DEFAULT);
  s_keep_min = (k == 1 || k == 5 || k == 30) ? k : KEEP_MINUTES_DEFAULT;
}

void backendConnTick() {}
void backendConnClose() {}

bool BackendHttp::begin(const String& url) {
  tls_ = WiFiClientSecure();
  if (s_insecure) tls_.setInsecure();
  return HTTPClient::begin(tls_, url);
}

// ---- ChunkedStream, unchanged from the ESP build ------------------------------
int ChunkedStream::nextByte() {
  char c;
  return (in_ && in_->readBytes(&c, 1) == 1) ? (uint8_t)c : -1;
}

bool ChunkedStream::nextChunk() {
  // The CRLF that ends the chunk before, then "<hex size>[;ext]\r\n".
  long size = 0;
  int digits = 0;
  bool ext = false;
  for (int guard = 0; guard < 80; guard++) {
    const int c = nextByte();
    if (c < 0) return false;
    if (c == '\n') {
      if (digits == 0) continue;            // the CRLF after the previous chunk
      break;
    }
    if (c == '\r' || ext) continue;
    if (c == ';') { ext = true; continue; }
    int v = -1;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    if (v < 0) return false;
    size = size * 16 + v;
    digits++;
  }
  if (digits == 0) return false;
  left_ = size;
  if (size == 0) {
    // The last chunk; its trailer ends with an empty line. Read as far as it
    // is already there, so a kept connection starts clean; finish() waits
    // for the rest.
    last_ = true;
    for (int guard = 0; guard < 4 && !clean_ && in_->available() > 0; guard++) {
      trailerByte(nextByte());
    }
    done_ = true;
    return false;
  }
  return true;
}

void ChunkedStream::trailerByte(int c) {
  if (c < 0 || c == '\r') return;
  if (c == '\n') {
    if (line_ == 0) clean_ = true;   // the empty line: the answer is over
    line_ = 0;
    return;
  }
  if (line_ < 255) line_++;
}

bool ChunkedStream::finish(size_t max_bytes, uint32_t wait_ms) {
  if (!in_) return false;
  // Every byte read here waits no longer than the whole drain may take.
  const unsigned long keep = in_->getTimeout();
  in_->setTimeout(wait_ms);
  const unsigned long t0 = millis();
  for (size_t n = 0; !clean_ && n < max_bytes && millis() - t0 < wait_ms; n++) {
    if (!last_) {
      if (done_) break;              // ended on data that made no sense
      read();
    } else {
      const int c = nextByte();
      if (c < 0) break;
      trailerByte(c);
    }
  }
  in_->setTimeout(keep);
  return clean_;
}

int ChunkedStream::read() {
  if (peeked_ >= 0) { const int c = peeked_; peeked_ = -1; return c; }
  if (done_) return -1;
  if (left_ <= 0 && !nextChunk()) { done_ = true; return -1; }
  const int c = nextByte();
  if (c < 0) { done_ = true; return -1; }
  left_--;
  return c;
}

int ChunkedStream::peek() {
  if (peeked_ < 0) peeked_ = read();
  return peeked_;
}

int ChunkedStream::available() {
  if (peeked_ >= 0) return 1;
  if (done_ || !in_) return 0;
  const int raw = in_->available();
  if (left_ > 0) return raw < left_ ? raw : (int)left_;
  return raw > 0 ? 1 : 0;   // a size line is waiting; read() will tell
}

