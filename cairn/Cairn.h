// Cairn.h - LODESTAR CAIRN: a field-station layer on top of LODESTAR NOVA (header-only, no Arduino deps).
//
// CAIRN adds, to the NOVA companion firmware:
//   * a station ID ("LC-" plus 8 letters/digits) taken from the radio's own public key;
//   * a journal: every message and every advert the radio hears, one JSON object per line, kept on the
//     radio's flash AND streamed live to the dashboard page;
//   * the check-in message "LC-xxxxxxxx Reporting at: <lat>,<lon>";
//   * the `cairn` text commands (the glue in MyMesh.cpp does the flash and radio work);
//   * (1.1.0) Standard / Disaster modes with the highest transmit power the rules allow, a battery-aware
//     power policy, a help beacon, status and map-mark messages, an automatic check-in timer, and
//     add / remove for chat channels.
//
// Engineered by Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS), with MeshCore community
// guidance and assistance.
//
// Host-side tests:  g++ -std=c++17 -Wall -Wextra -Icairn cairn/test/test_cairn.cpp -o t && ./t
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define CAIRN_VERSION  "1.2.0"
#define CAIRN_LINE     640       // longest journal line, bytes (a 160-character message can need ~480)
#define CAIRN_SAY_MAX  140       // longest message the dashboard can send (MeshCore text limits are about this)
#define CAIRN_RING     24        // lines held in RAM between the radio and the flash / USB writers

namespace cairn {

// ---- the station ID ---------------------------------------------------------------------------
// "LC-" plus 8 characters taken from the LAST 5 BYTES (40 bits) of the radio's own public key, so the
// ID is unique for practical purposes, needs no database, and can always be rebuilt from the radio.
// 32 characters: no I, O, 0 or 1, so an ID read out loud or off a screen is hard to get wrong.
// 32^8 = 1,099,511,627,776 possible IDs.
static const char ID_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
#define CAIRN_ID_LEN 11   // "LC-" + 8

// `key5` must hold 5 bytes (the last 5 of the public key). `out` must hold 12 bytes ("LC-" + 8 + NUL).
static inline void makeId(const uint8_t* key5, char* out) {
  out[0] = 'L'; out[1] = 'C'; out[2] = '-';
  uint64_t v = 0;
  for (int i = 0; i < 5; i++) v = (v << 8) | key5[i];
  for (int i = 0; i < 8; i++) out[3 + i] = ID_CHARS[(v >> (35 - 5 * i)) & 31];
  out[CAIRN_ID_LEN] = 0;
}

static inline bool validId(const char* s) {
  if (!s || strlen(s) != CAIRN_ID_LEN || s[0] != 'L' || s[1] != 'C' || s[2] != '-') return false;
  for (int i = 3; i < CAIRN_ID_LEN; i++) if (!memchr(ID_CHARS, s[i], 32) || s[i] == 0) return false;
  return true;
}

// degrees * 1e6 -> "-95.36980" (five decimals, about one metre), with no floating point.
static inline void fmtDeg(char* out, size_t cap, int32_t e6) {
  bool neg = e6 < 0;
  int64_t a = neg ? -(int64_t)e6 : (int64_t)e6;
  a = (a + 5) / 10;
  snprintf(out, cap, "%s%ld.%05ld", neg ? "-" : "", (long)(a / 100000), (long)(a % 100000));
}

// Every station message has the same shape:  <ID> <WHAT> at: <lat>,<lon>   (or "... at: no GPS fix").
//   "LC-K7M2P4XQ Reporting at: 29.76043,-95.36980"              the check-in ("I'm OK")
//   "LC-K7M2P4XQ NEEDS HELP at: 29.76043,-95.36980"             the help beacon
//   "LC-K7M2P4XQ STATUS NEED WATER at: 29.76043,-95.36980"      a status update
//   "LC-K7M2P4XQ MARK ROAD BLOCKED at: 29.76043,-95.36980"      a pin on the map
//   "LC-K7M2P4XQ LOW BATTERY 14% at: 29.76043,-95.36980"        the radio's battery is low
// Returns the length, or 0 if `cap` is too small.
static inline size_t posMessage(const char* id, const char* what, bool fix, int32_t lat_e6, int32_t lon_e6, char* out, size_t cap) {
  int n;
  if (fix) {
    char a[24], b[24];
    fmtDeg(a, sizeof(a), lat_e6); fmtDeg(b, sizeof(b), lon_e6);
    n = snprintf(out, cap, "%s %s at: %s,%s", id, what, a, b);
  } else {
    n = snprintf(out, cap, "%s %s at: no GPS fix", id, what);
  }
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

// "LC-K7M2P4XQ Reporting at: 29.76043,-95.36980" - or, with no GPS fix, "LC-K7M2P4XQ Reporting at: no GPS fix".
static inline size_t okMessage(const char* id, bool fix, int32_t lat_e6, int32_t lon_e6, char* out, size_t cap) {
  return posMessage(id, "Reporting", fix, lat_e6, lon_e6, out, cap);
}

// The one-tap status updates (1..8) and map marks (1..7). The dashboard shows the same words.
#define CAIRN_NSTATUS 8
#define CAIRN_NMARK   7
static const char* const STATUS_LABEL[CAIRN_NSTATUS + 1] = { "", "NEED MEDICAL", "NEED WATER", "NEED FOOD", "NEED SHELTER", "NEED TRANSPORT", "EVACUATING", "STAYING PUT", "CAN HELP" };
static const char* const MARK_LABEL[CAIRN_NMARK + 1]     = { "", "WATER", "SHELTER", "MEDICAL AID", "HAZARD", "MEETING POINT", "ROAD BLOCKED", "SUPPLIES" };

static inline size_t helpMessage(const char* id, bool fix, int32_t lat, int32_t lon, char* out, size_t cap) {
  return posMessage(id, "NEEDS HELP", fix, lat, lon, out, cap);
}
static inline size_t statusMessage(const char* id, int code, bool fix, int32_t lat, int32_t lon, char* out, size_t cap) {
  if (code < 1 || code > CAIRN_NSTATUS) return 0;
  char w[40]; snprintf(w, sizeof(w), "STATUS %s", STATUS_LABEL[code]);
  return posMessage(id, w, fix, lat, lon, out, cap);
}
static inline size_t markMessage(const char* id, int kind, bool fix, int32_t lat, int32_t lon, char* out, size_t cap) {
  if (kind < 1 || kind > CAIRN_NMARK) return 0;
  char w[40]; snprintf(w, sizeof(w), "MARK %s", MARK_LABEL[kind]);
  return posMessage(id, w, fix, lat, lon, out, cap);
}
static inline size_t lowBatMessage(const char* id, int pct, bool fix, int32_t lat, int32_t lon, char* out, size_t cap) {
  char w[40]; snprintf(w, sizeof(w), "LOW BATTERY %d%%", pct < 0 ? 0 : pct > 100 ? 100 : pct);
  return posMessage(id, w, fix, lat, lon, out, cap);
}

// ---- journal lines (JSON, one object per line, no newline inside) ---------------------------------
struct W {   // bounded writer; once anything does not fit the whole record is refused
  char* b; size_t cap, n; bool ok;
  W(char* buf, size_t c) : b(buf), cap(c), n(0), ok(c > 1) { if (ok) b[0] = 0; }
  void f(const char* fmt, ...) {
    if (!ok) return;
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(b + n, cap - n, fmt, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= cap - n) { ok = false; return; }
    n += (size_t)r;
  }
  // A JSON string. Quotes and backslashes are escaped, a newline becomes \n, other control
  // characters become a space. Bytes above 127 (UTF-8) pass through untouched.
  void str(const char* t) {
    if (!ok) return;
    if (n + 1 >= cap) { ok = false; return; }
    b[n++] = '"';
    for (; t && *t; t++) {
      unsigned char c = (unsigned char)*t;
      const char* e = nullptr; char one[2] = {0, 0};
      if (c == '"') e = "\\\"";
      else if (c == '\\') e = "\\\\";
      else if (c == '\n') e = "\\n";
      else if (c == '\t') e = "\\t";
      else if (c < 32 || c == 127) { one[0] = ' '; e = one; }
      else { one[0] = (char)c; e = one; }
      size_t l = strlen(e);
      if (n + l + 2 >= cap) { ok = false; return; }
      memcpy(b + n, e, l); n += l;
    }
    b[n++] = '"'; b[n] = 0;
  }
  size_t done() { if (!ok || n + 2 >= cap) return 0; b[n] = 0; return n; }
};

// kind: 'c' channel message, 'd' direct message, 'r' room-server post, 'x' reply to a command we sent.
// `epoch` is the radio's clock (it may be wrong if nobody has set it); `up` is seconds since boot.
// `hops` is -1 when unknown (a direct, non-flooded packet).
static inline size_t recMsg(char* out, size_t cap, uint32_t epoch, uint32_t up, char kind, const char* from,
                            const char* chan, float snr, int hops, const char* text) {
  W w(out, cap);
  w.f("{\"r\":\"m\",\"t\":%lu,\"u\":%lu,\"k\":\"%c\",\"f\":", (unsigned long)epoch, (unsigned long)up, kind);
  w.str(from ? from : "");
  w.f(",\"c\":"); w.str(chan ? chan : "");
  w.f(",\"s\":%.1f,\"h\":%d,\"x\":", (double)snr, hops);
  w.str(text ? text : "");
  w.f("}");
  return w.done();
}

// An advert heard. type: 1 companion, 2 repeater, 3 room server, 4 sensor. key: first 6 bytes of the
// public key. `has_adv`: the node announced a position (degrees * 1e6). `has_here`: this radio had a fix.
static inline size_t recAdv(char* out, size_t cap, uint32_t epoch, uint32_t up, int type, const char* name,
                            const uint8_t* key6, int hops, float snr, int rssi, bool has_adv, int32_t alat,
                            int32_t alon, bool has_here, int32_t hlat, int32_t hlon) {
  W w(out, cap);
  char kh[13]; for (int i = 0; i < 6; i++) snprintf(kh + i * 2, 3, "%02X", key6[i]);
  w.f("{\"r\":\"a\",\"t\":%lu,\"u\":%lu,\"ty\":%d,\"n\":", (unsigned long)epoch, (unsigned long)up, type);
  w.str(name ? name : "");
  w.f(",\"k\":\"%s\",\"h\":%d,\"s\":%.1f,\"rs\":%d", kh, hops, (double)snr, rssi);
  char a[24], b[24];
  if (has_adv) { fmtDeg(a, sizeof(a), alat); fmtDeg(b, sizeof(b), alon); w.f(",\"la\":%s,\"lo\":%s", a, b); }
  if (has_here) { fmtDeg(a, sizeof(a), hlat); fmtDeg(b, sizeof(b), hlon); w.f(",\"hla\":%s,\"hlo\":%s", a, b); }
  w.f("}");
  return w.done();
}

// A check-in this station sent.
static inline size_t recOwn(char* out, size_t cap, uint32_t epoch, uint32_t up, const char* id, int channel, const char* text) {
  W w(out, cap);
  w.f("{\"r\":\"o\",\"t\":%lu,\"u\":%lu,\"id\":\"%s\",\"ch\":%d,\"x\":", (unsigned long)epoch, (unsigned long)up, id, channel);
  w.str(text ? text : "");
  w.f("}");
  return w.done();
}

// The station started (a gap in the journal between two of these means the power was off).
static inline size_t recBoot(char* out, size_t cap, uint32_t epoch, const char* id, const char* ver) {
  W w(out, cap);
  w.f("{\"r\":\"b\",\"t\":%lu,\"u\":0,\"id\":\"%s\",\"v\":\"%s\"}", (unsigned long)epoch, id, ver);
  return w.done();
}

// ---- the ring between the radio and the writers -----------------------------------------------------
// Two readers (the flash writer and the USB streamer) each keep a Cursor. If a reader falls more than
// CAIRN_RING lines behind, the oldest lines are skipped and counted in `lost`.
struct Cursor { uint32_t next = 0; uint32_t lost = 0; };

class Journal {
public:
  uint32_t total = 0;        // lines accepted since boot
  uint32_t refused = 0;      // lines too long to keep

  bool push(const char* line, bool urgent = false) {
    size_t l = line ? strlen(line) : 0;
    if (l == 0 || l >= CAIRN_LINE) { refused++; return false; }
    memcpy(_r[_seq % CAIRN_RING], line, l + 1);
    _seq++; total++;
    if (urgent) _urgent = true;
    return true;
  }
  // The next unread line for this reader, or nullptr. Does not move the cursor: call advance().
  const char* peek(Cursor& c) const {
    if (_seq - c.next > CAIRN_RING) { c.lost += (_seq - c.next) - CAIRN_RING; c.next = _seq - CAIRN_RING; }
    return c.next == _seq ? nullptr : _r[c.next % CAIRN_RING];
  }
  void advance(Cursor& c) const { if (c.next != _seq) c.next++; }
  uint32_t pending(const Cursor& c) const { uint32_t d = _seq - c.next; return d > CAIRN_RING ? CAIRN_RING : d; }
  bool urgent() const { return _urgent; }
  void clearUrgent() { _urgent = false; }
private:
  char _r[CAIRN_RING][CAIRN_LINE];
  uint32_t _seq = 0;
  bool _urgent = false;
};

// ---- advert de-duplication ---------------------------------------------------------------------------
// The same advert reaches the radio several times by different routes. Log it the first time, and again
// only if it arrives by a shorter route (fewer hops) than before.
class AdvertSeen {
public:
  bool shouldLog(const uint8_t* key6, uint32_t adv_time, int hops) {
    for (int i = 0; i < N; i++) {
      if (_e[i].used && memcmp(_e[i].key, key6, 6) == 0 && _e[i].t == adv_time) {
        if (hops < _e[i].hops) { _e[i].hops = (int8_t)hops; return true; }
        return false;
      }
    }
    Entry& e = _e[_next]; _next = (_next + 1) % N;
    e.used = true; memcpy(e.key, key6, 6); e.t = adv_time; e.hops = (int8_t)(hops > 127 ? 127 : hops);
    return true;
  }
private:
  static const int N = 48;
  struct Entry { bool used = false; uint8_t key[6] = {0}; uint32_t t = 0; int8_t hops = 0; };
  Entry _e[N];
  int _next = 0;
};

// ---- SHA-256 (for hashtag channels) --------------------------------------------------------------------
// MeshCore derives the key of a hashtag channel such as "#water" as the first 16 bytes of SHA-256 of its name,
// so anyone who types the same name gets the same channel. The radio has its own SHA-256; this copy lets the
// PC tests check the derivation against the value MeshCore documents ("#test" -> 9cd8fcf22a47333b591d96a2b848b73f).
static inline void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
  uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
  uint8_t blk[128]; size_t i = 0, rem = len;
  auto R = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  auto run = [&](const uint8_t* b) {
    uint32_t w[64];
    for (int t = 0; t < 16; t++) w[t] = ((uint32_t)b[t*4] << 24) | ((uint32_t)b[t*4+1] << 16) | ((uint32_t)b[t*4+2] << 8) | b[t*4+3];
    for (int t = 16; t < 64; t++) { uint32_t s0 = R(w[t-15],7) ^ R(w[t-15],18) ^ (w[t-15] >> 3), s1 = R(w[t-2],17) ^ R(w[t-2],19) ^ (w[t-2] >> 10); w[t] = w[t-16] + s0 + w[t-7] + s1; }
    uint32_t a=h[0],b2=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int t = 0; t < 64; t++) {
      uint32_t S1 = R(e,6) ^ R(e,11) ^ R(e,25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[t] + w[t];
      uint32_t S0 = R(a,2) ^ R(a,13) ^ R(a,22), mj = (a & b2) ^ (a & c) ^ (b2 & c), t2 = S0 + mj;
      hh=g; g=f; f=e; e=d+t1; d=c; c=b2; b2=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b2; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  };
  while (rem >= 64) { run(msg + i); i += 64; rem -= 64; }
  memset(blk, 0, sizeof(blk)); if (rem) memcpy(blk, msg + i, rem);
  blk[rem] = 0x80;
  size_t total = (rem + 9 <= 64) ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8u;
  for (int k = 0; k < 8; k++) blk[total - 1 - k] = (uint8_t)(bits >> (8 * k));
  run(blk); if (total == 128) run(blk + 64);
  for (int k = 0; k < 8; k++) { out[k*4] = (uint8_t)(h[k] >> 24); out[k*4+1] = (uint8_t)(h[k] >> 16); out[k*4+2] = (uint8_t)(h[k] >> 8); out[k*4+3] = (uint8_t)h[k]; }
}

// ---- chat channels: names, keys, add / remove -----------------------------------------------------------
// The built-in Public channel's 16-byte key (base64 izOH6cXN6mrJ5e26oRXNcg== in MeshCore).
static const uint8_t PUBLIC_KEY16[16] = { 0x8b,0x33,0x87,0xe9,0xc5,0xcd,0xea,0x6a,0xc9,0xe5,0xed,0xba,0xa1,0x15,0xcd,0x72 };
#define CAIRN_MAX_CHANNELS 40
#define CAIRN_CHAN_NAME    32      // bytes in MeshCore's name field, including the end mark

// A channel name people can type and read aloud: 1..31 bytes, no control characters, no quote or backslash
// (they would need escaping in the dashboard's data), not starting or ending with a space.
static inline bool chanNameOk(const char* n) {
  if (!n) return false;
  size_t l = strlen(n);
  if (l < 1 || l >= CAIRN_CHAN_NAME) return false;
  if (n[0] == ' ' || n[l - 1] == ' ') return false;
  for (size_t i = 0; i < l; i++) { unsigned char c = (unsigned char)n[i]; if (c < 32 || c == 127 || c == '"' || c == '\\') return false; }
  return true;
}
// "#Water-Tank" -> "#water-tank". Only a-z, 0-9 and "-" are allowed after the "#" (the same rule the MeshCore app uses).
static inline bool hashtagNormalize(const char* in, char* out, size_t cap) {
  if (!in || in[0] != '#' || cap < 3) return false;
  size_t l = strlen(in);
  if (l < 2 || l >= CAIRN_CHAN_NAME || l + 1 > cap) return false;
  out[0] = '#';
  for (size_t i = 1; i < l; i++) {
    char c = in[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    out[i] = c;
  }
  out[l] = 0;
  return true;
}
static inline void hashtagKey(const char* normalized_name, uint8_t key16[16]) {
  uint8_t h[32]; sha256((const uint8_t*)normalized_name, strlen(normalized_name), h); memcpy(key16, h, 16);
}
static inline bool parseHex16(const char* s, uint8_t out[16]) {
  if (!s || strlen(s) != 32) return false;
  for (int i = 0; i < 32; i++) {
    char c = s[i]; int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (v < 0) return false;
    if (i % 2 == 0) out[i / 2] = (uint8_t)(v << 4); else out[i / 2] |= (uint8_t)v;
  }
  return true;
}
static inline void hex16(const uint8_t k[16], char out[33]) { for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", k[i]); }
// "pub" the built-in Public channel, "tag" a hashtag channel (anyone can join it by name), "key" a private channel.
static inline const char* chanKind(const char* name, const uint8_t key16[16]) {
  if (memcmp(key16, PUBLIC_KEY16, 16) == 0) return "pub";
  if (name && name[0] == '#') { char n[CAIRN_CHAN_NAME]; uint8_t k[16]; if (hashtagNormalize(name, n, sizeof(n)) && strcmp(n, name) == 0) { hashtagKey(n, k); if (memcmp(k, key16, 16) == 0) return "tag"; } }
  return "key";
}

// ---- transmit power: what the rules allow, and what the radio is set to ---------------------------------
// The Heltec V4 radio has a power amplifier after the SX1262 chip. MeshCore's "tx power" setting (-9..22) is the
// level the chip is driven at: 10 gives about 22 dBm at the antenna, 22 gives about 28 dBm (Heltec rates it
// 28 +/- 1 dBm; the MeshCore FAQ lists 22 -> 28 dBm, about 0.5 to 0.6 W).
//
// The US unlicensed rule used here is 47 CFR 15.247(b): at most 1 watt (30 dBm) into the antenna for 902-928 MHz,
// with an antenna of up to 6 dBi; with a higher-gain antenna the power must come down dB for dB. Part 15 has
// no higher "emergency" power: 47 CFR 2.405 lets LICENSED stations use their licence differently in a disaster and
// does not apply to unlicensed devices. So the highest legal power is the same in Standard and Disaster mode.
//
// The estimate below is deliberately cautious: it assumes the amplifier adds 12 dB and saturates at 28 dBm,
// and adds 1 dB for the rated tolerance before comparing with the limit.
namespace power {
static const int SAVER_SETTING = 10;       // about 22 dBm at the antenna
static inline int estDbm(int setting) { int e = setting + 12; return e > 28 ? 28 : e; }
static inline int worstDbm10(int setting) { return (estDbm(setting) + 1) * 10; }       // tenths of a dBm
static inline bool usBand(float mhz) { return mhz >= 902.0f && mhz <= 928.0f; }
static inline int limitDbm10(int ant10) { int ex = ant10 > 60 ? ant10 - 60 : 0; return 300 - ex; }   // tenths of a dBm
// The highest setting (up to the board's own maximum) that stays inside the limit for this antenna. -100 means
// "not in the US band: the US rule does not apply, so do not change the power".
static inline int legalMax(float mhz, int ant10, int hw_max) {
  if (!usBand(mhz)) return -100;
  for (int s = hw_max; s >= -9; s--) if (worstDbm10(s) <= limitDbm10(ant10)) return s;
  return -9;
}
static inline int milliwatts(int dbm) { double v = 1.0; for (int i = 0; i < dbm; i++) v *= 1.2589254117941673; return (int)(v + 0.5); }
}  // namespace power

// ---- settings kept on the radio (/cairn.cfg) ---------------------------------------------------------------
struct Config {
  uint8_t  mode       = 0;     // 0 Standard, 1 Disaster
  uint8_t  power_pref = 0;     // 0 full (highest the rules allow), 1 saver (about 22 dBm)
  uint8_t  ant10      = 20;    // antenna gain in tenths of a dBi (2.0)
  uint16_t auto_min   = 0;     // automatic check-in every N minutes, 0 = off
  uint8_t  beacon_min = 5;     // the help beacon repeats every N minutes
  uint8_t  help_on    = 0;     // the help beacon is running (kept over a restart)
  uint8_t  eco        = 1;     // battery-saving rules on
  uint8_t  lowbat     = 1;     // send a LOW BATTERY message once when the radio's battery runs low
  uint8_t  main_chan  = 0;     // channel used for check-ins, help, status and marks
};
static inline size_t saveConfig(const Config& c, char* out, size_t cap) {
  int n = snprintf(out, cap, "CAIRN2 %d %d %d %d %d %d %d %d %d\n", c.mode, c.power_pref, c.ant10, c.auto_min, c.beacon_min, c.help_on, c.eco, c.lowbat, c.main_chan);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}
// Anything out of range is ignored (the whole line is refused and the defaults stay).
static inline bool loadConfig(const char* t, Config& c) {
  int m, p, a, am, bm, h, e, l, mc;
  if (!t || sscanf(t, "CAIRN2 %d %d %d %d %d %d %d %d %d", &m, &p, &a, &am, &bm, &h, &e, &l, &mc) != 9) return false;
  if (m < 0 || m > 1 || p < 0 || p > 1 || a < 0 || a > 200 || am < 0 || am > 1440 || bm < 1 || bm > 60 || h < 0 || h > 1 || e < 0 || e > 1 || l < 0 || l > 1 || mc < 0 || mc >= CAIRN_MAX_CHANNELS) return false;
  c.mode = (uint8_t)m; c.power_pref = (uint8_t)p; c.ant10 = (uint8_t)a; c.auto_min = (uint16_t)am; c.beacon_min = (uint8_t)bm;
  c.help_on = (uint8_t)h; c.eco = (uint8_t)e; c.lowbat = (uint8_t)l; c.main_chan = (uint8_t)mc;
  return true;
}

// ---- the station -----------------------------------------------------------------------------------
class Cairn {
public:
  char id[CAIRN_ID_LEN + 1] = {0};
  Journal j;
  AdvertSeen seen;
  Cursor to_flash, to_usb;
  Config cfg;

  // What the glue tells the station about the radio, every pass of loop().
  struct Context {
    float freq_mhz = 0;      // the radio's frequency
    int   hw_max = 22;       // the board's highest tx power setting (MAX_LORA_TX_POWER)
    int   setting = 0;       // the tx power setting in use now
    int   batt_pct = -1;     // radio battery percent, -1 when there is no battery reading
    bool  external = false;  // powered from USB or the solar input
  } ctx;

  // Requests set by `cairn ...` text commands. The glue in MyMesh.cpp acts on them and clears them.
  bool want_ok = false;      int ok_channel = 0;
  bool want_dump = false;
  bool want_clear = false;
  int  want_mode = 0;        // 1 = disaster, 2 = standard
  bool want_power = false;   // recompute and apply the transmit power
  bool cfg_dirty = false;    // write /cairn.cfg
  // Messages the dashboard asks the radio to send (`cairn say`, `cairn dm`) and the channel list it asks for.
  bool want_say = false;     int say_channel = 0;
  bool want_dm = false;      uint8_t dm_key[6] = {0};
  char say_text[CAIRN_SAY_MAX + 1] = {0};
  bool want_chans = false;
  uint32_t want_time = 0;    // set the radio's clock to this (seconds since 1970), from the dashboard
  uint32_t last_ok_up = 0;   // seconds since boot when this station last sent a check-in (0 = none)
  // Help, status, marks
  bool want_help_now = false;       // send a help message now (the beacon's first message, or a repeat)
  bool want_help_stop = false;      // the beacon was cancelled: send an "I'm OK" so everyone knows it was cancelled
  int  want_status = 0;             // 1..CAIRN_NSTATUS
  int  want_mark = 0;               // 1..CAIRN_NMARK
  int  help_chan = -1;              // channel the help beacon uses, -1 = the main channel
  int  msg_channel = -1;            // channel for want_status / want_mark / want_ok when given, else cfg.main_chan
  // Channel changes. op: 1 add, 2 new (random key), 3 remove, 4 show key
  int  chan_op = 0; int chan_idx = 0; char chan_name[CAIRN_CHAN_NAME] = {0}; uint8_t chan_key[16] = {0}; bool chan_has_key = false;

  // Kept up to date by the glue, for the status line.
  uint32_t file_bytes = 0;

  // Timers, in seconds since the radio started (0 = not set).
  uint32_t help_next_up = 0, auto_next_up = 0;
  bool eco_low = false;       // battery-saving rules are in force right now
  bool lowbat_sent = false;   // the LOW BATTERY message went out and the battery has not recovered yet
  uint32_t arm_until_ms = 0;  // OLED: "press again to send HELP" window

  bool hasId() const { return validId(id); }
  void setId(const char* s) { if (validId(s)) { memcpy(id, s, CAIRN_ID_LEN); id[CAIRN_ID_LEN] = 0; } }
  bool disaster() const { return cfg.mode == 1; }
  int helpChannel() const { return help_chan >= 0 ? help_chan : cfg.main_chan; }
  int msgChannel() const { return msg_channel >= 0 ? msg_channel : cfg.main_chan; }

  // ---- power policy ----------------------------------------------------------------------------------------
  // The tx power setting the radio should use, or -100 to leave it alone (not the US band).
  int legalSetting() const { return power::legalMax(ctx.freq_mhz, cfg.ant10, ctx.hw_max); }
  int wantedSetting() const {
    int legal = legalSetting();
    if (legal == -100) return -100;
    if (cfg.mode == 1 || cfg.help_on) return legal;                    // Disaster mode and the help beacon: all the power the rules allow
    if (cfg.power_pref == 1 || eco_low) return legal < power::SAVER_SETTING ? legal : power::SAVER_SETTING;
    return legal;                                                      // Standard: the highest the rules allow
  }
  // Battery-saving rules: on at 25% or less on battery, off again at 35% or more (or on external power).
  void updateEco() {
    if (!cfg.eco || ctx.external || ctx.batt_pct < 0) { eco_low = false; return; }
    if (!eco_low && ctx.batt_pct <= 25) eco_low = true;
    else if (eco_low && ctx.batt_pct >= 35) eco_low = false;
  }
  // True once when the battery drops to 15% on battery power; re-arms when it climbs back above 30% or power is plugged in.
  bool lowBatDue() {
    if (ctx.external || ctx.batt_pct < 0 || ctx.batt_pct > 30) { if (ctx.external || ctx.batt_pct > 30) lowbat_sent = false; return false; }
    if (cfg.lowbat && !lowbat_sent && ctx.batt_pct <= 15) { lowbat_sent = true; return true; }
    return false;
  }

  // ---- timers -------------------------------------------------------------------------------------------------
  // Help beacon: sent now, then every cfg.beacon_min minutes until it is cancelled.
  bool helpDue(uint32_t up) const { return cfg.help_on && (help_next_up == 0 || (int32_t)(up - help_next_up) >= 0); }
  void helpSent(uint32_t up) { help_next_up = up + (uint32_t)cfg.beacon_min * 60u; if (help_next_up == 0) help_next_up = 1; }
  // Automatic check-in: every cfg.auto_min minutes (twice as long while the battery is low). Not while the help beacon is running.
  uint32_t autoEvery() const { return (uint32_t)cfg.auto_min * 60u * (eco_low ? 2u : 1u); }
  bool autoDue(uint32_t up) const { return cfg.auto_min > 0 && !cfg.help_on && auto_next_up != 0 && (int32_t)(up - auto_next_up) >= 0; }
  void autoReset(uint32_t up) { auto_next_up = cfg.auto_min ? up + autoEvery() : 0; if (cfg.auto_min && auto_next_up == 0) auto_next_up = 1; }
  uint32_t helpIn(uint32_t up) const { return (cfg.help_on && help_next_up && (int32_t)(help_next_up - up) > 0) ? help_next_up - up : 0; }

  void startHelp(uint32_t up, int ch) {
    (void)up; help_chan = ch;
    cfg.help_on = 1; help_next_up = 0; want_help_now = true; want_power = true; cfg_dirty = true;
  }
  void stopHelp() {
    if (!cfg.help_on) return;
    cfg.help_on = 0; help_next_up = 0; want_help_stop = true; want_power = true; cfg_dirty = true;
  }

  // ---- the JSON status line sent to the dashboard every 5 seconds -----------------------------------------------
  size_t statusJson(char* out, size_t cap, uint32_t up, uint32_t epoch, uint32_t lost) const {
    int legal = legalSetting(), set = ctx.setting, est = power::estDbm(set);
    uint32_t ago = last_ok_up ? (up + 1u - last_ok_up) : 0;
    int n = snprintf(out, cap,
      "{\"cairn\":1,\"id\":\"%s\",\"ver\":\"%s\",\"kb\":%lu,\"lines\":%lu,\"lost\":%lu,\"mode\":\"%s\",\"okago\":%lu,\"now\":%lu,"
      "\"txs\":%d,\"tx\":%d,\"mw\":%d,\"pw\":\"%s\",\"band\":%d,\"ant\":%d.%d,\"legal\":%d,\"help\":%d,\"hnext\":%lu,\"beacon\":%d,"
      "\"auto\":%d,\"eco\":%d,\"ecoon\":%d,\"lowbat\":%d,\"mc\":%d,\"bat\":%d,\"ext\":%d,\"chmax\":%d}",
      hasId() ? id : "", CAIRN_VERSION, (unsigned long)(file_bytes / 1024u), (unsigned long)j.total, (unsigned long)lost,
      cfg.mode == 1 ? "disaster" : "standard", (unsigned long)ago, (unsigned long)epoch,
      set, est, power::milliwatts(est), cfg.power_pref ? "saver" : "full", power::usBand(ctx.freq_mhz) ? 1 : 0, cfg.ant10 / 10, cfg.ant10 % 10,
      legal, cfg.help_on ? 1 : 0, (unsigned long)helpIn(up), cfg.beacon_min, cfg.auto_min, eco_low ? 1 : 0, cfg.eco ? 1 : 0, cfg.lowbat ? 1 : 0,
      cfg.main_chan, ctx.batt_pct, ctx.external ? 1 : 0, CAIRN_MAX_CHANNELS);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
  }

  // `a` is what follows the word "cairn". Fills `reply`.
  void runCommand(const char* a, char* reply, size_t cap, uint32_t up) {
    while (*a == ' ') a++;
    if (*a == 0 || strncmp(a, "status", 6) == 0) {
      snprintf(reply, cap, "LODESTAR CAIRN %s  ID %s  %s mode  journal %lu KB flash, %lu lines since boot, up %lu min",
               CAIRN_VERSION, hasId() ? id : "(none)", cfg.mode == 1 ? "DISASTER" : "STANDARD", (unsigned long)(file_bytes / 1024u), (unsigned long)j.total,
               (unsigned long)(up / 60u));
    } else if (cmd(a, "id")) {
      snprintf(reply, cap, "%s", hasId() ? id : "no ID yet");
    } else if (cmd(a, "ok")) {
      const char* p = a + 2; while (*p == ' ') p++;
      int ch = cfg.main_chan;
      if (*p) {
        char* end; long v = strtol(p, &end, 10);
        if (end == p || *end != 0 || v < 0 || v >= CAIRN_MAX_CHANNELS) { snprintf(reply, cap, "Err - cairn ok [channel number 0-39]"); return; }
        ch = (int)v;
      }
      ok_channel = ch; want_ok = true;
      snprintf(reply, cap, "OK - sending check-in to channel %d", ch);
    } else if (cmd(a, "dump")) {
      want_dump = true;
      snprintf(reply, cap, "OK - sending the journal");
    } else if (cmd(a, "clear")) {
      const char* p = a + 5; while (*p == ' ') p++;
      if (strcmp(p, "yes") == 0) { want_clear = true; snprintf(reply, cap, "OK - journal on the radio will be erased"); }
      else snprintf(reply, cap, "This erases the journal stored on the radio. To do it type: cairn clear yes");
    } else if (cmd(a, "time")) {
      const char* p = a + 4; while (*p == ' ') p++;
      char* end; unsigned long v = strtoul(p, &end, 10);
      if (*p == 0 || *end != 0 || v < 1700000000UL || v > 4102444800UL) { snprintf(reply, cap, "Err - cairn time <seconds since 1970>"); return; }
      want_time = (uint32_t)v;
      snprintf(reply, cap, "OK - radio clock set");
    } else if (cmd(a, "say")) {
      // cairn say <channel 0-39> <text>
      const char* p = a + 3; while (*p == ' ') p++;
      char* end; long ch = strtol(p, &end, 10);
      if (end == p || ch < 0 || ch >= CAIRN_MAX_CHANNELS || (*end != ' ' && *end != 0)) { snprintf(reply, cap, "Err - cairn say <channel 0-39> <text>"); return; }
      p = end; while (*p == ' ') p++;
      size_t n = strlen(p);
      if (n == 0) { snprintf(reply, cap, "Err - nothing to send"); return; }
      if (n > CAIRN_SAY_MAX) { snprintf(reply, cap, "Err - message too long (%u characters, most is %u)", (unsigned)n, (unsigned)CAIRN_SAY_MAX); return; }
      if (want_say || want_dm) { snprintf(reply, cap, "Err - still sending the last message, try again"); return; }
      memcpy(say_text, p, n); say_text[n] = 0; say_channel = (int)ch; want_say = true;
      snprintf(reply, cap, "OK - sending to channel %d", (int)ch);
    } else if (cmd(a, "dm")) {
      // cairn dm <12 hex characters of the contact's key> <text>
      const char* p = a + 2; while (*p == ' ') p++;
      uint8_t k[6]; int i = 0;
      for (; i < 12; i++) {
        char c = p[i]; int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) break;
        if (i % 2 == 0) k[i / 2] = (uint8_t)(v << 4); else k[i / 2] |= (uint8_t)v;
      }
      if (i != 12 || (p[12] != ' ' && p[12] != 0)) { snprintf(reply, cap, "Err - cairn dm <12 hex characters of the key> <text>"); return; }
      p += 12; while (*p == ' ') p++;
      size_t n = strlen(p);
      if (n == 0) { snprintf(reply, cap, "Err - nothing to send"); return; }
      if (n > CAIRN_SAY_MAX) { snprintf(reply, cap, "Err - message too long (%u characters, most is %u)", (unsigned)n, (unsigned)CAIRN_SAY_MAX); return; }
      if (want_say || want_dm) { snprintf(reply, cap, "Err - still sending the last message, try again"); return; }
      memcpy(dm_key, k, 6); memcpy(say_text, p, n); say_text[n] = 0; want_dm = true;
      snprintf(reply, cap, "OK - sending direct message");
    } else if (cmd(a, "chans")) {
      want_chans = true;
      snprintf(reply, cap, "OK - channel list on its way");
    } else if (cmd(a, "chan")) {
      chanCommand(a + 4, reply, cap);
    } else if (cmd(a, "mode")) {
      const char* p = a + 4; while (*p == ' ') p++;
      if (strcmp(p, "disaster") == 0) {
        cfg.mode = 1; want_mode = 1; want_power = true; cfg_dirty = true;
        snprintf(reply, cap, "OK - DISASTER mode: full power, screen off sooner, quiet hours off");
      } else if (strcmp(p, "standard") == 0 || strcmp(p, "normal") == 0) {
        cfg.mode = 0; want_mode = 2; want_power = true; cfg_dirty = true;
        snprintf(reply, cap, "OK - STANDARD mode");
      } else snprintf(reply, cap, "cairn mode standard | disaster (now %s)", cfg.mode == 1 ? "disaster" : "standard");
    } else if (cmd(a, "power")) {
      const char* p = a + 5; while (*p == ' ') p++;
      if (strcmp(p, "full") == 0 || strcmp(p, "max") == 0) { cfg.power_pref = 0; want_power = true; cfg_dirty = true; }
      else if (strcmp(p, "saver") == 0 || strcmp(p, "low") == 0) { cfg.power_pref = 1; want_power = true; cfg_dirty = true; }
      else if (*p != 0 && strcmp(p, "status") != 0) { snprintf(reply, cap, "cairn power full | saver | status"); return; }
      powerReply(reply, cap);
    } else if (cmd(a, "antenna")) {
      const char* p = a + 7; while (*p == ' ') p++;
      char* end; double g = strtod(p, &end);
      if (*p == 0) { snprintf(reply, cap, "antenna gain %d.%d dBi (cairn antenna <dBi>, printed on the antenna's packaging)", cfg.ant10 / 10, cfg.ant10 % 10); return; }
      if (end == p || *end != 0 || g < 0.0 || g > 20.0) { snprintf(reply, cap, "Err - cairn antenna <gain in dBi, 0 to 20>"); return; }
      cfg.ant10 = (uint8_t)(g * 10.0 + 0.5); want_power = true; cfg_dirty = true;
      powerReply(reply, cap);
    } else if (cmd(a, "help")) {
      const char* p = a + 4; while (*p == ' ') p++;
      if (strcmp(p, "off") == 0 || strcmp(p, "stop") == 0) {
        if (!cfg.help_on) { snprintf(reply, cap, "The help beacon is not running"); return; }
        stopHelp(); snprintf(reply, cap, "OK - help beacon stopped; sending I'M OK");
      } else {
        int ch = -1;
        if (*p) { char* end; long v = strtol(p, &end, 10); if (end == p || *end != 0 || v < 0 || v >= CAIRN_MAX_CHANNELS) { snprintf(reply, cap, "cairn help [channel] | cairn help off"); return; } ch = (int)v; }
        startHelp(up, ch);
        snprintf(reply, cap, "OK - HELP beacon ON: NEEDS HELP and your position every %d min until cancelled (cairn help off)", cfg.beacon_min);
      }
    } else if (cmd(a, "beacon")) {
      const char* p = a + 6; while (*p == ' ') p++;
      char* end; long v = strtol(p, &end, 10);
      if (*p == 0 || end == p || *end != 0 || v < 1 || v > 60) { snprintf(reply, cap, "beacon every %d min (cairn beacon <1-60>)", cfg.beacon_min); return; }
      cfg.beacon_min = (uint8_t)v; cfg_dirty = true;
      snprintf(reply, cap, "OK - the help beacon repeats every %d min", cfg.beacon_min);
    } else if (cmd(a, "auto")) {
      const char* p = a + 4; while (*p == ' ') p++;
      if (strcmp(p, "off") == 0) { cfg.auto_min = 0; auto_next_up = 0; cfg_dirty = true; snprintf(reply, cap, "OK - automatic check-in off"); return; }
      char* end; long v = strtol(p, &end, 10);
      if (*p == 0) { if (cfg.auto_min) snprintf(reply, cap, "automatic check-in every %d min (cairn auto <10-1440> | off)", cfg.auto_min); else snprintf(reply, cap, "automatic check-in off (cairn auto <10-1440>)"); return; }
      if (end == p || *end != 0 || v < 10 || v > 1440) { snprintf(reply, cap, "Err - cairn auto <minutes 10-1440> | off"); return; }
      cfg.auto_min = (uint16_t)v; autoReset(up); cfg_dirty = true;
      snprintf(reply, cap, "OK - automatic check-in every %d min", cfg.auto_min);
    } else if (cmd(a, "eco")) {
      const char* p = a + 3; while (*p == ' ') p++;
      if (strcmp(p, "on") == 0) cfg.eco = 1; else if (strcmp(p, "off") == 0) cfg.eco = 0; else { snprintf(reply, cap, "battery saving is %s (cairn eco on | off)", cfg.eco ? "on" : "off"); return; }
      updateEco(); want_power = true; cfg_dirty = true;
      snprintf(reply, cap, "OK - battery saving %s", cfg.eco ? "on: below 25%% on battery, Standard mode uses the saver power and automatic check-ins slow down" : "off");
    } else if (cmd(a, "lowbat")) {
      const char* p = a + 6; while (*p == ' ') p++;
      if (strcmp(p, "on") == 0) cfg.lowbat = 1; else if (strcmp(p, "off") == 0) cfg.lowbat = 0; else { snprintf(reply, cap, "low-battery message is %s (cairn lowbat on | off)", cfg.lowbat ? "on" : "off"); return; }
      cfg_dirty = true; snprintf(reply, cap, "OK - low-battery message %s", cfg.lowbat ? "on" : "off");
    } else if (cmd(a, "main")) {
      const char* p = a + 4; while (*p == ' ') p++;
      char* end; long v = strtol(p, &end, 10);
      if (*p == 0 || end == p || *end != 0 || v < 0 || v >= CAIRN_MAX_CHANNELS) { snprintf(reply, cap, "check-ins and alerts go to channel %d (cairn main <0-39>)", cfg.main_chan); return; }
      cfg.main_chan = (uint8_t)v; cfg_dirty = true;
      snprintf(reply, cap, "OK - check-ins and alerts go to channel %d", (int)v);
    } else if (cmd(a, "ustatus")) {
      // cairn ustatus <1-8> [channel]
      const char* p = a + 7; while (*p == ' ') p++;
      char* end; long v = strtol(p, &end, 10);
      if (end == p || v < 1 || v > CAIRN_NSTATUS || (*end != ' ' && *end != 0)) { snprintf(reply, cap, "Err - cairn ustatus <1-%d> [channel]", CAIRN_NSTATUS); return; }
      int ch = -1; if (!parseChan(end, ch)) { snprintf(reply, cap, "Err - channel must be 0-39"); return; }
      msg_channel = ch; want_status = (int)v;
      snprintf(reply, cap, "OK - sending STATUS %s", STATUS_LABEL[v]);
    } else if (cmd(a, "mark")) {
      const char* p = a + 4; while (*p == ' ') p++;
      char* end; long v = strtol(p, &end, 10);
      if (end == p || v < 1 || v > CAIRN_NMARK || (*end != ' ' && *end != 0)) { snprintf(reply, cap, "Err - cairn mark <1-%d> [channel]", CAIRN_NMARK); return; }
      int ch = -1; if (!parseChan(end, ch)) { snprintf(reply, cap, "Err - channel must be 0-39"); return; }
      msg_channel = ch; want_mark = (int)v;
      snprintf(reply, cap, "OK - marking %s here", MARK_LABEL[v]);
    } else if (cmd(a, "config")) {
      snprintf(reply, cap, "mode %s, power %s, antenna %d.%d dBi, auto %d min, beacon %d min, help %s, eco %s, lowbat %s, main channel %d",
               cfg.mode ? "disaster" : "standard", cfg.power_pref ? "saver" : "full", cfg.ant10 / 10, cfg.ant10 % 10, cfg.auto_min, cfg.beacon_min,
               cfg.help_on ? "ON" : "off", cfg.eco ? "on" : "off", cfg.lowbat ? "on" : "off", cfg.main_chan);
    } else {
      snprintf(reply, cap, "cairn [status|id|ok|say|dm|chans|chan add|new|remove|key|mode standard|disaster|power full|saver|antenna|help|help off|beacon|auto|eco|lowbat|main|ustatus|mark|config|dump|clear yes|time]");
    }
  }

private:
  static bool cmd(const char* a, const char* word) {
    size_t l = strlen(word);
    return strncmp(a, word, l) == 0 && (a[l] == 0 || a[l] == ' ');
  }
  // " 3" -> 3; "" -> -1 (use the main channel). False when something else follows.
  static bool parseChan(const char* e, int& ch) {
    while (*e == ' ') e++;
    if (*e == 0) { ch = -1; return true; }
    char* end; long v = strtol(e, &end, 10);
    if (end == e || *end != 0 || v < 0 || v >= CAIRN_MAX_CHANNELS) return false;
    ch = (int)v; return true;
  }
  void powerReply(char* reply, size_t cap) const {
    int legal = legalSetting(), want = wantedSetting();
    if (legal == -100) {
      snprintf(reply, cap, "Power NOT changed: this radio is not on the US 902-928 MHz band, so the US limit does not apply; its power stays as set in the radio's own settings. Check your local rules.");
      return;
    }
    int est = power::estDbm(want);
    snprintf(reply, cap, "%s mode, power %s: setting %d = about %d dBm (%d mW). Highest the rules allow with a %d.%d dBi antenna: setting %d (US 47 CFR 15.247: 1 W, antenna up to 6 dBi).",
             cfg.mode == 1 ? "DISASTER" : "STANDARD", cfg.mode == 1 || cfg.help_on ? "full" : (cfg.power_pref ? "saver" : "full"), want, est, power::milliwatts(est),
             cfg.ant10 / 10, cfg.ant10 % 10, legal);
  }
  // cairn chan add <name> [key] | new <name> | remove <n> | key <n>
  void chanCommand(const char* a, char* reply, size_t cap) {
    while (*a == ' ') a++;
    if (chan_op != 0) { snprintf(reply, cap, "Err - still working on the last channel change, try again"); return; }
    if (cmd(a, "add") || cmd(a, "new")) {
      bool isnew = cmd(a, "new");
      const char* p = a + 3; while (*p == ' ') p++;
      char name[CAIRN_CHAN_NAME + 8] = {0}; const char* keytxt = nullptr; char tmpkey[40] = {0};
      // The name runs to the end of the line, except that a trailing 32-character hex word is the key.
      size_t l = strlen(p);
      if (!isnew && l > 33 && p[l - 33] == ' ') { bool hex = true; for (size_t i = l - 32; i < l; i++) { char c = p[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) hex = false; } if (hex) { memcpy(tmpkey, p + l - 32, 32); tmpkey[32] = 0; keytxt = tmpkey; l -= 33; } }
      while (l > 0 && p[l - 1] == ' ') l--;
      if (l == 0 || l >= CAIRN_CHAN_NAME) { snprintf(reply, cap, "Err - the channel name must be 1 to 31 characters"); return; }
      memcpy(name, p, l); name[l] = 0;
      if (name[0] == '#') {
        char norm[CAIRN_CHAN_NAME];
        if (isnew || keytxt) { snprintf(reply, cap, "Err - a # channel needs no key: its key comes from its name (cairn chan add #name)"); return; }
        if (!hashtagNormalize(name, norm, sizeof(norm))) { snprintf(reply, cap, "Err - after # use only letters a-z, digits and - (for example #water-tank)"); return; }
        memcpy(chan_name, norm, strlen(norm) + 1); hashtagKey(norm, chan_key); chan_has_key = true; chan_op = 1;
        snprintf(reply, cap, "OK - joining %s (anyone who types the same name can read it)", norm);
        return;
      }
      if (!chanNameOk(name)) { snprintf(reply, cap, "Err - that name has a character not allowed (no quotes or backslash, no control characters)"); return; }
      if (isnew) { chan_has_key = false; chan_op = 2; }
      else {
        if (!keytxt) { snprintf(reply, cap, "Err - a private channel needs a key: cairn chan add <name> <32 hex characters>, or cairn chan new <name> to make one"); return; }
        if (!parseHex16(keytxt, chan_key)) { snprintf(reply, cap, "Err - the key must be 32 characters 0-9 a-f"); return; }
        chan_has_key = true; chan_op = 1;
      }
      memcpy(chan_name, name, strlen(name) + 1);
      snprintf(reply, cap, isnew ? "OK - making private channel %s with a new key" : "OK - adding private channel %s", name);
    } else if (cmd(a, "remove") || cmd(a, "rm") || cmd(a, "del")) {
      const char* p = a; while (*p && *p != ' ') p++; while (*p == ' ') p++;
      char* end; long v = strtol(p, &end, 10);
      if (*p == 0 || end == p || *end != 0 || v < 0 || v >= CAIRN_MAX_CHANNELS) { snprintf(reply, cap, "cairn chan remove <channel number 1-39>"); return; }
      if (v == 0) { snprintf(reply, cap, "Err - channel 0 (Public) cannot be removed: it is where check-ins and help calls go"); return; }
      chan_idx = (int)v; chan_op = 3;
      snprintf(reply, cap, "OK - removing channel %d", (int)v);
    } else if (cmd(a, "key")) {
      const char* p = a + 3; while (*p == ' ') p++;
      char* end; long v = strtol(p, &end, 10);
      if (*p == 0 || end == p || *end != 0 || v < 0 || v >= CAIRN_MAX_CHANNELS) { snprintf(reply, cap, "cairn chan key <channel number>"); return; }
      chan_idx = (int)v; chan_op = 4;
      snprintf(reply, cap, "OK - reading the key of channel %d", (int)v);
    } else if (*a == 0 || cmd(a, "list")) {
      want_chans = true; snprintf(reply, cap, "OK - channel list on its way");
    } else {
      snprintf(reply, cap, "cairn chan add #name | add <name> <key> | new <name> | remove <n> | key <n> | list");
    }
  }
};


// A small first-in first-out byte buffer, no heap. Used for the Bluetooth text link (see CairnOut.h).
template <size_t N> class ByteRing {
 public:
  size_t count() const { return n_; }
  size_t room() const { return N - n_; }
  uint32_t lost() const { return lost_; }
  void clear() { h_ = 0; n_ = 0; }
  bool push(uint8_t c) { if (n_ >= N) { lost_++; return false; } b_[(h_ + n_) % N] = c; n_++; return true; }
  int pop() { if (!n_) return -1; uint8_t c = b_[h_]; h_ = (h_ + 1) % N; n_--; return c; }
  size_t peek(uint8_t* d, size_t max) const { size_t k = n_ < max ? n_ : max; for (size_t i = 0; i < k; i++) d[i] = b_[(h_ + i) % N]; return k; }
  void drop(size_t k) { if (k > n_) k = n_; h_ = (h_ + k) % N; n_ -= k; }
 private:
  uint8_t b_[N];
  size_t h_ = 0, n_ = 0;
  uint32_t lost_ = 0;
};

}  // namespace cairn
