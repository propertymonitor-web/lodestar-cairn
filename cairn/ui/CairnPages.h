// CairnPages - the LODESTAR CAIRN boot splash and station screen for the 128x64 OLED.
// By Wolf Reactive Systems (WRS). Plain drawing functions, like NovaPages.h. tools/apply_cairn.py copies this
// (and CairnLogo.h) into MeshCore's companion UI.
#pragma once

#include "NovaPages.h"
#include <helpers/Cairn.h>
#include "CairnLogo.h"

namespace cairnui {

// Boot splash: the emblem on the left, the name on the right.
static inline void splash(DisplayDriver& d, const char* meshcore_version) {
  d.setColor(UIColor::primary_txt);
  d.drawXbm(2, 2, cairn_logo, CAIRN_LOGO_W, CAIRN_LOGO_H);
  const int mid = 46 + (d.width() - 46) / 2;     // centre of the space to the right of the emblem
  d.setTextSize(1); d.setColor(UIColor::corp_blue);
  d.drawTextCentered(mid, 4, "LODESTAR");
  d.setTextSize(2);
  d.drawTextCentered(mid, 14, "CAIRN");
  d.setTextSize(1); d.setColor(UIColor::primary_txt);
  d.drawTextCentered(mid, 36, "WRS");
  char b[24];
  snprintf(b, sizeof(b), "Ver. %s", CAIRN_VERSION);
  d.drawTextCentered(mid, 48, b);
  (void)meshcore_version;   // kept off the splash so it reads in a glance; Settings still shows it
}

// The station screen: ID, journal, last check-in. Pressing the button sends a check-in.
static inline void pageStation(DisplayDriver& d, const cairn::Cairn& C, uint32_t up_s) {
  char b[40];
  novaui::row(d, 0, "CAIRN STATION");
  novaui::row(d, 1, C.hasId() ? C.id : "ID not ready");
  snprintf(b, sizeof(b), "log %luKB %lu lines", (unsigned long)(C.file_bytes / 1024u), (unsigned long)C.j.total);
  novaui::row(d, 2, b);
  if (C.last_ok_up == 0) novaui::row(d, 3, "no check-in sent yet");
  else {
    uint32_t ago = up_s + 1u - C.last_ok_up;
    if (ago < 90u) snprintf(b, sizeof(b), "check-in %lus ago", (unsigned long)ago);
    else if (ago < 5400u) snprintf(b, sizeof(b), "check-in %lum ago", (unsigned long)(ago / 60u));
    else snprintf(b, sizeof(b), "check-in %.1fh ago", (double)ago / 3600.0);
    novaui::row(d, 3, b);
  }
  novaui::row(d, 4, NOVA_HOLD ": send check-in");
}

// The HELP screen. First press arms it (so a pocket press cannot send a call for help); a second press within
// five seconds starts the help beacon. While the beacon runs, a press stops it.
static inline void pageHelp(DisplayDriver& d, const cairn::Cairn& C, uint32_t now_ms, uint32_t up_s) {
  char b[40];
  if (C.cfg.help_on) {
    novaui::row(d, 0, "HELP BEACON ON");
    uint32_t in = C.helpIn(up_s);
    snprintf(b, sizeof(b), "next call in %lus", (unsigned long)in);
    novaui::row(d, 1, b);
    snprintf(b, sizeof(b), "every %d min", C.cfg.beacon_min);
    novaui::row(d, 2, b);
    novaui::row(d, 3, "full power, with GPS");
    novaui::row(d, 4, NOVA_HOLD ": cancel");
  } else if (C.arm_until_ms && (int32_t)(C.arm_until_ms - now_ms) > 0) {
    novaui::row(d, 0, "SEND HELP CALL?");
    novaui::row(d, 1, "Press again to send");
    novaui::row(d, 2, "NEEDS HELP + position");
    novaui::row(d, 3, "to everyone in range");
  } else {
    novaui::row(d, 0, "HELP");
    novaui::row(d, 1, "I NEED HELP call");
    novaui::row(d, 2, "repeats until cancelled");
    novaui::row(d, 4, NOVA_HOLD ": arm");
  }
}

// The POWER screen: mode, transmit power, battery. A press switches Standard / Disaster.
static inline void pagePower(DisplayDriver& d, const cairn::Cairn& C) {
  char b[40];
  novaui::row(d, 0, C.disaster() ? "DISASTER MODE" : "STANDARD MODE");
  int est = cairn::power::estDbm(C.ctx.setting);
  snprintf(b, sizeof(b), "radio %d dBm (%d mW)", est, cairn::power::milliwatts(est));
  novaui::row(d, 1, b);
  if (C.ctx.batt_pct >= 0) snprintf(b, sizeof(b), "battery %d%%%s", C.ctx.batt_pct, C.ctx.external ? " +power" : "");
  else snprintf(b, sizeof(b), "%s", C.ctx.external ? "on outside power" : "battery ?");
  novaui::row(d, 2, b);
  novaui::row(d, 3, C.eco_low ? "saving battery" : "full range");
  novaui::row(d, 4, NOVA_HOLD ": switch mode");
}

}  // namespace cairnui
