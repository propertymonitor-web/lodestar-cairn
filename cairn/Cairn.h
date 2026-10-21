// Cairn.h - LODESTAR CAIRN: a field-station layer on top of LODESTAR NOVA (header-only, no Arduino deps).
//
// CAIRN adds, to the NOVA companion firmware:
//   * a station ID ("LC-" plus 8 letters/digits) taken from the radio's own public key;
//   * a journal: every message and every advert the radio hears, one JSON object per line, kept on the
//     radio's flash AND streamed live to the dashboard page;
//   * the check-in message "LC-xxxxxxxx Reporting at: <lat>,<lon>";
//   * the `cairn` text commands (the glue in MyMesh.cpp does the flash and radio work).
//
// Host-side tests:  g++ -std=c++17 -Wall -Wextra -Icairn cairn/test/test_cairn.cpp -o t && ./t
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define CAIRN_VERSION  "1.0.2"
#define CAIRN_LINE     640       // longest journal line, bytes (a 160-character message can need ~480)
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

// "LC-K7M2P4XQ Reporting at: 29.76043,-95.36980" - or, with no GPS fix, "LC-K7M2P4XQ Reporting at: no GPS fix".
// Returns the length, or 0 if `cap` is too small.
static inline size_t okMessage(const char* id, bool fix, int32_t lat_e6, int32_t lon_e6, char* out, size_t cap) {
  int n;
  if (fix) {
    char a[24], b[24];
    fmtDeg(a, sizeof(a), lat_e6); fmtDeg(b, sizeof(b), lon_e6);
    n = snprintf(out, cap, "%s Reporting at: %s,%s", id, a, b);
  } else {
    n = snprintf(out, cap, "%s Reporting at: no GPS fix", id);
  }
  return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
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

// ---- the station -----------------------------------------------------------------------------------
class Cairn {
public:
  char id[CAIRN_ID_LEN + 1] = {0};
  Journal j;
  AdvertSeen seen;
  Cursor to_flash, to_usb;

  // Requests set by `cairn ...` text commands. The glue in MyMesh.cpp acts on them and clears them.
  bool want_ok = false;      int ok_channel = 0;
  bool want_dump = false;
  bool want_clear = false;
  int  want_mode = 0;        // 1 = disaster, 2 = normal
  uint32_t want_time = 0;    // set the radio's clock to this (seconds since 1970), from the dashboard
  uint32_t last_ok_up = 0;   // seconds since boot when this station last sent a check-in (0 = none)

  // Kept up to date by the glue, for the status line.
  uint32_t file_bytes = 0;

  bool hasId() const { return validId(id); }
  void setId(const char* s) { if (validId(s)) { memcpy(id, s, CAIRN_ID_LEN); id[CAIRN_ID_LEN] = 0; } }

  // `a` is what follows the word "cairn". Fills `reply`.
  void runCommand(const char* a, char* reply, size_t cap, uint32_t up) {
    while (*a == ' ') a++;
    if (*a == 0 || strncmp(a, "status", 6) == 0) {
      snprintf(reply, cap, "LODESTAR CAIRN %s  ID %s  journal %lu KB flash, %lu lines since boot, up %lu min",
               CAIRN_VERSION, hasId() ? id : "(none)", (unsigned long)(file_bytes / 1024u), (unsigned long)j.total,
               (unsigned long)(up / 60u));
    } else if (strncmp(a, "id", 2) == 0 && (a[2] == 0 || a[2] == ' ')) {
      snprintf(reply, cap, "%s", hasId() ? id : "no ID yet");
    } else if (strncmp(a, "ok", 2) == 0 && (a[2] == 0 || a[2] == ' ')) {
      const char* p = a + 2; while (*p == ' ') p++;
      int ch = 0;
      if (*p) {
        char* end; long v = strtol(p, &end, 10);
        if (end == p || *end != 0 || v < 0 || v > 39) { snprintf(reply, cap, "Err - cairn ok [channel number 0-39]"); return; }
        ch = (int)v;
      }
      ok_channel = ch; want_ok = true;
      snprintf(reply, cap, "OK - sending check-in to channel %d", ch);
    } else if (strncmp(a, "dump", 4) == 0) {
      want_dump = true;
      snprintf(reply, cap, "OK - sending the journal");
    } else if (strncmp(a, "clear", 5) == 0) {
      const char* p = a + 5; while (*p == ' ') p++;
      if (strcmp(p, "yes") == 0) { want_clear = true; snprintf(reply, cap, "OK - journal on the radio will be erased"); }
      else snprintf(reply, cap, "This erases the journal stored on the radio. To do it type: cairn clear yes");
    } else if (strncmp(a, "time", 4) == 0 && (a[4] == 0 || a[4] == ' ')) {
      const char* p = a + 4; while (*p == ' ') p++;
      char* end; unsigned long v = strtoul(p, &end, 10);
      if (*p == 0 || *end != 0 || v < 1700000000UL || v > 4102444800UL) { snprintf(reply, cap, "Err - cairn time <seconds since 1970>"); return; }
      want_time = (uint32_t)v;
      snprintf(reply, cap, "OK - radio clock set");
    } else if (strncmp(a, "mode", 4) == 0) {
      const char* p = a + 4; while (*p == ' ') p++;
      if (strcmp(p, "disaster") == 0) { want_mode = 1; snprintf(reply, cap, "OK - disaster mode: short screen time, quiet hours off"); }
      else if (strcmp(p, "normal") == 0) { want_mode = 2; snprintf(reply, cap, "OK - normal mode: desk profile"); }
      else snprintf(reply, cap, "cairn mode disaster | normal");
    } else {
      snprintf(reply, cap, "cairn [status|id|ok [channel]|dump|clear yes|mode disaster|normal|time <epoch>]");
    }
  }
};

}  // namespace cairn
