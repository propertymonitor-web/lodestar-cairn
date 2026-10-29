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
CAIRN_PAGES = os.path.join(ROOT, "cairn", "ui", "CairnPages.h")
CAIRN_LOGO = os.path.join(ROOT, "cairn", "ui", "CairnLogo.h")
CAIRN_OUT_H = os.path.join(ROOT, "cairn", "CairnOut.h")


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
#define CAIRN_CFG_FILE  "/cairn.cfg"
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
  {   // settings kept over a restart: mode, antenna, automatic check-in, help beacon ...
    File cf = fs->open(CAIRN_CFG_FILE, "r");
    if (cf) {
      char cb[96];
      int cn = cf.read((uint8_t*)cb, sizeof(cb) - 1);
      cf.close();
      if (cn > 0) { cb[cn] = 0; cairn::loadConfig(cb, cairn_core.cfg); }
    }
  }
#endif
  if (cairn_core.cfg.mode == 1) cairn_core.want_mode = 1;   // Disaster mode comes back after a restart
  cairn_core.want_power = true;
  cairn_core.autoReset(millis() / 1000u);
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

void MyMesh::cairnSaveCfg() {
#ifdef ESP32
  char cb[96];
  size_t cn = cairn::saveConfig(cairn_core.cfg, cb, sizeof(cb));
  if (cn == 0) return;
  FILESYSTEM* fs = _store->getPrimaryFS();
  File cf = fs->open(CAIRN_CFG_FILE, "w", true);
  if (!cf) return;
  cf.write((const uint8_t*)cb, cn);
  cf.close();
#endif
}

// Send a text to channel `ch` as a group message and write it in the journal. False when there is no such channel.
bool MyMesh::cairnSendText(int ch, const char* text, size_t n) {
  ChannelDetails channel;
  if (n == 0 || !getChannel(ch, channel) || channel.name[0] == 0) return false;
  if (!sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), channel.channel, _prefs.node_name, text, (int)n)) return false;
  nova_core.deliveries.onSendChannel(channel.name, nova_core.linkUsable(), _ms->getMillis());
  uint32_t now = millis();
  size_t m = cairn::recOwn(cairn_line, sizeof(cairn_line), getRTCClock()->getCurrentTime(), now / 1000u, cairn_core.id, ch, text);
  if (m) cairn_core.j.push(cairn_line, true);
  return true;
}

// The position-carrying messages: kind 0 check-in, 1 help, 2 status (arg = code), 3 mark (arg = kind), 4 low battery (arg = percent).
bool MyMesh::cairnSendKind(int ch, int kind, int arg) {
  char text[120];
  uint32_t now = millis();
  const lodestar::Position& pos = nova_core.link.position();
  bool fix = nova_core.link.hasFix(now);
  size_t n = 0;
  switch (kind) {
    case 0: n = cairn::okMessage(cairn_core.id, fix, pos.lat_e6, pos.lon_e6, text, sizeof(text)); break;
    case 1: n = cairn::helpMessage(cairn_core.id, fix, pos.lat_e6, pos.lon_e6, text, sizeof(text)); break;
    case 2: n = cairn::statusMessage(cairn_core.id, arg, fix, pos.lat_e6, pos.lon_e6, text, sizeof(text)); break;
    case 3: n = cairn::markMessage(cairn_core.id, arg, fix, pos.lat_e6, pos.lon_e6, text, sizeof(text)); break;
    case 4: n = cairn::lowBatMessage(cairn_core.id, arg, fix, pos.lat_e6, pos.lon_e6, text, sizeof(text)); break;
  }
  if (n == 0 || !cairnSendText(ch, text, n)) return false;
  if (kind == 0) cairn_core.last_ok_up = now / 1000u + 1u;
  return true;
}

// "LC-xxxxx Reporting at: <lat>,<lon>" to a channel.
bool MyMesh::cairnSendOk(int ch) { return cairnSendKind(ch, 0, 0); }

// Add, make, remove or show the key of a channel, then tell the dashboard what happened.
void MyMesh::cairnChannelOp() {
  int op = cairn_core.chan_op;
  cairn_core.chan_op = 0;
  static const char* OPS[] = {"", "add", "new", "remove", "key"};
  ChannelDetails cd;
  int idx = -1;
  const char* why = "";
  bool ok = false;
  uint8_t key[16];
  if (op == 1 || op == 2) {
    if (op == 2) getRNG()->random(key, 16); else memcpy(key, cairn_core.chan_key, 16);
    for (int i = 0; i < CAIRN_MAX_CHANNELS && idx < 0; i++) {
      static const uint8_t z16[16] = {0};
      if (getChannel(i, cd) && cd.name[0] != 0 && memcmp(cd.channel.secret, key, 16) == 0 && memcmp(cd.channel.secret + 16, z16, 16) == 0) idx = i;
    }
    if (idx >= 0) { why = "already"; ok = true; }
    else {
      for (int i = 1; i < CAIRN_MAX_CHANNELS && idx < 0; i++) if (getChannel(i, cd) && cd.name[0] == 0) idx = i;
      if (idx < 0) why = "full";
      else {
        ChannelDetails nd;
        memset(&nd, 0, sizeof(nd));
        strncpy(nd.name, cairn_core.chan_name, sizeof(nd.name) - 1);
        memcpy(nd.channel.secret, key, 16);
        if (setChannel(idx, nd)) { saveChannels(); ok = true; } else { why = "failed"; idx = -1; }
      }
    }
  } else if (op == 3) {
    idx = cairn_core.chan_idx;
    if (idx > 0 && getChannel(idx, cd) && cd.name[0] != 0) {
      ChannelDetails nd;
      memset(&nd, 0, sizeof(nd));
      if (setChannel(idx, nd)) {
        saveChannels(); ok = true;
        if (cairn_core.cfg.main_chan == idx) { cairn_core.cfg.main_chan = 0; cairn_core.cfg_dirty = true; }
      } else why = "failed";
    } else why = "none";
  } else if (op == 4) {
    idx = cairn_core.chan_idx;
    ok = getChannel(idx, cd) && cd.name[0] != 0;
    if (!ok) why = "none";
  }
#ifndef ENABLE_USB_INTERFACE
  auto pname = [](const char* nm) { for (const char* q = nm; *q; q++) { if (*q == '"' || *q == '\\') Serial.print('\\'); if ((uint8_t)*q >= 32) Serial.print(*q); } };
  if (op == 4 && ok) {
    char hx[33];
    cairn::hex16(cd.channel.secret, hx);
    Serial.print("{\"cairn\":1,\"chankey\":{\"i\":"); Serial.print(idx); Serial.print(",\"n\":\""); pname(cd.name); Serial.print("\",\"k\":\""); Serial.print(hx); Serial.println("\"}}");
  } else {
    if (ok && op != 3) getChannel(idx, cd);
    Serial.print("{\"cairn\":1,\"chanres\":{\"op\":\""); Serial.print(OPS[op]); Serial.print("\",\"ok\":"); Serial.print(ok ? 1 : 0);
    Serial.print(",\"i\":"); Serial.print(idx); Serial.print(",\"why\":\""); Serial.print(why); Serial.print("\",\"n\":\"");
    if (op != 3 && ok) pname(cd.name);
    Serial.println("\"}}");
  }
#else
  (void)OPS;
#endif
  cairn_core.want_chans = true;
}

// `cairn ...` typed on the USB serial port.
void MyMesh::cairnCommand(const char* args) {
  char r[420];
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

  uint32_t up = now / 1000u;
  cairn_core.ctx.freq_mhz = _prefs.freq;
  cairn_core.ctx.hw_max = MAX_LORA_TX_POWER;
  cairn_core.ctx.setting = _prefs.tx_power_dbm;
  cairn_core.ctx.batt_pct = nova_core.batt.pct();
  cairn_core.ctx.external = board.isExternalPowered();
  cairn_core.updateEco();
  static bool eco_before = false;
  if (eco_before != cairn_core.eco_low) { eco_before = cairn_core.eco_low; cairn_core.want_power = true; cairn_core.autoReset(up); }
  if (cairn_core.want_power) {
    cairn_core.want_power = false;
    int w = cairn_core.wantedSetting();                 // -100: not the US band, leave the power alone
    if (w != -100 && w != (int)_prefs.tx_power_dbm) {
      _prefs.tx_power_dbm = (int8_t)w;
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      savePrefs();
      cairn_core.ctx.setting = w;
    }
  }
  if (cairn_core.cfg_dirty) { cairn_core.cfg_dirty = false; cairnSaveCfg(); }
  if (cairn_core.chan_op) cairnChannelOp();

  // One message per pass at most, so the radio is never asked to send two at once.
  if (cairn_core.helpDue(up)) {
    cairn_core.want_help_now = false;
    cairnSendKind(cairn_core.helpChannel(), 1, 0);
    cairn_core.helpSent(up);                             // even if it could not send: try again at the next beat
  } else if (cairn_core.want_help_stop) {
    cairn_core.want_help_stop = false;
    cairnSendKind(cairn_core.helpChannel(), 0, 0);       // "Reporting at": everyone sees the call was cancelled
    cairn_core.help_chan = -1;
  } else if (cairn_core.want_status) {
    int code = cairn_core.want_status, ch = cairn_core.msgChannel();
    cairn_core.want_status = 0; cairn_core.msg_channel = -1;
    cairnSendKind(ch, 2, code);
  } else if (cairn_core.want_mark) {
    int code = cairn_core.want_mark, ch = cairn_core.msgChannel();
    cairn_core.want_mark = 0; cairn_core.msg_channel = -1;
    cairnSendKind(ch, 3, code);
  } else if (cairn_core.lowBatDue()) {
    cairnSendKind(cairn_core.cfg.main_chan, 4, cairn_core.ctx.batt_pct);
  } else if (cairn_core.autoDue(up)) {
    cairnSendKind(cairn_core.cfg.main_chan, 0, 0);
    cairn_core.autoReset(up);
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

  // Messages typed on the dashboard: `cairn say <ch> <text>` and `cairn dm <key> <text>`.
  if (cairn_core.want_say || cairn_core.want_dm) {
    bool dm = cairn_core.want_dm;
    cairn_core.want_say = cairn_core.want_dm = false;
    bool ok = false;
    char who[34] = "";
    if (!dm) {
      ChannelDetails channel;
      if (getChannel(cairn_core.say_channel, channel) && channel.name[0] != 0) {
        ok = sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), channel.channel, _prefs.node_name, cairn_core.say_text, (int)strlen(cairn_core.say_text));
        if (ok) nova_core.deliveries.onSendChannel(channel.name, nova_core.linkUsable(), _ms->getMillis());
        snprintf(who, sizeof(who), "%s", channel.name);
      }
    } else {
      ContactInfo* ct = lookupContactByPubKey(cairn_core.dm_key, 6);
      if (ct) {
        uint32_t ack = 0, tmo = 0;
        ok = sendMessage(*ct, getRTCClock()->getCurrentTimeUnique(), 0, cairn_core.say_text, ack, tmo) != MSG_SEND_FAILED;
        snprintf(who, sizeof(who), "%s", ct->name);
      }
    }
    if (ok) {
      size_t m = cairn::recMsg(cairn_line, sizeof(cairn_line), getRTCClock()->getCurrentTime(), now / 1000u, dm ? 'p' : 's', _prefs.node_name, who, 0.0f, -1, cairn_core.say_text);
      if (m) cairn_core.j.push(cairn_line, true);
    }
#ifndef ENABLE_USB_INTERFACE
    if (ok) Serial.println(dm ? "# direct message sent" : "# message sent");
    else if (dm) Serial.println("# direct message NOT sent (that contact is not in this radio's contact list, or it could not send)");
    else Serial.println("# message NOT sent (no such channel, or the radio could not send)");
#endif
  }
  if (cairn_core.want_chans) {
    cairn_core.want_chans = false;
#ifndef ENABLE_USB_INTERFACE
    Serial.print("{\"cairn\":1,\"chans\":[");
    bool first = true;
    for (int i = 0; i < CAIRN_MAX_CHANNELS; i++) {
      ChannelDetails ch;
      if (!getChannel(i, ch) || ch.name[0] == 0) continue;
      if (!first) Serial.print(',');
      first = false;
      Serial.print("{\"i\":"); Serial.print(i); Serial.print(",\"n\":\"");
      for (const char* q = ch.name; *q; q++) { if (*q == '"' || *q == '\\') Serial.print('\\'); if ((uint8_t)*q >= 32) Serial.print(*q); }
      Serial.print("\",\"t\":\""); Serial.print(cairn::chanKind(ch.name, ch.channel.secret)); Serial.print("\"}");
    }
    Serial.print("],\"mc\":"); Serial.print(cairn_core.cfg.main_chan); Serial.print(",\"max\":"); Serial.print(CAIRN_MAX_CHANNELS);
    Serial.println("}");
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
      char sj[720];
      if (cairn_core.statusJson(sj, sizeof(sj), now / 1000u, getRTCClock()->getCurrentTime(), cairn_core.to_flash.lost + cairn_core.to_usb.lost)) Serial.println(sj);
    }
  } else {
    while (cairn_core.j.peek(cairn_core.to_usb) != nullptr) cairn_core.j.advance(cairn_core.to_usb);   // nobody is listening
  }
#else
  cairn_core.want_dump = false;
#endif
}

'''



# ---- Bluetooth text link (see cairn/CairnOut.h) ----------------------------------------------
TUNNEL_DEFS = r'''
// ---- LODESTAR CAIRN: text link over Bluetooth (frame type 0xC8, both ways) -----------------------
CairnOut cairnOut;
#define CMD_CAIRN_TEXT        0xC8
#define PUSH_CODE_CAIRN_TEXT  0xC8

void MyMesh::cairnTunnelIn(const uint8_t* p, size_t n) {
  cairnOut.tunnelSeen(millis());
  cairnOut.feed(p, n);
}

// Called every pass: send the next piece of the radio's text output to the tablet, if Bluetooth can take it.
void MyMesh::cairnTunnelPump(uint32_t now) {
  if (!cairnOut.tunnelOn()) return;
  if (!_serial->isConnected() || cairnOut.quietFor(now) > CAIRN_TUN_QUIET) { cairnOut.tunnelOff(); return; }
  if (_serial->isWriteBusy() || cairnOut.pending() == 0) return;
  uint8_t f[1 + CAIRN_TUN_CHUNK];
  f[0] = PUSH_CODE_CAIRN_TEXT;
  size_t k = cairnOut.peek(&f[1], CAIRN_TUN_CHUNK);
  if (_serial->writeFrame(f, 1 + k)) cairnOut.drop(k);
}

'''
CAIRN_DEFS = CAIRN_DEFS.replace("Serial.", "cairnOut.")
# slow down for the Bluetooth link: never queue more than it can carry
for _old, _new in (
    ("    if (dump_f) {\n      uint8_t buf[512];", "    if (dump_f && cairnOut.room() >= 600) {\n      uint8_t buf[512];"),
    ("      if (!l) break;\n      cairnOut.write((const uint8_t*)l, strlen(l));", "      if (!l || cairnOut.room() < strlen(l) + 2) break;\n      cairnOut.write((const uint8_t*)l, strlen(l));"),
):
    if CAIRN_DEFS.count(_old) != 1:
        raise SystemExit("apply_cairn.py: flow-control anchor missing: " + _old[:40])
    CAIRN_DEFS = CAIRN_DEFS.replace(_old, _new)
CAIRN_DEFS += TUNNEL_DEFS

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
    shutil.copyfile(CAIRN_OUT_H, os.path.join(root, "src", "helpers", "CairnOut.h"))

    # ---- MyMesh.h -------------------------------------------------------------------------
    h = once(h, "#include <helpers/Nova.h>\n", "#include <helpers/Nova.h>\n#include <helpers/Cairn.h>\n#include <helpers/CairnOut.h>\n", "Nova include in MyMesh.h")
    h = once(h, "  void novaTick(uint32_t now);                                        // NOVA\n",
             "  void novaTick(uint32_t now);                                        // NOVA\n"
             "  void cairnInit();                                                   // CAIRN\n"
             "  void cairnTick(uint32_t now);\n"
             "  void cairnFlush();\n"
             "  void cairnCommand(const char* args);\n"
             "  void cairnLogDirect(char kind, const ContactInfo& from, mesh::Packet* pkt, const char* text);\n"
             "  void cairnLogChannel(int idx, mesh::Packet* pkt, const char* text);\n"
             "  void cairnLogAdvert(mesh::Packet* pkt, int rssi);\n"
             "  bool cairnSendOk(int ch);\n"
             "  bool cairnSendText(int ch, const char* text, size_t n);\n"
             "  bool cairnSendKind(int ch, int kind, int arg);\n"
             "  void cairnSaveCfg();\n"
             "  void cairnChannelOp();\n"
             "  void cairnTunnelIn(const uint8_t* p, size_t n);\n"
             "  void cairnTunnelPump(uint32_t now);\n",
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
             "  nova_core.loop(nv_now, nv_mv, board.isExternalPowered());\n  cairnTick(nv_now);   // LODESTAR CAIRN\n  cairnTunnelPump(nv_now);   // CAIRN: text link to the tablet\n", "nova_core.loop call")
    # `cairn ...` lines on the USB serial port
    c = once(c, "    if (strncmp(nv_line, \"nova\", 4) != 0 || (nv_line[4] != 0 && nv_line[4] != ' ')) continue;\n",
             "    if (strncmp(nv_line, \"cairn\", 5) == 0 && (nv_line[5] == 0 || nv_line[5] == ' ')) { cairnCommand(nv_line + 5); continue; }   // CAIRN\n"
             "    if (strncmp(nv_line, \"nova\", 4) != 0 || (nv_line[4] != 0 && nv_line[4] != ' ')) continue;\n", "nova serial command filter")
    # station ID at start-up, after NOVA restores its settings
    c = once(c, "      if (nv_n > 0) { nv_buf[nv_n] = 0; nova_core.loadSettings(nv_buf); }\n    }\n  }\n",
             "      if (nv_n > 0) { nv_buf[nv_n] = 0; nova_core.loadSettings(nv_buf); }\n    }\n  }\n  cairnInit();   // LODESTAR CAIRN: station ID and journal\n",
             "NOVA settings restore")
    # the NOVA serial line buffer is 96 characters: too short for `cairn say <ch> <text>`
    c = once(c, "  static char nv_line[96];", "  static char nv_line[200];", "serial line buffer")
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

    # the USB-dashboard text (NOVA reader and printers) also goes to the tablet over Bluetooth
    a = c.index("  static char nv_line[200];")
    b = c.index("\n#endif\n}\n", a)
    seg = c[a:b]
    for old, new in (("Serial.available()", "cairnOut.available()"), ("Serial.read()", "cairnOut.read()"), ("Serial.write(", "cairnOut.write("),
                     ("Serial.print(", "cairnOut.print("), ("Serial.println(", "cairnOut.println(")):
        if old not in seg:
            die(f"NOVA serial block no longer uses {old}; update apply_cairn.py.")
        seg = seg.replace(old, new)
    if "Serial." in seg:
        die("NOVA serial block has a Serial use apply_cairn.py does not know; update it.")
    seg = seg.replace("    if (nv_n > 0) {\n      cairnOut.write((const uint8_t*)nv_json", "    if (nv_n > 0 && cairnOut.room() > nv_n + 2) {\n      cairnOut.write((const uint8_t*)nv_json")
    seg = seg.replace("      size_t nv_m = nova_core.found.nextLine(", "      if (cairnOut.room() < 400) break;\n      size_t nv_m = nova_core.found.nextLine(")
    for need in ("cairnOut.room() > nv_n + 2", "cairnOut.room() < 400"):
        if need not in seg:
            die("could not add Bluetooth flow control to the NOVA output: " + need)
    c = c[:a] + seg + c[b:]
    c = once(c, "  } else if (cmd_frame[0] == CMD_RUN_CLI_COMMAND && len >= 3) { // V14+\n",
             "  } else if (cmd_frame[0] == CMD_CAIRN_TEXT && len >= 2) {   // CAIRN: a line of text from the tablet\n    cairnTunnelIn(&cmd_frame[1], len - 1);\n"
             "  } else if (cmd_frame[0] == CMD_RUN_CLI_COMMAND && len >= 3) { // V14+\n", "CMD_RUN_CLI_COMMAND branch")

    open(h_path, "w").write(h)
    open(c_path, "w").write(c)

    # ---- UITask.cpp: CAIRN splash and station screen ----------------------------------------
    u_path = os.path.join(comp, "ui-new", "UITask.cpp")
    for p in (u_path, CAIRN_PAGES, CAIRN_LOGO):
        if not os.path.isfile(p):
            die(f"Cannot find {p}.")
    shutil.copyfile(CAIRN_PAGES, os.path.join(comp, "ui-new", "CairnPages.h"))
    shutil.copyfile(CAIRN_LOGO, os.path.join(comp, "ui-new", "CairnLogo.h"))
    u = open(u_path).read()
    u = once(u, '#include "NovaPages.h"\n', '#include "NovaPages.h"\n#include "CairnPages.h"\n', "NovaPages include")
    u = once(u, "    FIRST,\n    NV_LINK,", "    FIRST,\n    CR_STN,       // LODESTAR CAIRN station screen\n    CR_HELP,      // LODESTAR CAIRN help call\n    CR_PWR,       // LODESTAR CAIRN mode and power\n    NV_LINK,", "HomePage enum")
    u = once(u, "    } else if (_page == HomePage::NV_LINK) {\n",
             "    } else if (_page == HomePage::CR_STN) {\n      cairnui::pageStation(display, cairn_core, millis() / 1000u);\n"
             "    } else if (_page == HomePage::CR_HELP) {\n      cairnui::pageHelp(display, cairn_core, millis(), millis() / 1000u);\n"
             "    } else if (_page == HomePage::CR_PWR) {\n      cairnui::pagePower(display, cairn_core);\n"
             "    } else if (_page == HomePage::NV_LINK) {\n", "render: NV_LINK branch")
    u = once(u, "    if (c == KEY_ENTER && _page == HomePage::NV_TRIP) {\n",
             "    if (c == KEY_ENTER && _page == HomePage::CR_STN) {   // CAIRN: send a check-in\n"
             "      cairn_core.ok_channel = cairn_core.cfg.main_chan; cairn_core.want_ok = true;\n"
             "      _task->showAlert(\"Check-in sending\", 900);\n      return true;\n    }\n"
             "    if (c == KEY_ENTER && _page == HomePage::CR_HELP) {   // CAIRN: arm, then send, or cancel the help call\n"
             "      uint32_t hm = millis();\n"
             "      if (cairn_core.cfg.help_on) { cairn_core.stopHelp(); _task->showAlert(\"Help call cancelled\", 1200); }\n"
             "      else if (cairn_core.arm_until_ms && (int32_t)(cairn_core.arm_until_ms - hm) > 0) { cairn_core.arm_until_ms = 0; cairn_core.startHelp(hm / 1000u, -1); _task->showAlert(\"HELP call sending\", 1500); }\n"
             "      else { cairn_core.arm_until_ms = hm + 5000u; }\n"
             "      return true;\n    }\n"
             "    if (c == KEY_ENTER && _page == HomePage::CR_PWR) {   // CAIRN: switch Standard / Disaster\n"
             "      char rr[200];\n"
             "      cairn_core.runCommand(cairn_core.disaster() ? \" mode standard\" : \" mode disaster\", rr, sizeof(rr), millis() / 1000u);\n"
             "      _task->showAlert(cairn_core.disaster() ? \"DISASTER mode\" : \"STANDARD mode\", 1200);\n      return true;\n    }\n"
             "    if (c == KEY_ENTER && _page == HomePage::NV_TRIP) {\n", "key: NV_TRIP")
    u = once(u, "(_page >= HomePage::NV_LINK && _page <= HomePage::NV_RANGE)", "_page == HomePage::CR_STN || _page == HomePage::CR_HELP || _page == HomePage::CR_PWR || (_page >= HomePage::NV_LINK && _page <= HomePage::NV_RANGE)", "render interval")
    u = once(u, "#define BOOT_SCREEN_MILLIS   3000   // 3 seconds", "#define BOOT_SCREEN_MILLIS   5000   // 5 seconds: CAIRN shows an emblem and the name, give it time to be read", "boot screen time")
    u = once(u, "novaui::splash(display, _version_info);   // LODESTAR NOVA", "cairnui::splash(display, _version_info);   // LODESTAR CAIRN", "splash call")
    open(u_path, "w").write(u)

    add_credit(c_path)
    print("LODESTAR CAIRN applied:")
    print("  + src/helpers/Cairn.h")
    print("  ~ examples/companion_radio/MyMesh.h, MyMesh.cpp  (station ID, journal, check-in, cairn commands)")
    print("  ~ examples/companion_radio/ui-new/UITask.cpp (CAIRN splash with emblem, station screen)")


if __name__ == "__main__":
    main()
