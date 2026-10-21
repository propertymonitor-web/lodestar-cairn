#!/usr/bin/env python3
"""
apply_cairn.py - add LODESTAR CAIRN to a MeshCore checkout.

  python tools/apply_cairn.py --meshcore MeshCore

First runs apply_nova.py (CAIRN is built on LODESTAR NOVA), then copies cairn/Cairn.h into MeshCore and
patches examples/companion_radio/MyMesh.{h,cpp}: a station ID kept on flash, a journal of every message
and advert heard (flash + live to the dashboard), the check-in message, and the `cairn` commands.
Every edit must match exactly once; if upstream MeshCore moved something the script stops and says which
anchor it could not find, instead of leaving a half-patched tree.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
CAIRN_H = os.path.join(ROOT, "cairn", "Cairn.h")


def die(msg):
    print(f"\nERROR: {msg}\n", file=sys.stderr)
    sys.exit(1)


def once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        die(f"Upstream MeshCore changed: expected 1 match for '{what}', found {n}. "
            f"The anchor for this edit in tools/apply_cairn.py needs updating.")
    return text.replace(old, new)


# --------------------------------------------------------------------------
# C++ inserted into MyMesh.cpp (before MyMesh::logRxRaw, after the NOVA block)
# --------------------------------------------------------------------------
CAIRN_DEFS = r'''// ---- LODESTAR CAIRN --------------------------------------------------------------------------
cairn::Cairn cairn_core;
static char cairn_line[CAIRN_LINE];

#define CAIRN_LOG_FILE  "/cairn.log"
#define CAIRN_LOG_OLD   "/cairn.1"
#define CAIRN_ID_FILE   "/cairn.id"
#ifndef CAIRN_LOG_MAX
#define CAIRN_LOG_MAX   (192u * 1024u)      // the journal keeps about this much, plus the same again in /cairn.1
#endif

static uint32_t cairnSizeOf(FILESYSTEM* fs, const char* path) {
#ifdef ESP32
  File f = fs->open(path, "r");
  if (!f) return 0;
  uint32_t s = (uint32_t)f.size();
  f.close();
  return s;
#else
  (void)fs; (void)path;
  return 0;
#endif
}

// The station ID is the last 5 bytes of this radio's public key, so it is the same every boot and can
// always be rebuilt. Nothing is stored for it and nothing checks it against a list (the system is offline).
void MyMesh::cairnInit() {
  uint32_t ep = getRTCClock()->getCurrentTime();
#ifdef ESP32
  FILESYSTEM* fs = _store->getPrimaryFS();
#endif
  char nid[CAIRN_ID_LEN + 1];
  cairn::makeId(self_id.pub_key + PUB_KEY_SIZE - 5, nid);
  cairn_core.setId(nid);
#ifdef ESP32
  cairn_core.file_bytes = cairnSizeOf(fs, CAIRN_LOG_FILE);
#endif
  size_t n = cairn::recBoot(cairn_line, sizeof(cairn_line), ep, cairn_core.id, CAIRN_VERSION);
  if (n) cairn_core.j.push(cairn_line, true);
}

// Write what is waiting to the flash. Rotates the file when it is full (the older half is kept as /cairn.1).
void MyMesh::cairnFlush() {
#ifdef ESP32
  if (cairn_core.j.pending(cairn_core.to_flash) == 0) return;
  FILESYSTEM* fs = _store->getPrimaryFS();
  if (cairnSizeOf(fs, CAIRN_LOG_FILE) > CAIRN_LOG_MAX) {
    fs->remove(CAIRN_LOG_OLD);
    fs->rename(CAIRN_LOG_FILE, CAIRN_LOG_OLD);
  }
  File f = fs->open(CAIRN_LOG_FILE, "a", true);
  if (!f) return;
  const char* l;
  while ((l = cairn_core.j.peek(cairn_core.to_flash)) != nullptr) {
    f.write((const uint8_t*)l, strlen(l));
    f.write((const uint8_t*)"\n", 1);
    cairn_core.j.advance(cairn_core.to_flash);
  }
  cairn_core.file_bytes = (uint32_t)f.size();
  f.close();
  cairn_core.j.clearUrgent();
#else
  while (cairn_core.j.peek(cairn_core.to_flash) != nullptr) cairn_core.j.advance(cairn_core.to_flash);
#endif
}

// A message from a contact: direct ('d'), a room server's post ('r'), or a reply to a command we sent ('x').
void MyMesh::cairnLogDirect(char kind, const ContactInfo& from, mesh::Packet* pkt, const char* text) {
  int hops = pkt->isRouteFlood() ? (int)pkt->getPathHashCount() : -1;
  size_t n = cairn::recMsg(cairn_line, sizeof(cairn_line), getRTCClock()->getCurrentTime(), millis() / 1000u, kind,
                           from.name, "", pkt->getSNR(), hops, text);
  if (n) cairn_core.j.push(cairn_line, true);
}

// A message on a channel.
void MyMesh::cairnLogChannel(int idx, mesh::Packet* pkt, const char* text) {
  ChannelDetails cd;
  const char* nm = getChannel(idx, cd) ? cd.name : "?";
  int hops = pkt->isRouteFlood() ? (int)pkt->getPathHashCount() : -1;
  size_t n = cairn::recMsg(cairn_line, sizeof(cairn_line), getRTCClock()->getCurrentTime(), millis() / 1000u, 'c',
                           "", nm, pkt->getSNR(), hops, text);
  if (n) cairn_core.j.push(cairn_line, true);
}

// An advert heard (each distinct advert once, again only if it arrives by a shorter route).
void MyMesh::cairnLogAdvert(mesh::Packet* pkt, int rssi) {
  const uint8_t* p = pkt->payload;
  size_t len = pkt->payload_len;
  const size_t off = 32 + 4 + 64;
  if (len <= off + 1) return;
  const uint8_t* ad = p + off;
  size_t adlen = len - off;
  int type = ad[0] & 0x0F;
  if (type < 1 || type > 4) return;
  uint32_t advt;
  memcpy(&advt, p + 32, 4);
  int hops = (int)pkt->getPathHashCount();
  if (!cairn_core.seen.shouldLog(p, advt, hops)) return;
  bool has_adv = false;
  int32_t alat = 0, alon = 0;
  if ((ad[0] & 0x10) && adlen >= 9) {
    memcpy(&alat, ad + 1, 4);
    memcpy(&alon, ad + 5, 4);
    has_adv = alat >= -90000000 && alat <= 90000000 && alon >= -180000000 && alon <= 180000000 && !(alat == 0 && alon == 0);
  }
  char nm[40] = {0};
  nova::NameBook::parseName(ad, adlen, nm, sizeof(nm));
  uint32_t now = millis();
  const lodestar::Position& pos = nova_core.link.position();
  bool fix = nova_core.link.hasFix(now);
  size_t n = cairn::recAdv(cairn_line, sizeof(cairn_line), getRTCClock()->getCurrentTime(), now / 1000u, type, nm, p, hops,
                           pkt->getSNR(), rssi, has_adv, alat, alon, fix, pos.lat_e6, pos.lon_e6);
  if (n) cairn_core.j.push(cairn_line, false);
}

// "LC-xxxxx Reporting at: <lat>,<lon>" to a channel (0 = the first channel, normally Public).
bool MyMesh::cairnSendOk(int ch) {
  ChannelDetails channel;
  if (!getChannel(ch, channel)) return false;
  char text[100];
  uint32_t now = millis();
  const lodestar::Position& pos = nova_core.link.position();
  size_t n = cairn::okMessage(cairn_core.id, nova_core.link.hasFix(now), pos.lat_e6, pos.lon_e6, text, sizeof(text));
  if (n == 0) return false;
  if (!sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), channel.channel, _prefs.node_name, text, (int)n)) return false;
  nova_core.deliveries.onSendChannel(channel.name, nova_core.linkUsable(), _ms->getMillis());
  cairn_core.last_ok_up = now / 1000u + 1u;
  size_t m = cairn::recOwn(cairn_line, sizeof(cairn_line), getRTCClock()->getCurrentTime(), now / 1000u, cairn_core.id, ch, text);
  if (m) cairn_core.j.push(cairn_line, true);
  return true;
}

// `cairn ...` typed on the USB serial port.
void MyMesh::cairnCommand(const char* args) {
  char r[200];
  cairn_core.runCommand(args, r, sizeof(r), millis() / 1000u);
  Serial.print("# ");
  Serial.println(r);
}

// Called every pass of loop(), from novaTick().
void MyMesh::cairnTick(uint32_t now) {
  static uint32_t next_flush = 0;
  uint32_t pend = cairn_core.j.pending(cairn_core.to_flash);
  if (pend > 0 && (cairn_core.j.urgent() || pend >= CAIRN_RING / 2 || (int32_t)(now - next_flush) >= 0)) {
    cairnFlush();
    next_flush = now + 20000u;
  }

  int nv_hour = nova::Nova::localHour(getRTCClock()->getCurrentTime(), _prefs.tz_offset);
  if (cairn_core.want_mode) {
    char r[96];
    if (cairn_core.want_mode == 1) { nova_core.runCommand(" profile hike", r, sizeof(r), now, nv_hour); nova_core.runCommand(" quiet off", r, sizeof(r), now, nv_hour); }
    else                           { nova_core.runCommand(" profile desk", r, sizeof(r), now, nv_hour); }
    cairn_core.want_mode = 0;
  }
  if (cairn_core.want_time) {
    getRTCClock()->setCurrentTime(cairn_core.want_time);   // the dashboard set the radio's clock from the tablet's
    cairn_core.want_time = 0;
  }
  if (cairn_core.want_clear) {
    cairn_core.want_clear = false;
    cairnFlush();
#ifdef ESP32
    FILESYSTEM* fs = _store->getPrimaryFS();
    fs->remove(CAIRN_LOG_FILE);
    fs->remove(CAIRN_LOG_OLD);
#endif
    cairn_core.file_bytes = 0;
  }
  if (cairn_core.want_ok) {
    cairn_core.want_ok = false;
    bool ok = cairnSendOk(cairn_core.ok_channel);
#ifndef ENABLE_USB_INTERFACE
    Serial.println(ok ? "# check-in sent" : "# check-in NOT sent (no such channel, or the radio could not send)");
#else
    (void)ok;
#endif
  }

#ifndef ENABLE_USB_INTERFACE
  // The journal, sent to the dashboard page: first on request (`cairn dump`: the whole file, oldest first),
  // then live, one line per message or advert.
  static int dump_stage = 0;      // 0 idle, 1 sending /cairn.1, 2 sending /cairn.log
  static File dump_f;
  static uint32_t dump_lines = 0;
#ifdef ESP32
  if (cairn_core.want_dump) {
    cairn_core.want_dump = false;
    cairnFlush();
    Serial.println("{\"cairn\":1,\"dump\":\"start\"}");
    dump_lines = 0;
    dump_stage = 1;
    if (dump_f) dump_f.close();
  }
  if (dump_stage > 0) {
    FILESYSTEM* fs = _store->getPrimaryFS();
    if (!dump_f) {
      dump_f = fs->open(dump_stage == 1 ? CAIRN_LOG_OLD : CAIRN_LOG_FILE, "r");
      if (!dump_f) {
        dump_stage = (dump_stage == 1) ? 2 : 0;
        if (dump_stage == 0) {
          Serial.print("{\"cairn\":1,\"dump\":\"end\",\"lines\":");
          Serial.print(dump_lines);
          Serial.println("}");
        }
      }
    }
    if (dump_f) {
      uint8_t buf[512];
      int got = dump_f.read(buf, sizeof(buf));
      if (got > 0) {
        for (int i = 0; i < got; i++) if (buf[i] == '\n') dump_lines++;
        Serial.write(buf, got);
      } else {
        dump_f.close();
        dump_stage = (dump_stage == 1) ? 2 : 0;
        if (dump_stage == 0) {
          Serial.print("{\"cairn\":1,\"dump\":\"end\",\"lines\":");
          Serial.print(dump_lines);
          Serial.println("}");
        }
      }
    }
  }
#else
  cairn_core.want_dump = false;
#endif
  if (nova_core.dash_on) {
    for (int i = 0; i < 4; i++) {
      const char* l = cairn_core.j.peek(cairn_core.to_usb);
      if (!l) break;
      Serial.write((const uint8_t*)l, strlen(l));
      Serial.write('\n');
      cairn_core.j.advance(cairn_core.to_usb);
    }
    static uint32_t next_status = 0;
    if ((int32_t)(now - next_status) >= 0) {
      next_status = now + 5000u;
      uint32_t ago = cairn_core.last_ok_up ? (now / 1000u + 1u - cairn_core.last_ok_up) : 0;
      Serial.printf("{\"cairn\":1,\"id\":\"%s\",\"ver\":\"%s\",\"kb\":%lu,\"lines\":%lu,\"lost\":%lu,\"mode\":\"%s\",\"okago\":%lu,\"now\":%lu}\n",
                    cairn_core.id, CAIRN_VERSION, (unsigned long)(cairn_core.file_bytes / 1024u), (unsigned long)cairn_core.j.total,
                    (unsigned long)(cairn_core.to_flash.lost + cairn_core.to_usb.lost),
                    (nova_core.cfg.profile == nova::PROFILE_HIKE && !nova_core.cfg.quiet_on) ? "disaster" : "normal",
                    (unsigned long)ago, (unsigned long)getRTCClock()->getCurrentTime());
    }
  } else {
    while (cairn_core.j.peek(cairn_core.to_usb) != nullptr) cairn_core.j.advance(cairn_core.to_usb);   // nobody is listening
  }
#else
  cairn_core.want_dump = false;
#endif
}

'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--meshcore", required=True, help="path to a MeshCore checkout")
    args = ap.parse_args()
    root = args.meshcore

    r = subprocess.run([sys.executable, os.path.join(HERE, "apply_nova.py"), "--meshcore", root])
    if r.returncode != 0:
        die("apply_nova.py failed (see above).")

    comp = os.path.join(root, "examples", "companion_radio")
    h_path, c_path = os.path.join(comp, "MyMesh.h"), os.path.join(comp, "MyMesh.cpp")
    for p in (h_path, c_path, CAIRN_H):
        if not os.path.isfile(p):
            die(f"Cannot find {p}.")
    h, c = open(h_path).read(), open(c_path).read()
    if "cairn_core" in h or "cairn_core" in c:
        die("This MeshCore checkout already has CAIRN in it. Use a fresh clone.")
    shutil.copyfile(CAIRN_H, os.path.join(root, "src", "helpers", "Cairn.h"))

    # ---- MyMesh.h -------------------------------------------------------------------------
    h = once(h, "#include <helpers/Nova.h>\n", "#include <helpers/Nova.h>\n#include <helpers/Cairn.h>\n", "Nova include in MyMesh.h")
    h = once(h, "  void novaTick(uint32_t now);                                        // NOVA\n",
             "  void novaTick(uint32_t now);                                        // NOVA\n"
             "  void cairnInit();                                                   // CAIRN\n"
             "  void cairnTick(uint32_t now);\n"
             "  void cairnFlush();\n"
             "  void cairnCommand(const char* args);\n"
             "  void cairnLogDirect(char kind, const ContactInfo& from, mesh::Packet* pkt, const char* text);\n"
             "  void cairnLogChannel(int idx, mesh::Packet* pkt, const char* text);\n"
             "  void cairnLogAdvert(mesh::Packet* pkt, int rssi);\n"
             "  bool cairnSendOk(int ch);\n",
             "novaTick declaration")
    h = once(h, "extern nova::Nova nova_core;   // LODESTAR NOVA", "extern nova::Nova nova_core;   // LODESTAR NOVA\nextern cairn::Cairn cairn_core;   // LODESTAR CAIRN",
             "nova_core extern")

    # ---- MyMesh.cpp -----------------------------------------------------------------------
    c = once(c, "void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {",
             CAIRN_DEFS + "void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {", "logRxRaw definition")
    # adverts: after NOVA files the node
    c = once(c, "                             nova_core.link.hasFix(nv_now), nv_pos.lat_e6, nv_pos.lon_e6, nv_now);\n",
             "                             nova_core.link.hasFix(nv_now), nv_pos.lat_e6, nv_pos.lon_e6, nv_now);\n"
             "    cairnLogAdvert(pkt, (int)_radio->getLastRSSI());   // CAIRN journal\n", "found.onAdvert call")
    # the journal ticks with NOVA
    c = once(c, "  nova_core.loop(nv_now, nv_mv, board.isExternalPowered());\n",
             "  nova_core.loop(nv_now, nv_mv, board.isExternalPowered());\n  cairnTick(nv_now);   // LODESTAR CAIRN\n", "nova_core.loop call")
    # `cairn ...` lines on the USB serial port
    c = once(c, "    if (strncmp(nv_line, \"nova\", 4) != 0 || (nv_line[4] != 0 && nv_line[4] != ' ')) continue;\n",
             "    if (strncmp(nv_line, \"cairn\", 5) == 0 && (nv_line[5] == 0 || nv_line[5] == ' ')) { cairnCommand(nv_line + 5); continue; }   // CAIRN\n"
             "    if (strncmp(nv_line, \"nova\", 4) != 0 || (nv_line[4] != 0 && nv_line[4] != ' ')) continue;\n", "nova serial command filter")
    # station ID at start-up, after NOVA restores its settings
    c = once(c, "      if (nv_n > 0) { nv_buf[nv_n] = 0; nova_core.loadSettings(nv_buf); }\n    }\n  }\n",
             "      if (nv_n > 0) { nv_buf[nv_n] = 0; nova_core.loadSettings(nv_buf); }\n    }\n  }\n  cairnInit();   // LODESTAR CAIRN: station ID and journal\n",
             "NOVA settings restore")
    # messages
    c = once(c, "  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);\n",
             "  cairnLogDirect('d', from, pkt, text);   // CAIRN journal\n  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);\n", "direct message")
    c = once(c, "  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);\n",
             "  cairnLogDirect('x', from, pkt, text);   // CAIRN journal\n  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);\n", "command reply")
    c = once(c, "  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);\n",
             "  cairnLogDirect('r', from, pkt, text);   // CAIRN journal\n  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);\n", "room post")
    c = once(c, "    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;\n  }\n\n  uint8_t channel_idx = findChannelIdx(channel);\n",
             "    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;\n  }\n\n  uint8_t channel_idx = findChannelIdx(channel);\n  cairnLogChannel(channel_idx, pkt, text);   // CAIRN journal\n",
             "channel message")

    open(h_path, "w").write(h)
    open(c_path, "w").write(c)

    print("LODESTAR CAIRN applied:")
    print("  + src/helpers/Cairn.h")
    print("  ~ examples/companion_radio/MyMesh.h, MyMesh.cpp  (station ID, journal, check-in, cairn commands)")


if __name__ == "__main__":
    main()
