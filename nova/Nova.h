// Nova - LODESTAR NOVA companion features for MeshCore.
// By FEAR REACTIVE.
//
// Header-only and free of Arduino / MeshCore dependencies so everything can be
// unit-tested on a PC (see nova/test). The MeshCore glue lives in
// tools/apply_nova.py, which patches examples/companion_radio.
//
// What is in here
//   * link health   - the LODESTAR uplink table, fed from every packet the companion hears
//   * deliveries    - what happened to each message you sent: sent, repeated, delivered, failed
//   * range check   - turns a repeater-discovery result into GOOD / OK / WEAK / NONE
//   * trip          - distance, time, top speed, track, and the last place with a usable uplink
//   * battery       - smoothed percentage, trend and a rough time-left estimate
//   * profiles      - hike / car / desk presets (screen timeout, how fast neighbours expire)
//   * quiet hours   - silence the buzzer, vibration and screen wake at night (favourites excepted)
//   * dashboard     - one JSON line of everything above, plus a small text command set
//
// Nothing here transmits, and nothing here changes how packets are routed.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "Lodestar.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Edition. LODESTAR ORION is the wardriving edition of this firmware: the same code, built with
// -DNOVA_ORION=1, with its own name and version marker.
#ifdef NOVA_CAIRN
#define NOVA_PRODUCT        "CAIRN"
#define NOVA_EDITION        "cairn"
#define NOVA_VER_SFX        ""
#define NOVA_BASE_VERSION   "1.0.4"
#elif defined(NOVA_ORION)
#define NOVA_PRODUCT        "ORION"
#define NOVA_EDITION        "orion"
#define NOVA_VER_SFX        "-orion"
#else
#define NOVA_PRODUCT        "NOVA"
#define NOVA_EDITION        "nova"
#define NOVA_VER_SFX        ""
#endif

#ifndef NOVA_BASE_VERSION
#define NOVA_BASE_VERSION   "0.1.0-beta"
#endif

// Speed units. The default build speaks km/h; build with -DNOVA_MPH=1 for a miles-per-hour version.
#ifdef NOVA_MPH
#define NOVA_VERSION        NOVA_BASE_VERSION NOVA_VER_SFX "-mph"
#define NOVA_SPD_MUL        2.23694f      // metres per second -> miles per hour
#define NOVA_SPD_UNIT       "mph"
#else
#define NOVA_VERSION        NOVA_BASE_VERSION NOVA_VER_SFX
#define NOVA_SPD_MUL        3.6f          // metres per second -> kilometres per hour
#define NOVA_SPD_UNIT       "km/h"
#endif
#define NOVA_MAX_DELIVERIES 8
#define NOVA_TRACK_POINTS   64
#define NOVA_BATT_SAMPLES   96
#define NOVA_JSON_LINKS     8
#define NOVA_SCORE_HIST     12      // 12 samples, 10 s apart

namespace nova {

static inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }   // wrap-safe

// ----------------------------------------------------------------------------
// Settings (the only things saved to flash)
// ----------------------------------------------------------------------------
enum Profile : uint8_t { PROFILE_HIKE = 0, PROFILE_CAR = 1, PROFILE_DESK = 2 };

struct Settings {
  uint8_t profile     = PROFILE_CAR;
  bool    quiet_on    = false;
  uint8_t quiet_start = 22;      // local hour, 0..23
  uint8_t quiet_end   = 7;
};

struct ProfileParams {
  const char* name;
  uint32_t screen_ms;            // how long the OLED stays on after a button press
  uint32_t stale_start_ms;       // LODESTAR: full credit for a neighbour until this age
  uint32_t stale_end_ms;         // ...none after this
  uint32_t expire_ms;            // ...dropped after this
};

static inline const ProfileParams& profileParams(uint8_t p) {
  // Moving fast: neighbours come and go in minutes. Walking: slower. Parked: they stay.
  static const ProfileParams P[3] = {
    { "hike",  10000u, 240000u,  900000u, 3600000u },
    { "car",   30000u,  90000u,  420000u,  900000u },
    { "desk",  60000u, 600000u, 1800000u, 7200000u },
  };
  return P[p > 2 ? 1 : p];
}

// ----------------------------------------------------------------------------
// Deliveries: what happened to the messages you sent
// ----------------------------------------------------------------------------
enum DeliveryState : uint8_t { D_SENT = 0, D_REPEATED = 1, D_DELIVERED = 2, D_FAILED = 3 };

struct Delivery {
  bool     used;
  bool     flood;               // went out as a flood (a direct send has no echo to listen for)
  bool     hash_set;
  bool     link_ok_at_send;     // was a usable uplink known when it was sent?
  bool     chan;                // a channel message: no acknowledgement exists, so the best outcome is a repeater echo
  uint8_t  state;
  int16_t  via;                 // first byte of the repeater that carried it first, or -1
  uint32_t ack;
  uint32_t sent_ms;
  uint32_t timeout_ms;
  uint32_t rtt_ms;              // set when delivered
  uint32_t seq;                 // order of sending, for the dashboard
  uint8_t  hash[LODESTAR_HASH_LEN];
  char     who[14];
};

struct DeliveryStats { uint32_t sent, repeated, delivered, failed; };

class DeliveryLog {
public:
  DeliveryStats stats{};

  DeliveryLog() { memset(_d, 0, sizeof(_d)); }

  // The phone asked us to send a message. `timeout_ms` is the firmware's own estimate of
  // how long an acknowledgement should take.
  void onSend(uint32_t ack, bool flood, uint32_t timeout_ms, const char* who, bool link_ok, uint32_t now) {
    if (ack == 0) return;                       // nothing to track without an ack code
    int slot = -1;
    uint32_t oldest = 0;
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      if (!_d[i].used) { slot = i; break; }
      if (slot < 0 || _d[i].seq < _d[slot].seq) slot = i;
    }
    (void)oldest;
    Delivery& d = _d[slot];
    memset(&d, 0, sizeof(d));
    d.used = true; d.flood = flood; d.state = D_SENT; d.via = -1;
    d.ack = ack; d.sent_ms = now; d.timeout_ms = timeout_ms < 3000 ? 3000 : timeout_ms;
    d.link_ok_at_send = link_ok; d.seq = ++_seq;
    strncpy(d.who, who ? who : "?", sizeof(d.who) - 1);
    stats.sent++;
  }

  // The phone sent a message to a channel (Public, a private channel...). Channel messages carry no
  // acknowledgement, so the most NOVA can tell you is whether a repeater picked it up.
  void onSendChannel(const char* name, bool link_ok, uint32_t now) {
    int slot = -1;
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      if (!_d[i].used) { slot = i; break; }
      if (slot < 0 || _d[i].seq < _d[slot].seq) slot = i;
    }
    Delivery& d = _d[slot];
    memset(&d, 0, sizeof(d));
    d.used = true; d.flood = true; d.chan = true; d.state = D_SENT; d.via = -1;
    d.sent_ms = now; d.timeout_ms = 10000u;
    d.link_ok_at_send = link_ok; d.seq = ++_seq;
    strncpy(d.who, name ? name : "channel", sizeof(d.who) - 1);
    stats.sent++;
  }

  // Our radio put the message on the air. Binds the packet hash to the oldest message
  // that has none yet, so a later echo can be recognised.
  void onTxText(const uint8_t* hash, uint32_t now) {
    (void)now;
    int best = -1;
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      const Delivery& d = _d[i];
      if (!d.used || d.hash_set || d.state != D_SENT || !d.flood) continue;
      if (best < 0 || d.seq < _d[best].seq) best = i;
    }
    if (best < 0) return;
    memcpy(_d[best].hash, hash, LODESTAR_HASH_LEN);
    _d[best].hash_set = true;
  }

  // A flood packet was heard whose last hop is the repeater with first byte `via_key0`.
  void onHeard(const uint8_t* hash, int via_key0) {
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      Delivery& d = _d[i];
      if (!d.used || !d.hash_set || d.state != D_SENT) continue;
      if (memcmp(d.hash, hash, LODESTAR_HASH_LEN) != 0) continue;
      d.state = D_REPEATED; d.via = (int16_t)via_key0;
      stats.repeated++;
    }
  }

  // The recipient's acknowledgement arrived.
  void onAck(uint32_t ack, uint32_t now) {
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      Delivery& d = _d[i];
      if (!d.used || d.chan || d.ack != ack || d.state == D_DELIVERED) continue;
      if (d.state == D_FAILED && stats.failed) stats.failed--;     // it arrived after all
      d.state = D_DELIVERED; d.rtt_ms = elapsed(now, d.sent_ms);
      stats.delivered++;
    }
  }

  void loop(uint32_t now) {
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      Delivery& d = _d[i];
      if (!d.used || d.state == D_DELIVERED || d.state == D_FAILED) continue;
      if (d.chan && d.state == D_REPEATED) continue;            // a repeater has it: as far as a channel message goes, done
      // A direct message's acknowledgement can take far longer than the firmware's estimate on a busy mesh
      // (a real log showed one arriving after 18 s), so give those much longer before calling them failed.
      uint32_t limit = d.chan ? d.timeout_ms + 2000u : (d.timeout_ms * 2u + 2000u > 30000u ? d.timeout_ms * 2u + 2000u : 30000u);
      if (elapsed(now, d.sent_ms) > limit) { d.state = D_FAILED; stats.failed++; }
    }
  }

  // Newest first. Returns nullptr when n is past the end.
  const Delivery* newest(int n) const {
    int order[NOVA_MAX_DELIVERIES]; int cnt = 0;
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) if (_d[i].used) order[cnt++] = i;
    for (int a = 1; a < cnt; a++) {                       // tiny insertion sort, newest first
      int v = order[a], b = a - 1;
      while (b >= 0 && _d[order[b]].seq < _d[v].seq) { order[b + 1] = order[b]; b--; }
      order[b + 1] = v;
    }
    return (n >= 0 && n < cnt) ? &_d[order[n]] : nullptr;
  }
  int count() const { int n = 0; for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) n += _d[i].used; return n; }
  int pending() const { int n = 0; for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) n += (_d[i].used && (_d[i].state == D_SENT || (_d[i].state == D_REPEATED && !_d[i].chan))); return n; }

  static const char* stateName(uint8_t s) {
    switch (s) { case D_SENT: return "sent"; case D_REPEATED: return "repeated"; case D_DELIVERED: return "delivered"; default: return "failed"; }
  }

  // One OLED line: "Bob  delivered 4s" / "Ana  repeated 3F" / "Cy   failed"
  static void line(const Delivery& d, uint32_t now, char* out, size_t cap) {
    char who[9]; snprintf(who, sizeof(who), "%.8s", d.who);
    switch (d.state) {
      case D_DELIVERED: snprintf(out, cap, "%-8s ok %lus", who, (unsigned long)((d.rtt_ms + 500) / 1000)); break;
      case D_REPEATED:  if (d.via >= 0) { snprintf(out, cap, "%-8s heard by %02X", who, (unsigned)d.via); }
                        else { snprintf(out, cap, "%-8s repeated", who); }
                        break;
      case D_FAILED:    if (d.chan) snprintf(out, cap, "%-8s unheard", who);
                        else snprintf(out, cap, "%-8s FAILED%s", who, d.link_ok_at_send ? "" : " no link");
                        break;
      default:          snprintf(out, cap, "%-8s sent %lus", who, (unsigned long)(elapsed(now, d.sent_ms) / 1000)); break;
    }
  }

private:
  Delivery _d[NOVA_MAX_DELIVERIES];
  uint32_t _seq = 0;
};

// ----------------------------------------------------------------------------
// Repeater names, read from the adverts repeaters broadcast
// ----------------------------------------------------------------------------
#define NOVA_NAME_LEN   24
#define NOVA_NAME_SLOTS 24

class NameBook {
public:
  NameBook() { memset(_e, 0, sizeof(_e)); }

  // Pull the display name out of an advert's app data: flags, then optional location (8),
  // two reserved fields (2 each), then the name itself. Returns false if there is none.
  static bool parseName(const uint8_t* ad, size_t len, char* out, size_t cap) {
    if (len < 1 || cap < 2) return false;
    uint8_t flags = ad[0];
    if (!(flags & 0x80)) return false;                       // no name in this advert
    size_t i = 1;
    if (flags & 0x10) i += 8;
    if (flags & 0x20) i += 2;
    if (flags & 0x40) i += 2;
    if (i >= len) return false;
    size_t n = len - i;
    if (n > cap - 1) n = cap - 1;
    size_t o = 0;
    for (size_t k = 0; k < n; k++) {
      uint8_t c = ad[i + k];
      if (c == 0) break;
      out[o++] = (char)(c < 0x20 || c == 0x7F ? ' ' : c);
    }
    // never leave half of a multi-byte character at the end
    while (o > 0 && (((uint8_t)out[o - 1]) & 0xC0) == 0x80) o--;                    // continuation bytes
    if (o > 0 && ((uint8_t)out[o - 1]) >= 0xC0) o--;                                // an orphaned lead byte
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
    return o > 0;
  }

  // A full advert payload: public key (32), timestamp (4), signature (64), app data.
  // Only repeaters (low nibble of the flags = 2) are kept.
  void onAdvert(const uint8_t* payload, size_t payload_len) {
    const size_t off = 32 + 4 + 64;
    if (payload_len <= off + 1) return;
    if ((payload[off] & 0x0F) != 2) return;
    char nm[NOVA_NAME_LEN];
    if (!parseName(payload + off, payload_len - off, nm, sizeof(nm))) return;
    int slot = -1;
    for (int i = 0; i < NOVA_NAME_SLOTS; i++) if (_e[i].used && memcmp(_e[i].key, payload, 3) == 0 && same(_e[i], payload)) { slot = i; break; }
    if (slot < 0) {
      for (int i = 0; i < NOVA_NAME_SLOTS; i++) {
        if (!_e[i].used) { slot = i; break; }
        if (slot < 0 || _e[i].seq < _e[slot].seq) slot = i;
      }
    }
    Entry& e = _e[slot];
    e.used = true; memcpy(e.key, payload, 3); memcpy(e.full, payload + 3, 5);
    strncpy(e.name, nm, sizeof(e.name) - 1); e.name[sizeof(e.name) - 1] = 0;
    e.seq = ++_seq;
  }

  // The name for a repeater known by the first `key_len` bytes of its key. With a short key
  // that two repeaters share, there is no honest answer, so none is given.
  const char* get(const uint8_t* key, uint8_t key_len) const {
    if (key_len == 0) return "";
    const Entry* found = nullptr;
    for (int i = 0; i < NOVA_NAME_SLOTS; i++) {
      const Entry& e = _e[i];
      if (!e.used || memcmp(e.key, key, key_len > 3 ? 3 : key_len) != 0) continue;
      if (found && strcmp(found->name, e.name) != 0) return "";
      found = &e;
    }
    return found ? found->name : "";
  }

private:
  struct Entry { bool used; uint8_t key[3]; uint8_t full[5]; char name[NOVA_NAME_LEN]; uint32_t seq; };
  static bool same(const Entry& e, const uint8_t* payload) { return memcmp(e.full, payload + 3, 5) == 0; }
  Entry _e[NOVA_NAME_SLOTS];
  uint32_t _seq = 0;
};

// ----------------------------------------------------------------------------
// Found nodes: every node whose advert this radio hears, with where it was heard.
// This is the wardriving log. Companions and repeaters (and room servers and sensors) all
// advertise themselves; an advert may carry the node's own announced location, and we add
// our own GPS position at the moment we heard it best.
// ----------------------------------------------------------------------------
#define NOVA_FOUND_SLOTS 48
#define NOVA_FOUND_NAME  20
enum NodeType : uint8_t { NODE_COMPANION = 1, NODE_REPEATER = 2, NODE_ROOM = 3, NODE_SENSOR = 4 };

struct FoundNode {
  bool    used, dirty, has_adv, has_here;
  uint8_t type, hops;                 // hops: 0 = heard directly, otherwise relayed through that many repeaters
  int8_t  rssi;
  int16_t snr_q;                      // best SNR seen, in quarter dB
  uint8_t key[6];                     // first 6 bytes of the node's public key
  char    name[NOVA_FOUND_NAME];
  int32_t adv_lat, adv_lon;           // the location the node announces about itself (degrees * 1e6)
  int32_t here_lat, here_lon;         // where this radio was when it heard the node best
  uint16_t count;
  uint32_t first_ms, last_ms, seq;
};

class FoundBook {
public:
  FoundBook() { memset(_e, 0, sizeof(_e)); }

  // `payload` is a full advert payload: public key (32), timestamp (4), signature (64), app data.
  // `hops` is how many repeaters it came through. `fix`/`lat_e6`/`lon_e6` are this radio's own position.
  // Returns true if the node is new or something worth reporting changed.
  bool onAdvert(const uint8_t* payload, size_t len, uint8_t hops, float snr, int rssi,
                bool fix, int32_t lat_e6, int32_t lon_e6, uint32_t now) {
    const size_t off = 32 + 4 + 64;
    if (!payload || len <= off + 1) return false;
    const uint8_t* ad = payload + off; size_t adlen = len - off;
    uint8_t type = ad[0] & 0x0F;
    if (type < NODE_COMPANION || type > NODE_SENSOR) return false;

    bool has_adv = false; int32_t alat = 0, alon = 0;
    if ((ad[0] & 0x10) && adlen >= 9) {
      alat = rd32(ad + 1); alon = rd32(ad + 5);
      has_adv = alat >= -90000000 && alat <= 90000000 && alon >= -180000000 && alon <= 180000000 && !(alat == 0 && alon == 0);
    }
    char nm[NOVA_FOUND_NAME] = {0};
    if (NameBook::parseName(ad, adlen, nm, sizeof(nm))) {
      for (char* c = nm; *c; c++) if (*c == '"' || *c == '\\') *c = '?';      // a name is data, never markup
    }
    int sq = (int)(snr * 4.0f + (snr < 0 ? -0.5f : 0.5f)); if (sq > 127 * 4) sq = 127 * 4; if (sq < -128 * 4) sq = -128 * 4;
    int rs = rssi < -127 ? -127 : (rssi > 0 ? 0 : rssi);

    int slot = -1;
    for (int i = 0; i < NOVA_FOUND_SLOTS; i++) if (_e[i].used && memcmp(_e[i].key, payload, 6) == 0) { slot = i; break; }
    bool fresh = slot < 0;
    if (fresh) {
      for (int i = 0; i < NOVA_FOUND_SLOTS; i++) {
        if (!_e[i].used) { slot = i; break; }
        if (slot < 0 || _e[i].seq < _e[slot].seq) slot = i;                  // otherwise drop the one not heard for longest
      }
      FoundNode& n = _e[slot]; memset(&n, 0, sizeof(n));
      n.used = true; memcpy(n.key, payload, 6); n.first_ms = now;
      n.hops = hops; n.rssi = (int8_t)rs; n.snr_q = (int16_t)sq;
      n.has_here = false;
    }
    FoundNode& n = _e[slot];
    bool changed = fresh;
    n.type = type; n.last_ms = now; n.seq = ++_seq; if (n.count < 65535) n.count++;
    if (nm[0] && strcmp(nm, n.name) != 0) { strncpy(n.name, nm, sizeof(n.name) - 1); n.name[sizeof(n.name) - 1] = 0; changed = true; }
    if (has_adv && (!n.has_adv || n.adv_lat != alat || n.adv_lon != alon)) { n.has_adv = true; n.adv_lat = alat; n.adv_lon = alon; changed = true; }

    // keep the best reception: heard more directly, or at least 1 dB stronger
    bool better = fresh || hops < n.hops || (hops == n.hops && sq >= n.snr_q + 4) || (!n.has_here && fix);
    if (better) {
      n.hops = hops; n.rssi = (int8_t)rs; n.snr_q = (int16_t)sq;
      if (fix) { n.has_here = true; n.here_lat = lat_e6; n.here_lon = lon_e6; }
      changed = true;
    }
    if (changed) n.dirty = true;
    return changed;
  }

  void markAllDirty() { for (int i = 0; i < NOVA_FOUND_SLOTS; i++) if (_e[i].used) _e[i].dirty = true; }
  void clear() { memset(_e, 0, sizeof(_e)); _seq = 0; }
  int  count(int type = 0) const { int c = 0; for (int i = 0; i < NOVA_FOUND_SLOTS; i++) if (_e[i].used && (type == 0 || _e[i].type == type)) c++; return c; }
  int  dirtyCount() const { int c = 0; for (int i = 0; i < NOVA_FOUND_SLOTS; i++) if (_e[i].used && _e[i].dirty) c++; return c; }
  const FoundNode* at(int i) const { return (i >= 0 && i < NOVA_FOUND_SLOTS && _e[i].used) ? &_e[i] : nullptr; }

  // The next changed node as one JSON line (no newline); clears its flag. 0 if nothing is waiting
  // or `cap` is too small (the flag is then left set).
  size_t nextLine(char* out, size_t cap, uint32_t now) {
    for (int i = 0; i < NOVA_FOUND_SLOTS; i++) {
      FoundNode& n = _e[i];
      if (!n.used || !n.dirty) continue;
      size_t o = 0;
      char kh[13]; for (int k = 0; k < 6; k++) snprintf(kh + k * 2, 3, "%02X", n.key[k]);
      o += put(out + o, cap - o, "{\"nova\":1,\"found\":{\"k\":\"%s\",\"t\":%d,\"n\":\"%s\",\"hop\":%d,\"rs\":%d,\"sn\":%.1f,\"c\":%u,\"ago\":%lu,\"first\":%lu",
               kh, n.type, n.name, n.hops, (int)n.rssi, (double)n.snr_q / 4.0, (unsigned)n.count,
               (unsigned long)((uint32_t)(now - n.last_ms) / 1000u), (unsigned long)((uint32_t)(now - n.first_ms) / 1000u));
      char a[16], b[16];
      if (n.has_adv)  { e6(a, sizeof(a), n.adv_lat);  e6(b, sizeof(b), n.adv_lon);  o += put(out + o, cap - o, ",\"alat\":%s,\"alon\":%s", a, b); }
      if (n.has_here) { e6(a, sizeof(a), n.here_lat); e6(b, sizeof(b), n.here_lon); o += put(out + o, cap - o, ",\"hlat\":%s,\"hlon\":%s", a, b); }
      o += put(out + o, cap - o, "}}");
      if (o == 0 || o >= cap) return 0;
      n.dirty = false;
      return o;
    }
    return 0;
  }

private:
  static int32_t rd32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
  static void e6(char* out, size_t cap, int32_t v) {          // degrees * 1e6 as "-12.345678", with no floating point
    bool neg = v < 0; int64_t a = neg ? -(int64_t)v : (int64_t)v;
    snprintf(out, cap, "%s%ld.%06ld", neg ? "-" : "", (long)(a / 1000000), (long)(a % 1000000));
  }
  static size_t put(char* out, size_t cap, const char* fmt, ...) {
    if (cap < 2) return 0;
    va_list ap; va_start(ap, fmt); int n = vsnprintf(out, cap, fmt, ap); va_end(ap);
    return (n < 0 || (size_t)n >= cap) ? cap : (size_t)n;     // cap means "did not fit"
  }
  FoundNode _e[NOVA_FOUND_SLOTS];
  uint32_t _seq = 0;
};

// ----------------------------------------------------------------------------
// Range check
// ----------------------------------------------------------------------------
enum RangeVerdict : uint8_t { RANGE_NONE = 0, RANGE_WEAK = 1, RANGE_OK = 2, RANGE_GOOD = 3 };

// `best_snr`: strongest repeater reply (dB). `sf`: spreading factor. Judged against the
// LoRa demodulation floor for that SF, not an absolute number.
static inline uint8_t rangeVerdict(int repeaters, float best_snr, uint8_t sf) {
  if (repeaters <= 0) return RANGE_NONE;
  float floor_db = -7.5f - 2.5f * ((float)sf - 7.0f);
  float margin = best_snr - floor_db;
  if (margin >= 10.0f && repeaters >= 2) return RANGE_GOOD;
  if (margin >= 4.0f) return RANGE_OK;
  return RANGE_WEAK;
}
static inline const char* rangeName(uint8_t v) {
  switch (v) { case RANGE_GOOD: return "GOOD"; case RANGE_OK: return "OK"; case RANGE_WEAK: return "WEAK"; default: return "NONE"; }
}

// ----------------------------------------------------------------------------
// Trip: distance, time, top speed, a track, and the last good-coverage point
// ----------------------------------------------------------------------------
struct TrackPoint { int32_t lat_e6, lon_e6; };

class Trip {
public:
  Trip() { reset(); }

  void reset() {
    dist_m = 0; top_mps = 0; moving_ms = 0; total_ms = 0;
    _have_last = false; _have_cov = false; _ntrack = 0; _track_step = 25.0; _since_point = 0;
    _last_ms = 0; _started = false;
  }

  // Call about once a second. `fix` false means no usable GPS fix right now.
  void update(bool fix, int32_t lat_e6, int32_t lon_e6, float speed_mps, bool moving, bool coverage, uint32_t now) {
    if (!fix) { _have_last = false; return; }              // do not bridge a gap in the fixes
    if (_have_last) {
      uint32_t dt = elapsed(now, _last_ms);
      double e, n; offsetM(_last_lat, _last_lon, lat_e6, lon_e6, e, n);
      double step = sqrt(e * e + n * n);
      if (dt > 0 && dt < 30000u && step / ((double)dt / 1000.0) < 80.0) {   // ignore teleports (>288 km/h)
        total_ms += dt;
        if (moving) moving_ms += dt;
        if (moving && step >= 3.0) {                          // below this it is GPS jitter
          dist_m += step; _since_point += step;
          if (_since_point >= _track_step) { addPoint(lat_e6, lon_e6); _since_point = 0; }
        }
      }
    } else if (_ntrack == 0) {
      addPoint(lat_e6, lon_e6);
    }
    if (moving && speed_mps > top_mps && speed_mps < 80.0f) top_mps = speed_mps;
    _started = true;
    _last_lat = lat_e6; _last_lon = lon_e6; _last_ms = now; _have_last = true;
    if (coverage) { _cov_lat = lat_e6; _cov_lon = lon_e6; _cov_ms = now; _have_cov = true; }
  }

  bool started() const { return _started; }
  bool haveCoverage() const { return _have_cov; }
  uint32_t coverageAgeMs(uint32_t now) const { return _have_cov ? elapsed(now, _cov_ms) : 0; }

  // Straight-line distance and compass bearing from (lat,lon) to the last place that had
  // a usable uplink. Returns false when there has been none.
  bool toCoverage(int32_t lat_e6, int32_t lon_e6, double& dist, double& bearing_deg) const {
    if (!_have_cov) return false;
    double e, n; offsetM(lat_e6, lon_e6, _cov_lat, _cov_lon, e, n);
    dist = sqrt(e * e + n * n);
    double b = atan2(e, n) * 180.0 / M_PI;
    if (b < 0) b += 360.0;
    bearing_deg = b;
    return true;
  }

  int trackCount() const { return _ntrack; }
  const TrackPoint& trackAt(int i) const { return _track[i]; }
  int32_t covLat() const { return _cov_lat; }
  int32_t covLon() const { return _cov_lon; }

  double   dist_m;
  float    top_mps;
  uint32_t moving_ms, total_ms;

  static void offsetM(int32_t lat0, int32_t lon0, int32_t lat1, int32_t lon1, double& east, double& north) {
    const double k = M_PI / 180.0 / 1e6;
    const double R = 6371000.0;
    double mid = ((double)lat0 + (double)lat1) * 0.5 * k;
    north = (double)(lat1 - lat0) * k * R;
    east  = (double)(lon1 - lon0) * k * R * cos(mid);
  }

private:
  TrackPoint _track[NOVA_TRACK_POINTS];
  int      _ntrack;
  double   _track_step, _since_point;
  bool     _have_last, _have_cov, _started;
  int32_t  _last_lat = 0, _last_lon = 0, _cov_lat = 0, _cov_lon = 0;
  uint32_t _last_ms, _cov_ms = 0;

  void addPoint(int32_t lat, int32_t lon) {
    if (_ntrack >= NOVA_TRACK_POINTS) {                   // full: keep every other point, and space them twice as far apart
      for (int i = 0; i < NOVA_TRACK_POINTS / 2; i++) _track[i] = _track[i * 2];
      _ntrack = NOVA_TRACK_POINTS / 2;
      _track_step *= 2.0;
    }
    _track[_ntrack].lat_e6 = lat; _track[_ntrack].lon_e6 = lon; _ntrack++;
  }
};

// ----------------------------------------------------------------------------
// Battery: smoothed percentage, trend, rough time left
// ----------------------------------------------------------------------------
class Battery {
public:
  Battery() { reset(); }
  void reset() { _n = 0; _head = 0; _mv_f = 0; _last_sample = 0; _have = false; _charging = false; _since_change = 0; }

  // Single-cell Li-ion/LiPo, resting-ish voltage to percent. Approximate: a loaded cell reads low.
  static int percent(int mv) {
    static const int16_t T[][2] = {
      {4200,100},{4150,95},{4110,90},{4080,85},{4020,80},{3980,75},{3950,70},{3910,65},{3870,60},
      {3850,55},{3840,50},{3820,45},{3800,40},{3790,35},{3770,30},{3750,25},{3730,20},{3710,15},
      {3690,10},{3610,5},{3270,0} };
    const int N = (int)(sizeof(T) / sizeof(T[0]));
    if (mv >= T[0][0]) return 100;
    if (mv <= T[N - 1][0]) return 0;
    for (int i = 0; i < N - 1; i++) {
      if (mv <= T[i][0] && mv > T[i + 1][0]) {
        return T[i + 1][1] + (T[i][1] - T[i + 1][1]) * (mv - T[i + 1][0]) / (T[i][0] - T[i + 1][0]);
      }
    }
    return 0;
  }

  // Feed often (every few seconds is fine); it samples itself every 150 s.
  void update(uint16_t mv, bool charging, uint32_t now) {
    if (mv < 2500 || mv > 5500) return;                    // not a battery reading
    _mv_f = _have ? _mv_f + 0.1f * ((float)mv - _mv_f) : (float)mv;
    if (!_have || charging != _charging) {
      _charging = charging; _n = 0; _head = 0;             // plugging in or out starts a fresh trend
      _last_sample = now; _have = true; _since_change = now;
      push(now);
      return;
    }
    if (elapsed(now, _last_sample) >= 150000u) { _last_sample = now; push(now); }
  }

  int      mv() const { return (int)(_mv_f + 0.5f); }
  int      pct() const { return _have ? percent(mv()) : -1; }
  bool     charging() const { return _charging; }
  int      samples() const { return _n; }

  // Percent per hour (negative = draining), from a straight-line fit. Needs 30 min of data.
  bool trendPerHour(float& out) const {
    if (_n < 4) return false;
    uint32_t t0 = at(0).t, t1 = at(_n - 1).t;
    if (elapsed(t1, t0) < 1800000u) return false;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < _n; i++) {
      double x = (double)elapsed(at(i).t, t0) / 3600000.0;   // hours
      double y = percent(at(i).mv);
      sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double den = _n * sxx - sx * sx;
    if (den < 1e-9) return false;
    out = (float)((_n * sxy - sx * sy) / den);
    return true;
  }

  // Rough hours until empty, only while discharging steadily. False if it cannot say.
  bool hoursLeft(float& out) const {
    float tr;
    if (_charging || !trendPerHour(tr) || tr > -0.5f) return false;
    float h = (float)pct() / -tr;
    if (h > 99.0f) h = 99.0f;
    out = h;
    return true;
  }

  int sampleCount() const { return _n; }
  int sampleMv(int i) const { return at(i).mv; }
  uint32_t sampleAgeS(int i, uint32_t now) const { return elapsed(now, at(i).t) / 1000u; }

private:
  struct S { uint32_t t; uint16_t mv; };
  S _s[NOVA_BATT_SAMPLES];
  int _n, _head;
  float _mv_f; uint32_t _last_sample, _since_change; bool _have, _charging;

  void push(uint32_t now) {
    if (_n < NOVA_BATT_SAMPLES) { _s[(_head + _n) % NOVA_BATT_SAMPLES] = { now, (uint16_t)mv() }; _n++; }
    else { _s[_head] = { now, (uint16_t)mv() }; _head = (_head + 1) % NOVA_BATT_SAMPLES; }
  }
  const S& at(int i) const { return _s[(_head + i) % NOVA_BATT_SAMPLES]; }
};

// ----------------------------------------------------------------------------
// Diagnostics
// ----------------------------------------------------------------------------
struct Diag {
  uint32_t rx = 0, tx = 0, last_rx_ms = 0;
  float    rx_snr = 0; bool have_rx = false;
  void onRx(float snr, uint32_t now) { rx++; last_rx_ms = now; rx_snr = have_rx ? rx_snr + 0.1f * (snr - rx_snr) : snr; have_rx = true; }
  void onTx() { tx++; }
};

// ----------------------------------------------------------------------------
// Nova: everything together
// ----------------------------------------------------------------------------
class Nova {
public:
  lodestar::Lodestar link;      // uplink table and own position
  Settings     cfg;
  DeliveryLog  deliveries;
  Trip         trip;
  Battery      batt;
  Diag         diag;
  NameBook     names;
  FoundBook    found;

  bool     settings_dirty = false;      // the glue saves the settings when this is set, then clears it
  uint32_t dash_until_ms = 0;           // dashboard stream runs while now < this (see `nova hello`)
  bool     dash_on = false;

  Nova() { applyProfile(); }

  // ---- glue calls --------------------------------------------------------
  // About once a second (it rate-limits itself).
  void loop(uint32_t now, uint16_t batt_mv, bool charging) {
    link.loop(now);
    deliveries.loop(now);
    batt.update(batt_mv, charging, now);
    if (!_ticked || elapsed(now, _last_tick) >= 1000u) {
      _ticked = true; _last_tick = now;
      const lodestar::Position& p = link.position();
      trip.update(link.hasFix(now), p.lat_e6, p.lon_e6, p.speed_mps, link.moving(now), link.best() >= 0, now);
      if (++_ticks % 10 == 0) {                            // every 10 s remember how good the best uplink is
        if (_hist_n < NOVA_SCORE_HIST) _hist[(_hist_head + _hist_n++) % NOVA_SCORE_HIST] = link.bestScore();
        else { _hist[_hist_head] = link.bestScore(); _hist_head = (_hist_head + 1) % NOVA_SCORE_HIST; }
      }
    }
    if (dash_on && (int32_t)(now - dash_until_ms) >= 0) dash_on = false;
  }

  bool linkUsable() const { return link.best() >= 0; }

  // Heard repeaters, strongest score first. Fills idx[] and returns how many (at most `max`).
  int ranked(int* idx, int max, uint32_t now) {
    bool used[LODESTAR_MAX_UPLINKS] = {false};
    int n = 0;
    while (n < max) {
      int bi = -1;
      for (int i = 0; i < LODESTAR_MAX_UPLINKS; i++) {
        if (!link.entry(i).used || used[i]) continue;
        if (bi < 0 || link.scoreOf(i, now) > link.scoreOf(bi, now)) bi = i;
      }
      if (bi < 0) break;
      used[bi] = true; idx[n++] = bi;
    }
    return n;
  }

  // Is the best uplink getting better (+1), worse (-1) or holding (0) over the last couple of minutes?
  int scoreTrend() const {
    if (_hist_n < 6) return 0;
    float old = 0, cur = 0;
    for (int i = 0; i < 3; i++) { old += histAt(i); cur += histAt(_hist_n - 1 - i); }
    float d = (cur - old) / 3.0f;
    return d > 0.08f ? 1 : (d < -0.08f ? -1 : 0);
  }

  // Local hour 0..23 from a Unix time and a whole-hour timezone offset; -1 if the clock was never set.
  static int localHour(uint32_t epoch, int tz_hours) {
    if (epoch < 1767225600u) return -1;                    // before 2026-01-01: the clock has not been set
    long t = (long)epoch + (long)tz_hours * 3600L;
    int h = (int)((t / 3600L) % 24L);
    return h < 0 ? h + 24 : h;
  }

  void applyProfile() {
    const ProfileParams& p = profileParams(cfg.profile);
    link.cfg.stale_start_ms = p.stale_start_ms;
    link.cfg.stale_end_ms   = p.stale_end_ms;
    link.cfg.expire_ms      = p.expire_ms;
  }
  uint32_t screenMs() const { return profileParams(cfg.profile).screen_ms; }

  // Is it quiet right now? `hour` is the local hour 0..23, or -1 when the clock is not set.
  bool quietNow(int hour) const {
    if (!cfg.quiet_on || hour < 0 || hour > 23) return false;
    if (cfg.quiet_start == cfg.quiet_end) return false;
    if (cfg.quiet_start < cfg.quiet_end) return hour >= cfg.quiet_start && hour < cfg.quiet_end;
    return hour >= cfg.quiet_start || hour < cfg.quiet_end;           // wraps past midnight
  }
  // Should an incoming message make noise / wake the screen? Favourites always may.
  bool mayAlert(bool from_favourite, int hour) const { return from_favourite || !quietNow(hour); }

  // ---- saving settings ----------------------------------------------------
  size_t saveSettings(char* out, size_t cap) const {
    int n = snprintf(out, cap, "NOVA1 %d %d %d %d\n", cfg.profile, cfg.quiet_on ? 1 : 0, cfg.quiet_start, cfg.quiet_end);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
  }
  bool loadSettings(const char* text) {
    int pr, qo, qs, qe; char tag[8];
    if (!text || sscanf(text, "%7s %d %d %d %d", tag, &pr, &qo, &qs, &qe) != 5) return false;
    if (strcmp(tag, "NOVA1") != 0 || pr < 0 || pr > 2 || (qo != 0 && qo != 1) || qs < 0 || qs > 23 || qe < 0 || qe > 23) return false;
    cfg.profile = (uint8_t)pr; cfg.quiet_on = qo == 1; cfg.quiet_start = (uint8_t)qs; cfg.quiet_end = (uint8_t)qe;
    applyProfile();
    return true;
  }

  // ---- text commands ("nova ..."), from the serial port ---------------------
  // `args` is the text after the word "nova". Writes a one-line answer into `reply`.
  void runCommand(const char* args, char* reply, size_t cap, uint32_t now, int local_hour) {
    while (*args == ' ') args++;
    if (*args == 0 || strncmp(args, "status", 6) == 0) {
      int pc = batt.pct();
      char b[24]; if (pc >= 0) snprintf(b, sizeof(b), "%d%%%s", pc, batt.charging() ? "+" : ""); else snprintf(b, sizeof(b), "?");
      snprintf(reply, cap, NOVA_PRODUCT " %s profile %s batt %s link %s heard %d sent %lu ok %lu failed %lu quiet %s",
               NOVA_VERSION, profileParams(cfg.profile).name, b, linkUsable() ? "usable" : "none", link.count(),
               (unsigned long)deliveries.stats.sent, (unsigned long)deliveries.stats.delivered,
               (unsigned long)deliveries.stats.failed, quietNow(local_hour) ? "now" : (cfg.quiet_on ? "on" : "off"));
    } else if (strncmp(args, "hello", 5) == 0) {
      dash_on = true; dash_until_ms = now + 10000u;            // the dashboard page repeats this every few seconds
      snprintf(reply, cap, "OK");
    } else if (strncmp(args, "profile", 7) == 0) {
      const char* a = args + 7; while (*a == ' ') a++;
      int p = -1;
      if (strncmp(a, "hike", 4) == 0) p = PROFILE_HIKE; else if (strncmp(a, "car", 3) == 0) p = PROFILE_CAR; else if (strncmp(a, "desk", 4) == 0) p = PROFILE_DESK;
      if (p < 0) { snprintf(reply, cap, "profile: %s (nova profile hike|car|desk)", profileParams(cfg.profile).name); return; }
      cfg.profile = (uint8_t)p; applyProfile(); settings_dirty = true;
      snprintf(reply, cap, "OK - profile %s", profileParams(cfg.profile).name);
    } else if (strncmp(args, "quiet", 5) == 0) {
      const char* a = args + 5; while (*a == ' ') a++;
      if (strncmp(a, "off", 3) == 0) { cfg.quiet_on = false; settings_dirty = true; snprintf(reply, cap, "OK - quiet hours off"); return; }
      int s, e; char* end;
      s = (int)strtol(a, &end, 10);
      if (end != a && *end == '-') {
        const char* b = end + 1; e = (int)strtol(b, &end, 10);
        if (end != b && s >= 0 && s <= 23 && e >= 0 && e <= 23 && s != e) {
          cfg.quiet_start = (uint8_t)s; cfg.quiet_end = (uint8_t)e; cfg.quiet_on = true; settings_dirty = true;
          snprintf(reply, cap, "OK - quiet %02d:00 to %02d:00 (favourites still alert)", s, e);
          return;
        }
      }
      if (*a == 0) snprintf(reply, cap, cfg.quiet_on ? "quiet %02d:00 to %02d:00" : "quiet off (nova quiet 22-7 | off)", cfg.quiet_start, cfg.quiet_end);
      else snprintf(reply, cap, "Err - nova quiet <start hour>-<end hour> (0-23, different) | off");
    } else if (strncmp(args, "trip", 4) == 0) {
      const char* a = args + 4; while (*a == ' ') a++;
      if (strncmp(a, "reset", 5) == 0) { trip.reset(); snprintf(reply, cap, "OK - trip cleared"); }
      else snprintf(reply, cap, "trip %.1f km, %lu min moving, top %d " NOVA_SPD_UNIT, trip.dist_m / 1000.0,
                    (unsigned long)(trip.moving_ms / 60000u), (int)(trip.top_mps * NOVA_SPD_MUL + 0.5f));
    } else if (strncmp(args, "found", 5) == 0) {
      const char* a = args + 5; while (*a == ' ') a++;
      if (strncmp(a, "clear", 5) == 0) { found.clear(); snprintf(reply, cap, "OK - found list cleared"); }
      else snprintf(reply, cap, "found %d repeaters, %d companions, %d other", found.count(NODE_REPEATER), found.count(NODE_COMPANION),
                    found.count(NODE_ROOM) + found.count(NODE_SENSOR));
    } else if (strncmp(args, "defaults", 8) == 0) {
      cfg = Settings(); applyProfile(); settings_dirty = true;
      snprintf(reply, cap, "OK - NOVA settings back to defaults");
    } else {
      snprintf(reply, cap, "nova [status|profile hike|car|desk|quiet 22-7|off|trip [reset]|found [clear]|defaults]");
    }
  }

  // ---- dashboard JSON -------------------------------------------------------
  // One line, no newline inside. Returns the length, or 0 if `cap` is too small.
  // Contains coordinates: it is only ever written to the local USB port, and only
  // while a dashboard page has asked for it.
  size_t json(char* out, size_t cap, uint32_t now, int local_hour) {
    J j(out, cap);
    link.loop(now);
    j.f("{\"nova\":1,\"ed\":\"%s\",\"v\":\"%s\",\"unit\":\"%s\",\"t\":%lu,\"prof\":\"%s\"", NOVA_EDITION, NOVA_VERSION, NOVA_SPD_UNIT, (unsigned long)now, profileParams(cfg.profile).name);
    j.f(",\"quiet\":{\"on\":%d,\"now\":%d,\"s\":%d,\"e\":%d}", cfg.quiet_on ? 1 : 0, quietNow(local_hour) ? 1 : 0, cfg.quiet_start, cfg.quiet_end);

    // battery
    float tr = 0, hl = 0; bool hasTr = batt.trendPerHour(tr), hasHl = batt.hoursLeft(hl);
    j.f(",\"bat\":{\"mv\":%d,\"pct\":%d,\"chg\":%d,\"trend\":", batt.mv(), batt.pct(), batt.charging() ? 1 : 0);
    if (hasTr) j.f("%.1f", (double)tr); else j.s("null");
    j.s(",\"hrs\":"); if (hasHl) j.f("%.1f", (double)hl); else j.s("null");
    j.s(",\"hist\":[");
    for (int i = 0; i < batt.sampleCount(); i++) j.f("%s%d", i ? "," : "", batt.sampleMv(i));
    j.s("]}");

    // gps
    const lodestar::Position& p = link.position();
    bool fix = link.hasFix(now);
    j.f(",\"gps\":{\"fix\":%d", fix ? 1 : 0);
    if (fix) {
      j.f(",\"lat\":%.6f,\"lon\":%.6f,\"sats\":%d,\"spd\":%d,\"mov\":%d,\"course\":", (double)p.lat_e6 / 1e6, (double)p.lon_e6 / 1e6,
          (int)p.sats, (int)(p.speed_mps * NOVA_SPD_MUL + 0.5f), link.moving(now) ? 1 : 0);
      if (p.course_valid) j.f("%d", (int)(p.course_deg + 0.5f) % 360); else j.s("null");
    }
    j.s("}");

    // links: best first, at most NOVA_JSON_LINKS
    j.f(",\"best\":%d,\"links\":[", link.best() >= 0 ? (int)link.entry(link.best()).key[0] : -1);
    int order[NOVA_JSON_LINKS];
    int nrank = ranked(order, NOVA_JSON_LINKS, now);
    int shown = 0;
    for (int k = 0; k < nrank; k++) {
      int bi = order[k];
      const lodestar::Uplink& u = link.entry(bi);
      char id[8];
      if (u.key_len >= 3) snprintf(id, sizeof(id), "%02X%02X%02X", u.key[0], u.key[1], u.key[2]);
      else if (u.key_len == 2) snprintf(id, sizeof(id), "%02X%02X", u.key[0], u.key[1]);
      else snprintf(id, sizeof(id), "%02X", u.key[0]);
      j.f("%s{\"id\":\"%s\",\"name\":\"",
          shown ? "," : "", id);
      j.esc(names.get(u.key, u.key_len));
      j.f("\",\"best\":%d,\"sc\":%d,\"rel\":%d,\"lq\":%d,\"snr\":%.1f,\"rssi\":%d,\"age\":%lu,\"hit\":%lu,\"n\":%lu}",
          bi == link.best() ? 1 : 0, pc(link.scoreOf(bi, now)), pc(link.reliability(bi, now)), pc(link.linkQuality(bi)),
          (double)u.snr, (int)u.rssi, (unsigned long)(elapsed(now, u.last_ms) / 1000u), (unsigned long)u.hits, (unsigned long)(u.hits + u.misses));
      shown++;
    }
    j.s("]");

    // deliveries
    j.f(",\"dl\":{\"sent\":%lu,\"rep\":%lu,\"ok\":%lu,\"fail\":%lu,\"list\":[", (unsigned long)deliveries.stats.sent, (unsigned long)deliveries.stats.repeated,
        (unsigned long)deliveries.stats.delivered, (unsigned long)deliveries.stats.failed);
    for (int i = 0; i < NOVA_MAX_DELIVERIES; i++) {
      const Delivery* d = deliveries.newest(i);
      if (!d) break;
      j.f("%s{\"who\":\"", i ? "," : ""); j.esc(d->who);
      j.f("\",\"st\":\"%s\",\"via\":%d,\"rtt\":%lu,\"age\":%lu,\"flood\":%d,\"link\":%d,\"chan\":%d}", DeliveryLog::stateName(d->state), (int)d->via,
          (unsigned long)d->rtt_ms, (unsigned long)(elapsed(now, d->sent_ms) / 1000u), d->flood ? 1 : 0, d->link_ok_at_send ? 1 : 0, d->chan ? 1 : 0);
    }
    j.s("]}");

    // trip
    j.f(",\"trip\":{\"km\":%.2f,\"mov_s\":%lu,\"tot_s\":%lu,\"top\":%d,\"track\":[", trip.dist_m / 1000.0, (unsigned long)(trip.moving_ms / 1000u),
        (unsigned long)(trip.total_ms / 1000u), (int)(trip.top_mps * NOVA_SPD_MUL + 0.5f));
    for (int i = 0; i < trip.trackCount(); i++) j.f("%s[%.5f,%.5f]", i ? "," : "", (double)trip.trackAt(i).lat_e6 / 1e6, (double)trip.trackAt(i).lon_e6 / 1e6);
    j.s("]");
    j.f(",\"cov\":");
    double cd, cb;
    if (fix && trip.toCoverage(p.lat_e6, p.lon_e6, cd, cb)) {
      j.f("{\"lat\":%.5f,\"lon\":%.5f,\"m\":%d,\"brg\":%d,\"age\":%lu}", (double)trip.covLat() / 1e6, (double)trip.covLon() / 1e6, (int)(cd + 0.5), (int)(cb + 0.5) % 360,
          (unsigned long)(trip.coverageAgeMs(now) / 1000u));
    } else j.s("null");
    j.s("}");

    // diagnostics
    j.f(",\"diag\":{\"rx\":%lu,\"tx\":%lu,\"snr\":", (unsigned long)diag.rx, (unsigned long)diag.tx);
    if (diag.have_rx) j.f("%.1f", (double)diag.rx_snr); else j.s("null");
    j.f(",\"last_rx_s\":%lu,\"up_s\":%lu}", diag.have_rx ? (unsigned long)(elapsed(now, diag.last_rx_ms) / 1000u) : 0ul, (unsigned long)(now / 1000u));
    j.s("}");
    return j.ok ? j.n : 0;
  }

  static int pc(float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (int)(v * 100.0f + 0.5f); }

private:
  bool _ticked = false; uint32_t _last_tick = 0; uint32_t _ticks = 0;
  float _hist[NOVA_SCORE_HIST]; int _hist_n = 0, _hist_head = 0;
  float histAt(int i) const { return _hist[(_hist_head + i) % NOVA_SCORE_HIST]; }

  struct J {   // bounded writer
    char* b; size_t cap; size_t n; bool ok;
    J(char* buf, size_t c) : b(buf), cap(c), n(0), ok(c > 1) { if (ok) b[0] = 0; }
    void s(const char* t) { size_t l = strlen(t); if (!ok || n + l + 1 > cap) { ok = false; return; } memcpy(b + n, t, l + 1); n += l; }
    void f(const char* fmt, ...) {
      if (!ok) return;
      va_list ap; va_start(ap, fmt);
      int r = vsnprintf(b + n, cap - n, fmt, ap);
      va_end(ap);
      if (r < 0 || (size_t)r >= cap - n) { ok = false; return; }
      n += (size_t)r;
    }
    void esc(const char* t) {       // a name is data, not markup: keep to plain printable ASCII
      for (; *t; t++) {
        char c = (*t >= 32 && *t < 127 && *t != '"' && *t != '\\') ? *t : '?';
        if (!ok || n + 2 > cap) { ok = false; return; }
        b[n++] = c; b[n] = 0;
      }
    }
  };
};

}  // namespace nova
