#!/usr/bin/env python3
"""
apply_nova.py - add LODESTAR NOVA to a MeshCore checkout.

  python tools/apply_nova.py --meshcore MeshCore

Copies nova/Nova.h, lodestar/Lodestar.h and nova/ui/NovaPages.h into MeshCore and patches
examples/companion_radio (MyMesh.h / MyMesh.cpp / ui-new/UITask.cpp). Every edit must match
exactly once (or the stated number of times); if upstream MeshCore moved something the
script stops and says which anchor it could not find, instead of leaving a half-patched tree.
"""
import argparse
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
NOVA_H = os.path.join(ROOT, "nova", "Nova.h")
LODESTAR_H = os.path.join(ROOT, "lodestar", "Lodestar.h")
PAGES_H = os.path.join(ROOT, "nova", "ui", "NovaPages.h")


def die(msg):
    print(f"\nERROR: {msg}\n", file=sys.stderr)
    sys.exit(1)


def patch(text, pattern, repl, what, count=1, flags=0):
    """One regex replacement; it must match exactly `count` places."""
    found = re.findall(pattern, text, flags)
    if len(found) != count:
        die(f"Upstream MeshCore changed: expected {count} match(es) for '{what}', found {len(found)}. "
            f"The anchor for this edit in tools/apply_nova.py needs updating.")
    return re.sub(pattern, lambda m: repl(m) if callable(repl) else repl, text, flags=flags)


# --------------------------------------------------------------------------
# C++ inserted into MyMesh.cpp
# --------------------------------------------------------------------------
MYMESH_DEFS = r'''// ---- LODESTAR NOVA ---------------------------------------------------------------
nova::Nova nova_core;

// Every packet the radio decodes. Feeds the uplink table and the delivery tracker.
void MyMesh::logRx(mesh::Packet* pkt, int len, float score) {
  (void)len; (void)score;
  uint32_t nv_now = millis();
  nova_core.diag.onRx(pkt->getSNR(), nv_now);
  nova_core.link.cfg.sf = _prefs.sf;
  if (pkt->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
    nova_core.names.onAdvert(pkt->payload, pkt->payload_len);   // repeater names
    const lodestar::Position& nv_pos = nova_core.link.position();   // wardriving log: who we heard, and where we were
    nova_core.found.onAdvert(pkt->payload, pkt->payload_len, pkt->getPathHashCount(), pkt->getSNR(), (int)_radio->getLastRSSI(),
                             nova_core.link.hasFix(nv_now), nv_pos.lat_e6, nv_pos.lon_e6, nv_now);
  }
  uint8_t nv_n = pkt->getPathHashCount();
  uint8_t nv_sz = pkt->getPathHashSize();
  if (nv_n * nv_sz > MAX_PATH_SIZE) return;
  if (pkt->getPayloadType() == PAYLOAD_TYPE_ADVERT && nv_n == 0 && pkt->payload_len > PUB_KEY_SIZE + 4 + SIGNATURE_SIZE) {
    // a zero-hop advert: a node we hear directly. Only repeaters count as uplinks.
    uint8_t nv_flags = pkt->payload[PUB_KEY_SIZE + 4 + SIGNATURE_SIZE];
    if ((nv_flags & 0x0F) == ADV_TYPE_REPEATER) {
      nova_core.link.onAdvert(pkt->payload, pkt->getSNR(), _radio->getLastRSSI(), nv_now);
    }
  }
  if (pkt->isRouteFlood() && nv_n > 0) {
    // the last hop in the path is the repeater we just heard directly
    const uint8_t* nv_last = &pkt->path[(nv_n - 1) * nv_sz];
    int nv_from = nova_core.link.onHeardFrom(nv_last, nv_sz, pkt->getSNR(), _radio->getLastRSSI(), nv_now);
    if (pkt->getPayloadType() == PAYLOAD_TYPE_ADVERT && nv_n >= 2) nova_core.link.noteReach(nv_from, nv_n - 1);
    uint8_t nv_hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(nv_hash);
    nova_core.link.onFloodCopy(nv_hash, pkt->path, nv_n, nv_sz, nv_from, nv_now);   // credits implicit ACKs; forwarding is not ours to cancel
    nova_core.deliveries.onHeard(nv_hash, nv_last[0]);
  }
}

// Every packet we put on the air.
void MyMesh::logTx(mesh::Packet* pkt, int len) {
  nova_core.diag.onTx();
  if (pkt->isRouteFlood()) {
    uint8_t nv_hash[MAX_HASH_SIZE];
    pkt->calculatePacketHash(nv_hash);
    uint32_t nv_now = millis();
    if (pkt->getPathHashCount() == 0 && (pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG || pkt->getPayloadType() == PAYLOAD_TYPE_GRP_TXT)) {
      nova_core.deliveries.onTxText(nv_hash, nv_now);        // our own message, not one we are relaying
    }
    uint32_t nv_win = _radio->getEstAirtimeFor(len) * 12;
    if (nv_win < 4000) nv_win = 4000;
    if (nv_win > 15000) nv_win = 15000;
    nova_core.link.onTx(nv_hash, pkt->path, pkt->getPathHashCount(), pkt->getPathHashSize(), nv_now, nv_win);
  }
}

// Called every pass of loop(): GPS to NOVA, settings to flash, text commands and the dashboard stream.
void MyMesh::novaTick(uint32_t nv_now) {
#if ENV_INCLUDE_GPS == 1
  {
    LocationProvider* nv_gps = sensors.getLocationProvider();
    if (nv_gps != NULL && nv_gps->isEnabled()) {
      nova_core.link.onPosition((int32_t)nv_gps->getLatitude(), (int32_t)nv_gps->getLongitude(),
                                (uint8_t)nv_gps->satellitesCount(), nv_gps->isValid(), nv_now);
    }
  }
#endif
  static uint32_t nv_next_batt = 0;
  static uint16_t nv_mv = 0;
  if ((int32_t)(nv_now - nv_next_batt) >= 0) {       // the battery is read every 5 s, not every pass
    nv_mv = board.getBattMilliVolts();
    nv_next_batt = nv_now + 5000;
  }
  nova_core.loop(nv_now, nv_mv, board.isExternalPowered());

  if (nova_core.settings_dirty) {                    // a setting changed: write it to flash (rare)
    nova_core.settings_dirty = false;
    char nv_buf[96];
    size_t nv_n = nova_core.saveSettings(nv_buf, sizeof(nv_buf));
    if (nv_n > 0) {
      FILESYSTEM* nv_fs = _store->getPrimaryFS();
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
      nv_fs->remove("/nova.cfg");
      File nv_f = nv_fs->open("/nova.cfg", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
      File nv_f = nv_fs->open("/nova.cfg", "w");
#else
      File nv_f = nv_fs->open("/nova.cfg", "w", true);
#endif
      if (nv_f) { nv_f.write((const uint8_t*)nv_buf, nv_n); nv_f.close(); }
    }
  }

#ifndef ENABLE_USB_INTERFACE
  // The USB port is free in this build (the phone talks over Bluetooth), so the dashboard
  // page uses it. Lines starting with "nova" are commands; while a page keeps saying
  // "nova hello" the node sends one JSON line every second. Nothing is sent otherwise.
  if (_cli_rescue) return;
  static char nv_line[96];
  static size_t nv_len = 0;
  static uint32_t nv_next_json = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (nv_len < sizeof(nv_line) - 1) nv_line[nv_len++] = c; continue; }
    nv_line[nv_len] = 0;
    nv_len = 0;
    if (strncmp(nv_line, "nova", 4) != 0 || (nv_line[4] != 0 && nv_line[4] != ' ')) continue;
    int nv_hour = nova::Nova::localHour(getRTCClock()->getCurrentTime(), _prefs.tz_offset);
    if (strncmp(nv_line + 4, " json", 5) == 0) { nv_next_json = 0; nova_core.dash_on = true; nova_core.dash_until_ms = nv_now + 4000u; continue; }
    char nv_reply[160];
    nova_core.runCommand(nv_line + 4, nv_reply, sizeof(nv_reply), nv_now, nv_hour);
    Serial.print("# ");
    Serial.println(nv_reply);
  }
  static bool nv_was_on = false;
  if (nova_core.dash_on && !nv_was_on) nova_core.found.markAllDirty();   // a page just connected: send it everything found so far
  nv_was_on = nova_core.dash_on;
  if (nova_core.dash_on && (int32_t)(nv_now - nv_next_json) >= 0) {
    nv_next_json = nv_now + 1000u;
    static char nv_json[6144];
    int nv_hour = nova::Nova::localHour(getRTCClock()->getCurrentTime(), _prefs.tz_offset);
    size_t nv_n = nova_core.json(nv_json, sizeof(nv_json), nv_now, nv_hour);
    if (nv_n > 0) {
      Serial.write((const uint8_t*)nv_json, nv_n);
      Serial.write('\n');
    }
    static char nv_found[320];   // newly found or improved nodes, a few per second
    for (int nv_i = 0; nv_i < 6; nv_i++) {
      size_t nv_m = nova_core.found.nextLine(nv_found, sizeof(nv_found), nv_now);
      if (nv_m == 0) break;
      Serial.write((const uint8_t*)nv_found, nv_m);
      Serial.write('\n');
    }
  }
#endif
}

'''

LOAD_SETTINGS = r'''  {  // --- NOVA: restore saved settings (if any); anything invalid is ignored ---
    FILESYSTEM* nv_fs = _store->getPrimaryFS();
    File nv_f = _store->openRead(nv_fs, "/nova.cfg");
    if (nv_f) {
      char nv_buf[96];
      int nv_n = nv_f.read((uint8_t*)nv_buf, sizeof(nv_buf) - 1);
      nv_f.close();
      if (nv_n > 0) { nv_buf[nv_n] = 0; nova_core.loadSettings(nv_buf); }
    }
  }
'''

SEND_HOOK = r'''          nova_core.deliveries.onSend(expected_ack, result == MSG_SEND_SENT_FLOOD, est_timeout, recipient->name,
                                      nova_core.linkUsable(), _ms->getMillis());
'''

CHAN_HOOK = r'''        nova_core.deliveries.onSendChannel(channel.name, nova_core.linkUsable(), _ms->getMillis());   // NOVA
'''

ACK_HOOK = r'''  {  // --- NOVA: an acknowledgement arrived ---
    uint32_t nv_ack;
    memcpy(&nv_ack, data, 4);
    nova_core.deliveries.onAck(nv_ack, _ms->getMillis());
  }
'''

# --------------------------------------------------------------------------
# C++ inserted into UITask.cpp
# --------------------------------------------------------------------------
ENUM_PAGES = '''    FIRST,
    NV_LINK,      // LODESTAR NOVA pages
    NV_SENT,
    NV_TRIP,
    NV_BACK,
    NV_BATT,
    NV_RANGE,
    RECENT,
'''

SETTINGS_ENUM = '''#if UI_DISCOVER_SCREEN
    DISCOVERY,
#endif
    NV_SET,
    NV_QUIET,
'''

RENDER_PAGES = r'''    } else if (_page == HomePage::NV_LINK) {
      novaui::pageLink(display, nova_core, millis());
    } else if (_page == HomePage::NV_SENT) {
      novaui::pageSent(display, nova_core, millis());
    } else if (_page == HomePage::NV_TRIP) {
      novaui::pageTrip(display, nova_core, millis(), _task->getGPSState(), novaui::tripIsArmed(millis()));
    } else if (_page == HomePage::NV_BACK) {
      novaui::pageBack(display, nova_core, millis(), _task->getGPSState());
    } else if (_page == HomePage::NV_BATT) {
      novaui::pageBatt(display, nova_core, millis());
    } else if (_page == HomePage::NV_RANGE) {
      novaui::RangeData rd{};
      rd.sf = _node_prefs->sf;
#if UI_DISCOVER_SCREEN
      rd.available = true;
      rd.idle = (discovery_req_time == 0);
      rd.running = (discovery_req_time != 0 && millis() < discovery_req_time + 6000);
      rd.count = (int)disc_nodes_count;
      for (uint32_t i = 0; i < disc_nodes_count && i < DISCOVERED_NODES_TABLE_SIZE; i++) {
        if (i == 0 || discovered[i].snr_in > rd.best_in) { rd.best_in = discovered[i].snr_in; rd.best_out = discovered[i].snr_out; }
        if (i == 0 || discovered[i].snr_in < rd.worst_in) rd.worst_in = discovered[i].snr_in;
      }
#endif
      novaui::pageRange(display, nova_core, rd);
    } else if (_page == HomePage::NV_SET) {
#ifdef ESP32
      uint32_t nv_heap = ESP.getFreeHeap();
#else
      uint32_t nv_heap = 0;
#endif
      novaui::pageSettings(display, nova_core, millis(), nv_heap);
    } else if (_page == HomePage::NV_QUIET) {
      novaui::pageQuiet(display, nova_core, nova::Nova::localHour(rtc_clock.getCurrentTime(), _node_prefs->tz_offset));
'''

KEY_PAGES = r'''    if (c == KEY_ENTER && _page == HomePage::NV_TRIP) {
      if (novaui::tripPress(millis())) { nova_core.trip.reset(); _task->showAlert("Trip cleared", 800); }
      return true;
    }
#if UI_DISCOVER_SCREEN
    if (c == KEY_ENTER && _page == HomePage::NV_RANGE) {
      if (millis() > discovery_req_time + 6000) {   // rate limiter, and lets the last answers finish
        sendDiscoverRequest();
        if (discovery_req_time == 0) discovery_req_time = millis();
      }
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::NV_SET) {
      novaui::cycleProfile(nova_core);
      _task->showAlert(nova::profileParams(nova_core.cfg.profile).name, 800);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::NV_QUIET) {
      novaui::toggleQuiet(nova_core);
      _task->showAlert(nova_core.cfg.quiet_on ? "Quiet hours ON" : "Quiet hours off", 800);
      return true;
    }
'''

# local hour for quiet hours, computed where it is needed
ALERT_MSG = '''  bool ls_alert = nova_core.mayAlert(from.isFav(), nova::Nova::localHour(rtc_clock.getCurrentTime(), the_mesh.getNodePrefs()->tz_offset));   // quiet hours (favourites always alert)
'''
ALERT_CHAN = '''  bool ls_alert = nova_core.mayAlert(false, nova::Nova::localHour(rtc_clock.getCurrentTime(), the_mesh.getNodePrefs()->tz_offset));
'''


CREDIT_MARK = "LODESTAR-CREDIT"
CREDIT_CPP = """// ===========================================================================  LODESTAR-CREDIT
// Engineered by Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS),
// with MeshCore community guidance and assistance.
// ===========================================================================
__attribute__((used)) static const char LODESTAR_CREDIT[] = "Engineered by Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS), with MeshCore community guidance and assistance.";
"""


def add_credit(path):
    """Put the credit at the top of the patched source file (a comment, and a string that stays in the firmware image). Safe to run twice."""
    with open(path) as f:
        t = f.read()
    if CREDIT_MARK in t:
        return
    with open(path, "w") as f:
        f.write(CREDIT_CPP + t)



def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--meshcore", required=True, help="path to a MeshCore checkout")
    args = ap.parse_args()

    root = args.meshcore
    comp = os.path.join(root, "examples", "companion_radio")
    paths = {
        "h": os.path.join(comp, "MyMesh.h"),
        "c": os.path.join(comp, "MyMesh.cpp"),
        "u": os.path.join(comp, "ui-new", "UITask.cpp"),
    }
    for p in list(paths.values()) + [NOVA_H, LODESTAR_H, PAGES_H]:
        if not os.path.isfile(p):
            die(f"Cannot find {p}. Is --meshcore pointing at a MeshCore checkout?")
    txt = {}
    for k, p in paths.items():
        with open(p) as f:
            txt[k] = f.read()
        if "nova_core" in txt[k]:
            die("This MeshCore checkout already has NOVA in it. Use a fresh clone.")

    # ---- library files -----------------------------------------------------
    helpers = os.path.join(root, "src", "helpers")
    shutil.copyfile(NOVA_H, os.path.join(helpers, "Nova.h"))
    shutil.copyfile(LODESTAR_H, os.path.join(helpers, "Lodestar.h"))
    shutil.copyfile(PAGES_H, os.path.join(comp, "ui-new", "NovaPages.h"))

    # ---- MyMesh.h ------------------------------------------------------------
    h = txt["h"]
    h = patch(h, r"#include <helpers/BaseChatMesh\.h>\n", "#include <helpers/BaseChatMesh.h>\n#include <helpers/Nova.h>\n", "BaseChatMesh include")
    h = patch(h, r"(  void logRxRaw\(float snr, float rssi, const uint8_t raw\[\], int len\) override;\n)",
              lambda m: m.group(1) + "  void logRx(mesh::Packet* packet, int len, float score) override;   // NOVA\n"
                                     "  void logTx(mesh::Packet* packet, int len) override;                 // NOVA\n"
                                     "  void novaTick(uint32_t now);                                        // NOVA\n",
              "logRxRaw declaration")
    h = patch(h, r"extern MyMesh the_mesh;", "extern MyMesh the_mesh;\nextern nova::Nova nova_core;   // LODESTAR NOVA", "the_mesh extern")

    # ---- MyMesh.cpp ----------------------------------------------------------
    c = txt["c"]
    c = patch(c, r"(void MyMesh::logRxRaw\(float snr, float rssi, const uint8_t raw\[\], int len\) \{)",
              lambda m: MYMESH_DEFS + m.group(1), "logRxRaw definition")
    c = patch(c, r"(          next_ack_idx = \(next_ack_idx \+ 1\) % EXPECTED_ACK_TABLE_SIZE;\n)",
              lambda m: m.group(1) + SEND_HOOK, "ack table insert in CMD_SEND_TXT_MSG")
    c = patch(c, r"(      if \(success && sendGroupMessage\(msg_timestamp, channel\.channel, _prefs\.node_name, text, len - i\)\) \{\n)",
              lambda m: m.group(1) + CHAN_HOOK, "channel send in CMD_SEND_CHANNEL_TXT_MSG")
    c = patch(c, r"(ContactInfo\*  MyMesh::processAck\(const uint8_t \*data\) \{\n)",
              lambda m: m.group(1) + ACK_HOOK, "processAck")
    c = patch(c, r"(void MyMesh::loop\(\) \{\n  BaseChatMesh::loop\(\);\n)",
              lambda m: m.group(1) + "  novaTick(millis());   // LODESTAR NOVA\n", "MyMesh::loop")
    c = patch(c, r"(  // load persisted prefs\n  _store->loadPrefs\(_prefs\);\n)",
              lambda m: m.group(1) + LOAD_SETTINGS, "loadPrefs in begin")

    # ---- UITask.cpp ----------------------------------------------------------
    u = txt["u"]
    u = patch(u, r'(#include "icons\.h"\n)',
              lambda m: m.group(1) + '#include "NovaPages.h"\n'
                                     '#define NOVA_SCREEN_MS ((AUTO_OFF_MILLIS) > 0 ? nova_core.screenMs() : 0)   // screen timeout follows the NOVA profile\n',
              "icons include")
    u = patch(u, r"    FIRST,\n    RECENT,\n", ENUM_PAGES, "HomePage enum start")
    u = patch(u, r"#if UI_DISCOVER_SCREEN\n    DISCOVERY,\n#endif\n", SETTINGS_ENUM, "HomePage enum discovery")
    # page dots: with more pages the stock 10 px spacing would run off the edge of the screen
    u = patch(u, r"    int x = display\.width\(\) / 2 - 5 \* \(HomePage::Count-1\);\n    for \(uint8_t i = 0; i < HomePage::Count; i\+\+, x \+= 10\) \{",
              "    int nv_step = (HomePage::Count * 10 > 120) ? 120 / HomePage::Count : 10;   // NOVA: squeeze the dots to fit\n"
              "    int x = display.width() / 2 - (nv_step * (HomePage::Count - 1)) / 2;\n"
              "    for (uint8_t i = 0; i < HomePage::Count; i++, x += nv_step) {",
              "page indicator dots")
    u = patch(u, r"(#ifndef UI_NO_HIBERNATE\n    \} else if \(_page == HomePage::SHUTDOWN\) \{)",
              lambda m: RENDER_PAGES + m.group(1), "render: SHUTDOWN branch")
    u = patch(u, r"    return 5000;   // next render after 5000 ms",
              "    return (_page >= HomePage::NV_LINK && _page <= HomePage::NV_RANGE) || _page == HomePage::NV_SET || _page == HomePage::NV_QUIET ? 2000 : 5000;   // NOVA pages refresh faster",
              "render interval")
    u = patch(u, r"(#ifndef UI_NO_HIBERNATE\n    if \(c == KEY_ENTER && _page == HomePage::SHUTDOWN\) \{)",
              lambda m: KEY_PAGES + m.group(1), "handleInput: SHUTDOWN key")
    # boot splash: LODESTAR / NOVA / by Wolf Reactive Systems (WRS) instead of the MeshCore logo
    u = patch(u, r"(  int render\(DisplayDriver& display\) override \{\n)    // meshcore logo.*?(    return 1000;\n)",
              lambda m: m.group(1) + "    novaui::splash(display, _version_info);   // LODESTAR NOVA\n" + m.group(2), "splash screen", flags=re.S)
    # screen timeout from the profile
    u = patch(u, r"millis\(\) \+ AUTO_OFF_MILLIS", "millis() + NOVA_SCREEN_MS", "auto-off timer", count=6)
    # quiet hours: no screen wake, no sound or buzz (favourites still alert)
    u = patch(u, r"(  if \(!\(txt_type == TXT_TYPE_PLAIN \|\| txt_type == TXT_TYPE_SIGNED_PLAIN\)\) return;\n)",
              lambda m: m.group(1) + ALERT_MSG, "onMessageRecv start")
    u = patch(u, r"(void UITask::onChannelMessageRecv\(mesh::Packet \*pkt, ChannelDetails& channel_details, const char\* text\) \{\n)",
              lambda m: m.group(1) + ALERT_CHAN, "onChannelMessageRecv start")
    u = patch(u, r"    if \(!_display->isOn\(\) && !hasConnection\(\)\) \{\n      _display->turnOn\(\);",
              "    if (!_display->isOn() && !hasConnection() && ls_alert) {\n      _display->turnOn();", "screen wake", count=2)
    u = patch(u, r"  if \(!hasConnection\(\)\) \{\n    notify\(UIEventType::(contactMessage|channelMessage)\);",
              lambda m: "  if (!hasConnection() && ls_alert) {\n    notify(UIEventType::" + m.group(1) + ");", "message notify", count=2)

    for k, p in paths.items():
        body = {"h": h, "c": c, "u": u}[k]
        with open(p, "w") as f:
            f.write(body)

    add_credit(paths["c"])
    print("LODESTAR NOVA applied:")
    print("  + src/helpers/Nova.h, src/helpers/Lodestar.h, examples/companion_radio/ui-new/NovaPages.h")
    print("  ~ examples/companion_radio/MyMesh.h        (hooks)")
    print("  ~ examples/companion_radio/MyMesh.cpp      (packet hooks, send/ack tracking, settings, dashboard link)")
    print("  ~ examples/companion_radio/ui-new/UITask.cpp (7 new screens, splash, quiet hours, profile screen timeout)")


if __name__ == "__main__":
    main()
