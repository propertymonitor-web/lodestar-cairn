// Unit tests for Nova.h. Build and run:
//   g++ -std=c++17 -Wall -Wextra -Ilodestar -Inova nova/test/test_nova.cpp -o nova_test && ./nova_test
// With an argument, also writes one sample dashboard line to that file.
#include "Nova.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace nova;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void hashOf(uint8_t* h, int n) { for (int i = 0; i < LODESTAR_HASH_LEN; i++) h[i] = (uint8_t)(n * 7 + i); }

// drive the GPS in a straight line (north) at `mps` for `secs`, one fix per second
static uint32_t drive(Nova& N, uint32_t t, int secs, int32_t& lat, int32_t lon, double mps, bool sf_enabled = true) {
  (void)sf_enabled;
  for (int i = 0; i < secs; i++, t += 1000) {
    lat += (int32_t)(mps / 0.111319);       // 1e-6 degrees of latitude is 0.111319 m
    N.link.onPosition(lat, lon, 9, true, t);
    N.loop(t, 3900, false);
  }
  return t;
}

int main(int argc, char** argv) {
  // 1. settings round-trip, and bad text is rejected without changing anything
  {
    Nova a; char buf[80], r[160];
    CHECK(!a.settings_dirty);
    a.runCommand("profile hike", r, sizeof r, 1000, 12);   CHECK(a.cfg.profile == PROFILE_HIKE && a.settings_dirty && strncmp(r, "OK", 2) == 0);
    a.settings_dirty = false;
    a.runCommand("quiet 23-6", r, sizeof r, 1000, 12);     CHECK(a.cfg.quiet_on && a.cfg.quiet_start == 23 && a.cfg.quiet_end == 6 && a.settings_dirty);
    size_t n = a.saveSettings(buf, sizeof buf);            CHECK(n > 0);
    Nova b;                                                CHECK(b.loadSettings(buf));
    CHECK(b.cfg.profile == PROFILE_HIKE && b.cfg.quiet_on && b.cfg.quiet_start == 23 && b.cfg.quiet_end == 6);
    CHECK(b.link.cfg.expire_ms == profileParams(PROFILE_HIKE).expire_ms);   // loading re-applies the profile
    Nova c;
    CHECK(!c.loadSettings("") && !c.loadSettings("junk") && !c.loadSettings("NOVA2 1 0 22 7") && !c.loadSettings("NOVA1 5 0 22 7")
          && !c.loadSettings("NOVA1 1 2 22 7") && !c.loadSettings("NOVA1 1 1 24 7") && !c.loadSettings("NOVA1 1 1 22 -1"));
    CHECK(c.cfg.profile == PROFILE_CAR && !c.cfg.quiet_on);
    CHECK(a.saveSettings(buf, 5) == 0);
  }

  // 2. commands: bad input changes nothing and never sets the dirty flag
  {
    Nova a; char r[160];
    a.runCommand("profile bogus", r, sizeof r, 0, 12);     CHECK(!a.settings_dirty && a.cfg.profile == PROFILE_CAR);
    a.runCommand("quiet 5-5", r, sizeof r, 0, 12);         CHECK(strncmp(r, "Err", 3) == 0 && !a.cfg.quiet_on && !a.settings_dirty);
    a.runCommand("quiet 25-3", r, sizeof r, 0, 12);        CHECK(strncmp(r, "Err", 3) == 0 && !a.cfg.quiet_on);
    a.runCommand("quiet abc", r, sizeof r, 0, 12);         CHECK(strncmp(r, "Err", 3) == 0 && !a.cfg.quiet_on);
    a.runCommand("status", r, sizeof r, 0, 12);            CHECK(strstr(r, NOVA_PRODUCT) && !a.settings_dirty);
    a.runCommand("", r, sizeof r, 0, 12);                  CHECK(strstr(r, NOVA_PRODUCT));
    a.runCommand("wat", r, sizeof r, 0, 12);               CHECK(strstr(r, "nova ["));
    a.runCommand("quiet 22-7", r, sizeof r, 0, 12);        a.settings_dirty = false;
    a.runCommand("quiet off", r, sizeof r, 0, 12);         CHECK(!a.cfg.quiet_on && a.settings_dirty);
    a.runCommand("profile desk", r, sizeof r, 0, 12);      a.runCommand("defaults", r, sizeof r, 0, 12);
    CHECK(a.cfg.profile == PROFILE_CAR && a.link.cfg.expire_ms == profileParams(PROFILE_CAR).expire_ms);
  }

  // 3. quiet hours: inside, outside, wrapping midnight, clock not set, favourites
  {
    Nova a; a.cfg.quiet_on = true; a.cfg.quiet_start = 22; a.cfg.quiet_end = 7;
    CHECK(a.quietNow(23) && a.quietNow(0) && a.quietNow(6) && !a.quietNow(7) && !a.quietNow(12) && a.quietNow(22) && !a.quietNow(21));
    CHECK(!a.quietNow(-1));                                 // clock not set: never go silent by mistake
    CHECK(!a.mayAlert(false, 2) && a.mayAlert(true, 2) && a.mayAlert(false, 12));
    a.cfg.quiet_start = 9; a.cfg.quiet_end = 17;            // a daytime window that does not wrap
    CHECK(a.quietNow(9) && a.quietNow(16) && !a.quietNow(17) && !a.quietNow(8));
    a.cfg.quiet_on = false;                                 CHECK(!a.quietNow(10));
  }

  // 4. deliveries: sent -> repeated -> delivered
  {
    DeliveryLog L; uint8_t h[LODESTAR_HASH_LEN]; hashOf(h, 1);
    L.onSend(111, true, 10000, "Bob", true, 1000);
    CHECK(L.count() == 1 && L.pending() == 1 && L.stats.sent == 1);
    L.onTxText(h, 1200);
    L.onHeard(h, 0x3F);
    const Delivery* d = L.newest(0);                         CHECK(d && d->state == D_REPEATED && d->via == 0x3F && L.stats.repeated == 1);
    L.onHeard(h, 0x40);                                      CHECK(L.stats.repeated == 1 && d->via == 0x3F);   // only the first echo counts
    L.onAck(111, 5200);
    CHECK(d->state == D_DELIVERED && d->rtt_ms == 4200 && L.stats.delivered == 1 && L.pending() == 0);
    L.onAck(111, 6000);                                      CHECK(L.stats.delivered == 1);                  // a duplicate ack changes nothing
    char line[40]; DeliveryLog::line(*d, 6000, line, sizeof line);   CHECK(strstr(line, "Bob") && strstr(line, "ok 4s"));
  }

  // 5. deliveries: timeout, late ack, no ack code, direct sends, unrelated echo
  {
    DeliveryLog L; uint8_t h[LODESTAR_HASH_LEN], h2[LODESTAR_HASH_LEN]; hashOf(h, 2); hashOf(h2, 3);
    L.onSend(0, true, 5000, "Nobody", true, 0);              CHECK(L.count() == 0);                          // no ack code: not tracked
    L.onSend(222, true, 5000, "Ana", false, 1000);
    L.onTxText(h, 1100);
    L.onHeard(h2, 0x11);                                     CHECK(L.newest(0)->state == D_SENT);           // someone else's packet
    L.loop(5000);                                            CHECK(L.newest(0)->state == D_SENT);           // not yet (timeout 5s + 2s grace)
    L.loop(9000);                                            CHECK(L.newest(0)->state == D_SENT);           // a slow mesh: an ack 18 s later is normal
    L.loop(20000);                                           CHECK(L.newest(0)->state == D_SENT);
    L.loop(32000);                                           CHECK(L.newest(0)->state == D_FAILED && L.stats.failed == 1);   // direct messages get at least 30 s
    char line[40]; DeliveryLog::line(*L.newest(0), 32000, line, sizeof line);   CHECK(strstr(line, "FAILED") && strstr(line, "no link"));
    L.onAck(222, 36000);                                     CHECK(L.newest(0)->state == D_DELIVERED && L.stats.failed == 0 && L.stats.delivered == 1);   // it did arrive
    L.onSend(333, false, 5000, "Cy", true, 20000);           L.onTxText(h, 20100);
    CHECK(L.newest(0)->state == D_SENT && !L.newest(0)->hash_set);                                          // a direct send has no echo to bind
    L.onSend(1, true, 100, "Fast", true, 0);                 CHECK(L.newest(0)->timeout_ms >= 3000);        // timeout never absurdly short
  }

  // 5b. channel messages: no ack exists; a repeater echo is the best outcome, silence is "unheard"
  {
    DeliveryLog L; uint8_t h[LODESTAR_HASH_LEN], h2[LODESTAR_HASH_LEN]; hashOf(h, 20); hashOf(h2, 21);
    L.onSendChannel("Public", true, 1000);                   CHECK(L.count() == 1 && L.stats.sent == 1 && L.pending() == 1 && L.newest(0)->chan);
    L.onTxText(h, 1100); L.onHeard(h, 0x3F);
    CHECK(L.newest(0)->state == D_REPEATED && L.pending() == 0);
    L.loop(60000);                                           CHECK(L.newest(0)->state == D_REPEATED && L.stats.failed == 0);   // an echo is final for a channel
    L.onAck(0, 61000);                                       CHECK(L.newest(0)->state == D_REPEATED && L.stats.delivered == 0);
    L.onSendChannel("Public", true, 70000);                  L.onTxText(h2, 70100);
    L.loop(75000);                                           CHECK(L.newest(0)->state == D_SENT);
    L.loop(83000);                                           CHECK(L.newest(0)->state == D_FAILED && L.stats.failed == 1);
    char line[40]; DeliveryLog::line(*L.newest(0), 83000, line, sizeof line);   CHECK(strstr(line, "unheard"));
  }

  // 6. deliveries: ring keeps the newest, order newest-first; binding is first-in first-out
  {
    DeliveryLog L; uint8_t h[LODESTAR_HASH_LEN];
    for (int i = 1; i <= 12; i++) L.onSend((uint32_t)i, true, 5000, "X", true, (uint32_t)i * 100);
    CHECK(L.count() == NOVA_MAX_DELIVERIES);
    CHECK(L.newest(0)->ack == 12 && L.newest(7)->ack == 5 && L.newest(8) == nullptr);
    hashOf(h, 9); L.onTxText(h, 5000);                       // binds to the OLDEST unbound (ack 5)
    L.onHeard(h, 0x22);
    int found = 0; for (int i = 0; i < 8; i++) if (L.newest(i)->state == D_REPEATED) { found++; CHECK(L.newest(i)->ack == 5); }
    CHECK(found == 1);
  }

  // 7. range verdict is judged against the spreading-factor floor
  {
    CHECK(rangeVerdict(0, 10, 10) == RANGE_NONE);
    CHECK(rangeVerdict(3, 0.0f, 10) == RANGE_GOOD);          // SF10 floor is -15 dB: 0 dB is 15 dB of margin
    CHECK(rangeVerdict(1, 0.0f, 10) == RANGE_OK);            // good signal but only one repeater answered
    CHECK(rangeVerdict(2, -10.0f, 10) == RANGE_OK);          // 5 dB over the floor
    CHECK(rangeVerdict(2, -12.0f, 10) == RANGE_WEAK);        // 3 dB over the floor
    CHECK(rangeVerdict(2, 0.0f, 7) == RANGE_OK);             // same 0 dB is only 7.5 dB over the SF7 floor
    CHECK(strcmp(rangeName(RANGE_GOOD), "GOOD") == 0 && strcmp(rangeName(9), "NONE") == 0);
  }

  // 8. trip: distance, jitter ignored, teleports ignored, top speed, time
  {
    Nova N; int32_t lat = 30000000, lon = -97000000; uint32_t t = 100000;
    t = drive(N, t, 60, lat, lon, 15.0);                      // one minute at 54 km/h
    CHECK(N.trip.dist_m > 800 && N.trip.dist_m < 960);       // about 900 m
    CHECK(N.trip.top_mps > 13.0f && N.trip.top_mps < 17.0f);
    CHECK(N.trip.moving_ms > 50000 && N.trip.total_ms >= N.trip.moving_ms);
    double d0 = N.trip.dist_m;
    for (int i = 0; i < 60; i++, t += 1000) { N.link.onPosition(lat + (i % 2) * 5, lon, 9, true, t); N.loop(t, 3900, false); }   // 0.5 m wobble while parked
    CHECK(N.trip.dist_m - d0 < 5.0);
    double d1 = N.trip.dist_m;
    N.link.onPosition(lat + 5000000, lon, 9, true, t); N.loop(t, 3900, false); t += 1000;       // a 550 km jump (a bad fix)
    CHECK(N.trip.dist_m - d1 < 1.0);
  }

  // 9. trip: a lost fix is not bridged; track decimation keeps the whole route
  {
    Nova N; int32_t lat = 30000000, lon = -97000000; uint32_t t = 100000;
    t = drive(N, t, 20, lat, lon, 20.0);
    double d = N.trip.dist_m;
    t += 60000; lat += 2000000;                               // lost the fix, reappeared 220 km away (a tunnel, a long drive)
    t = drive(N, t, 5, lat, lon, 20.0);
    CHECK(N.trip.dist_m - d < 400.0);
    Nova M; lat = 30000000; t = 100000;
    t = drive(M, t, 3000, lat, lon, 25.0);                    // 75 km: far more than 64 points at 25 m
    CHECK(M.trip.trackCount() <= NOVA_TRACK_POINTS && M.trip.trackCount() >= NOVA_TRACK_POINTS / 2);
    CHECK(M.trip.trackAt(0).lat_e6 <= 30000000 + 2000);                       // still starts at the start
    CHECK(M.trip.trackAt(M.trip.trackCount() - 1).lat_e6 > lat - 4000000 / 20);   // and reaches near the end
  }

  // 10. coverage: remembers the last place with a usable uplink, and points back to it
  {
    Nova N; int32_t lat = 30000000, lon = -97000000; uint32_t t = 100000;
    CHECK(!N.trip.haveCoverage());
    uint8_t key[8] = {0xA1,0xB2,0xC3,0,0,0,0,0};
    for (int i = 0; i < 12; i++, t += 20000) N.link.onAdvert(key, 9.0f, -80, t);         // a strong repeater
    t = drive(N, t, 30, lat, lon, 15.0);                      // driving north while it is heard
    CHECK(N.linkUsable() && N.trip.haveCoverage());
    int32_t cov_lat = N.trip.covLat();
    for (int i = 0; i < 40; i++) { t += 60000; N.loop(t, 3900, false); }                  // 40 minutes with nothing heard: it expires
    CHECK(!N.linkUsable());
    t = drive(N, t, 60, lat, lon, 15.0);                      // keep driving north without coverage
    CHECK(N.trip.covLat() == cov_lat);                        // still the old place
    double dist, brg;
    const lodestar::Position& p = N.link.position();
    CHECK(N.trip.toCoverage(p.lat_e6, p.lon_e6, dist, brg));
    CHECK(brg > 175.0 && brg < 185.0);                        // it is behind us: due south
    CHECK(dist > 800 && dist < 1100);                         // about 60 s at 15 m/s
  }

  // 11. battery: percent curve, trend, hours left, charging resets the trend
  {
    CHECK(Battery::percent(4300) == 100 && Battery::percent(3000) == 0 && Battery::percent(3850) == 55);
    int prev = 101; bool mono = true; for (int mv = 4250; mv >= 3200; mv -= 10) { int p = Battery::percent(mv); if (p > prev) mono = false; prev = p; }
    CHECK(mono);
    Battery b; uint32_t t = 0; float tr, hl;
    CHECK(b.pct() == -1);
    b.update(100, false, t);  CHECK(b.pct() == -1 && b.samples() == 0);                     // 100 mV is not a battery reading
    // drain about 4 percent per hour: ~ 15 mV/h around 3800 mV; one reading every 10 s for 3 hours
    for (int i = 0; i < 1080; i++, t += 10000) b.update((uint16_t)(3950 - (int)(t / 3600000.0 * 40.0)), false, t);
    CHECK(b.trendPerHour(tr) && tr < -1.0f && tr > -15.0f);
    CHECK(b.hoursLeft(hl) && hl > 1.0f && hl <= 99.0f);
    b.update(4100, true, t);                                  // plugged in
    CHECK(b.charging() && !b.trendPerHour(tr) && !b.hoursLeft(hl));
    Battery c; t = 0; for (int i = 0; i < 400; i++, t += 10000) c.update(3800, false, t);
    CHECK(!c.hoursLeft(hl));                                  // flat line: no estimate rather than "99 hours"
    Battery d; t = 0; for (int i = 0; i < 100; i++, t += 10000) d.update(3800, false, t);
    CHECK(!d.trendPerHour(tr));                               // under 30 minutes of data: no trend
    Battery e; t = 0; for (int i = 0; i < 4000; i++, t += 10000) e.update(3900, false, t);
    CHECK(e.sampleCount() <= NOVA_BATT_SAMPLES);              // history is bounded
  }

  // 12. profile changes what LODESTAR does with old neighbours
  {
    Nova car; car.cfg.profile = PROFILE_CAR; car.applyProfile();
    Nova desk; desk.cfg.profile = PROFILE_DESK; desk.applyProfile();
    CHECK(car.screenMs() < desk.screenMs() && car.link.cfg.expire_ms < desk.link.cfg.expire_ms);
    CHECK(profileParams(77).screen_ms == profileParams(PROFILE_CAR).screen_ms);          // out-of-range falls back to the car profile
  }

  // 13. dashboard line: fits, is one line, starts and ends right, names are neutralised
  {
    Nova N; int32_t lat = 30000000, lon = -97000000; uint32_t t = 100000; static char out[6000];
    uint8_t k1[8] = {0xA1,0xB2,0xC3}, k2[8] = {0x44,0x55,0x66};
    for (int i = 0; i < 12; i++, t += 20000) { N.link.onAdvert(k1, 9.0f, -80, t); N.link.onAdvert(k2, -8.0f, -112, t); }
    t = drive(N, t, 100, lat, lon, 12.0);
    N.deliveries.onSend(77, true, 8000, "Bob \"the\" \\ \xC3\xA9\n", true, t);
    N.diag.onRx(5.5f, t);
    size_t n = N.json(out, sizeof out, t, 12);
    CHECK(n > 100 && n == strlen(out) && out[0] == '{' && out[n - 1] == '}' && !strchr(out, '\n'));
    CHECK(strstr(out, "\"nova\":1") && strstr(out, "\"links\":[{\"id\":\"A1B2C3\"") && strstr(out, "\"gps\":{\"fix\":1"));
    CHECK(strstr(out, "Bob ?the? ? ?\""));                                                // quote, backslash and UTF-8 all neutralised (name is cut to 13 bytes)
    if (argc > 1) { FILE* f = fopen(argv[1], "w"); if (f) { fputs(out, f); fputc('\n', f); fclose(f); } }
    char tiny[100]; CHECK(N.json(tiny, sizeof tiny, t, 12) == 0);                           // too small: reports failure, never overruns
    Nova E; size_t m = E.json(out, sizeof out, 5000, -1);                                    // a fresh node with nothing yet
    CHECK(m > 50 && strstr(out, "\"links\":[]") && strstr(out, "\"cov\":null") && strstr(out, "\"fix\":0"));
  }

  // 14. dashboard stream: only runs while the page keeps asking
  {
    Nova N; char r[80];
    CHECK(!N.dash_on);
    N.runCommand("hello", r, sizeof r, 1000, 12);              CHECK(N.dash_on);
    N.loop(5000, 3900, false);                                  CHECK(N.dash_on);
    N.loop(12000, 3900, false);                                 CHECK(!N.dash_on);                  // stopped by itself
    N.runCommand("hello", r, sizeof r, 0xFFFFFFF0u, 12);        N.loop(0x00000010u, 3900, false);   CHECK(N.dash_on);   // survives millis() wrapping
  }

  // 15. ranking, trend, local hour
  {
    Nova N; uint32_t t = 100000; int idx[8];
    uint8_t a[8] = {1,1,1}, b[8] = {2,2,2}, c[8] = {3,3,3};
    for (int i = 0; i < 12; i++, t += 20000) { N.link.onAdvert(a, -10.0f, -118, t); N.link.onAdvert(b, 9.0f, -80, t); N.link.onAdvert(c, 0.0f, -100, t); }
    int n = N.ranked(idx, 8, t);
    CHECK(n == 3 && N.link.entry(idx[0]).key[0] == 2 && N.link.entry(idx[2]).key[0] == 1);
    CHECK(N.ranked(idx, 2, t) == 2);
    Nova E; CHECK(E.ranked(idx, 8, 1000) == 0);
    CHECK(Nova::localHour(0, -5) == -1 && Nova::localHour(1767225600u + 3600u * 14, 0) == 14 && Nova::localHour(1767225600u + 3600u * 2, -5) == 21 && Nova::localHour(1767225600u + 3600u * 22, 5) == 3);
    // trend: a falling best score reads as worse, a rising one as better, a steady one as 0
    Nova T; uint8_t k[8] = {9,9,9}; uint32_t u = 100000;
    CHECK(T.scoreTrend() == 0);
    for (int s = 0; s < 260; s++, u += 1000) { if (s % 20 == 0) T.link.onAdvert(k, 9.0f, -80, u); T.loop(u, 3900, false); }   // steady, still being heard
    CHECK(T.scoreTrend() == 0);
    for (int s = 0; s < 200; s++, u += 1000) T.loop(u, 3900, false);                         // 200 s of silence: it fades
    CHECK(T.scoreTrend() == -1);
  }

  // 15b. repeater names from adverts
  {
    auto advert = [](uint8_t* p, uint8_t k0, uint8_t type, bool latlon, const char* name, size_t* len) {
      memset(p, 0, 400); p[0] = k0; p[1] = 0xA0; p[2] = 0xB0; p[3] = 1; p[4] = 2;
      size_t o = 100; p[o++] = (uint8_t)(type | 0x80 | (latlon ? 0x10 : 0));
      if (latlon) { memset(p + o, 7, 8); o += 8; }
      size_t n = strlen(name); memcpy(p + o, name, n); *len = o + n;
    };
    NameBook B; uint8_t p[400]; size_t len;
    advert(p, 0x11, 2, true, "Hill Top RPT", &len);          B.onAdvert(p, len);
    CHECK(strcmp(B.get(p, 3), "Hill Top RPT") == 0);
    CHECK(strcmp(B.get(p, 1), "Hill Top RPT") == 0);                                   // a one-byte hash resolves when it is unique
    advert(p, 0x11, 2, false, "Other Site", &len);           p[1] = 0xC0; B.onAdvert(p, len);
    uint8_t k1[3] = {0x11, 0, 0};                            CHECK(strcmp(B.get(k1, 1), "") == 0);   // two repeaters share it: no guess
    advert(p, 0x22, 1, false, "A Chat Node", &len);          B.onAdvert(p, len);
    uint8_t k2[3] = {0x22, 0xA0, 0xB0};                      CHECK(strcmp(B.get(k2, 3), "") == 0);   // companions are not repeaters
    advert(p, 0x33, 2, false, "Caf\xC3\xA9 Roof Node With A Very Long Name", &len);
    B.onAdvert(p, len);
    uint8_t k3[3] = {0x33, 0xA0, 0xB0};
    const char* nm = B.get(k3, 3);                           CHECK(strlen(nm) > 0 && strlen(nm) < NOVA_NAME_LEN);
    bool utf_ok = true; for (const char* c = nm; *c; c++) { if (((uint8_t)*c & 0xC0) == 0xC0 && !c[1]) utf_ok = false; }
    CHECK(utf_ok);
    advert(p, 0x44, 2, false, "Quote\"Back\\slash", &len);   B.onAdvert(p, len);
    Nova N; N.names.onAdvert(p, len);                                                     // JSON must stay valid with awkward names
    for (int r = 0; r < 12; r++) { uint8_t kk[8] = {0x44, 0xA0, 0xB0}; N.link.onAdvert(kk, 5.0f, -90, 1000u + (uint32_t)r * 20000); }
    static char out[6144]; N.json(out, sizeof out, 300000, 12);
    CHECK(strstr(out, "\"name\":\"Quote?Back?slash\""));                       // quotes and backslashes are swapped for ? so the line stays valid JSON
    advert(p, 0x55, 2, false, "", &len);                     B.onAdvert(p, len);          // an advert with an empty name changes nothing
    uint8_t k5[3] = {0x55, 0xA0, 0xB0};                      CHECK(strcmp(B.get(k5, 3), "") == 0);
  }

  // 15c. found nodes (the wardriving log)
  {
    auto adv = [](uint8_t* p, uint8_t k0, uint8_t type, int32_t lat, int32_t lon, const char* name, size_t* len) {
      memset(p, 0, 400); p[0] = k0; p[1] = 0xA0; p[2] = 0xB0; p[3] = 0xC0; p[4] = 0xD0; p[5] = 0xE0;
      size_t o = 100; bool ll = lat != 0 || lon != 0; p[o++] = (uint8_t)(type | 0x80 | (ll ? 0x10 : 0));
      if (ll) { for (int i = 0; i < 4; i++) { p[o + i] = (uint8_t)((uint32_t)lat >> (8 * i)); p[o + 4 + i] = (uint8_t)((uint32_t)lon >> (8 * i)); } o += 8; }
      size_t n = strlen(name); memcpy(p + o, name, n); *len = o + n;
    };
    FoundBook F; uint8_t p[400]; size_t len; char line[320];
    CHECK(F.count() == 0 && F.nextLine(line, sizeof line, 1000) == 0);
    adv(p, 0x01, 2, 30267200, -97743100, "Hill RPT", &len);
    CHECK(F.onAdvert(p, len, 0, 6.5f, -95, true, 30270000, -97740000, 10000));                  // new repeater with its own location
    adv(p, 0x02, 1, 0, 0, "Sam's phone", &len);
    CHECK(F.onAdvert(p, len, 2, -3.0f, -110, false, 0, 0, 11000));                              // companion, no location, no GPS fix
    CHECK(F.count() == 2 && F.count(NODE_REPEATER) == 1 && F.count(NODE_COMPANION) == 1 && F.dirtyCount() == 2);
    CHECK(F.nextLine(line, sizeof line, 20000) > 0);
    CHECK(strstr(line, "\"t\":2") && strstr(line, "\"n\":\"Hill RPT\"") && strstr(line, "\"alat\":30.267200,\"alon\":-97.743100"));
    CHECK(strstr(line, "\"hlat\":30.270000,\"hlon\":-97.740000") && strstr(line, "\"hop\":0") && strstr(line, "\"sn\":6.5"));
    CHECK(strstr(line, "\"ago\":10") && strstr(line, "\"first\":10"));
    CHECK(F.nextLine(line, sizeof line, 20000) > 0);
    CHECK(strstr(line, "\"t\":1") && strstr(line, "alat") == nullptr && strstr(line, "hlat") == nullptr && strstr(line, "\"hop\":2"));   // nothing is invented
    CHECK(F.nextLine(line, sizeof line, 20000) == 0 && F.dirtyCount() == 0);
    adv(p, 0x01, 2, 30267200, -97743100, "Hill RPT", &len);
    CHECK(!F.onAdvert(p, len, 0, 6.6f, -95, true, 30280000, -97750000, 21000));                  // same node, barely better: nothing to report
    CHECK(F.onAdvert(p, len, 0, 9.0f, -90, true, 30280000, -97750000, 22000));                   // clearly better: the position it was heard at moves
    CHECK(F.nextLine(line, sizeof line, 22000) > 0 && strstr(line, "\"hlat\":30.280000") && strstr(line, "\"c\":3"));
    adv(p, 0x02, 1, 0, 0, "Sam's phone", &len);
    CHECK(F.onAdvert(p, len, 0, -8.0f, -118, true, 30281000, -97751000, 23000));                 // heard directly now beats relayed, and we have a fix
    CHECK(F.nextLine(line, sizeof line, 23000) > 0 && strstr(line, "\"hop\":0") && strstr(line, "\"hlat\":30.281000"));
    F.markAllDirty(); CHECK(F.dirtyCount() == 2);
    // bad input is ignored
    adv(p, 0x03, 5, 0, 0, "x", &len);                         CHECK(!F.onAdvert(p, len, 0, 1, -90, false, 0, 0, 1));   // unknown type
    adv(p, 0x03, 2, 95000000, 0, "x", &len);                  CHECK(F.onAdvert(p, len, 0, 1, -90, false, 0, 0, 1) && F.count() == 3);   // absurd latitude is dropped, node kept
    { char l3[320]; F.nextLine(l3, sizeof l3, 2); F.nextLine(l3, sizeof l3, 2); F.nextLine(l3, sizeof l3, 2); }
    CHECK(!F.onAdvert(p, 50, 0, 1, -90, false, 0, 0, 1));                                      // too short to be an advert
    adv(p, 0x04, 2, 0, 0, "Quote\"Back\\slash", &len);        F.onAdvert(p, len, 0, 1, -90, false, 0, 0, 1);
    { bool ok = false; for (int i = 0; i < 4 && !ok; i++) { size_t m = F.nextLine(line, sizeof line, 5); ok = m && strstr(line, "\"n\":\"Quote?Back?slash\""); } CHECK(ok); }
    CHECK(F.nextLine(line, 40, 5) == 0);                                                          // a buffer too small never writes a broken line
    // the table is bounded: the oldest is dropped
    FoundBook G;
    for (int i = 0; i < NOVA_FOUND_SLOTS + 10; i++) { adv(p, (uint8_t)(i + 1), 2, 0, 0, "n", &len); p[1] = (uint8_t)i; G.onAdvert(p, len, 0, 1, -90, false, 0, 0, 1000u + (uint32_t)i); }
    CHECK(G.count() == NOVA_FOUND_SLOTS);
    // and the full command + the status line carry the edition
    Nova N; char r[160]; N.runCommand("found", r, sizeof r, 0, 12);   CHECK(strstr(r, "found 0 repeaters"));
    adv(p, 0x09, 2, 0, 0, "R", &len); N.found.onAdvert(p, len, 0, 1, -90, false, 0, 0, 1);
    N.runCommand("found", r, sizeof r, 0, 12);                        CHECK(strstr(r, "found 1 repeaters, 0 companions"));
    N.runCommand("found clear", r, sizeof r, 0, 12);                  CHECK(N.found.count() == 0);
    static char o2[6144]; N.json(o2, sizeof o2, 5000, 12);            CHECK(strstr(o2, "\"ed\":\"" NOVA_EDITION "\"") && strstr(o2, NOVA_VERSION));
  }

  // 16. worst case dashboard line still fits its buffer
  {
    Nova N; static char out[6144]; uint32_t t = 100000; int32_t lat = 30000000, lon = -97000000;
    for (int i = 0; i < LODESTAR_MAX_UPLINKS; i++) { uint8_t ap[400]; memset(ap, 0, sizeof ap); ap[0] = (uint8_t)(i + 1); ap[1] = (uint8_t)(0xA0 + i); ap[2] = (uint8_t)(0xF0 - i); ap[100] = 0x82; memset(ap + 101, 'W', 23); N.names.onAdvert(ap, 124); }
    for (int i = 0; i < LODESTAR_MAX_UPLINKS; i++) { uint8_t k[8] = {(uint8_t)(i + 1), (uint8_t)(0xA0 + i), (uint8_t)(0xF0 - i)}; for (int r = 0; r < 12; r++) N.link.onAdvert(k, (float)(i - 12), -90 - i, t + (uint32_t)r * 20000); }
    t += 12 * 20000;
    for (int i = 0; i < 8; i++) N.deliveries.onSend((uint32_t)i + 1, true, 9000, "ABCDEFGHIJKLM", true, t);
    { size_t n0 = N.json(out, sizeof out, t, 12); int named = 0; for (const char* q = out; (q = strstr(q, "\"name\":\"WWW")); q++) named++;
      CHECK(named >= 6 && n0 < sizeof out - 1500); printf("eight named repeaters (%d shown) + eight deliveries: %zu bytes\n", named, n0); }   // while the repeaters are fresh
    t = drive(N, t, 6000, lat, lon, 30.0);                                                    // 180 km: track full and thinned
    for (int i = 0; i < 4000; i++, t += 10000) N.batt.update((uint16_t)(4000 - i / 10), false, t);
    N.diag.onRx(-3.5f, t);
    size_t n = N.json(out, sizeof out, t, 12);
    CHECK(n > 2000 && n < sizeof out - 1000);                                                  // fits with room to spare
    printf("worst-case dashboard line: %zu bytes of %zu\n", n, sizeof out);
  }

  printf(fails ? "\n%d FAILED\n" : "\nall tests passed\n", fails);
  return fails ? 1 : 0;
}
