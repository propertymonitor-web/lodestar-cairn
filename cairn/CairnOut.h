// LODESTAR CAIRN - the text link that rides on the Bluetooth companion connection.
//
// The tablet app (CAIRN Tactical Station) talks to the radio over Bluetooth. MeshCore's Bluetooth link carries
// framed binary commands, so CAIRN sends its own text lines inside one extra frame type (0xC8) in both directions:
//   tablet -> radio: [0xC8][one line of text, e.g. "nova hello" or "cairn chans"]
//   radio -> tablet: [0xC8][up to 160 bytes of the radio's text output]  (the tablet joins the pieces and splits lines)
// Everything the radio prints for the USB dashboard goes through cairnOut, so it also reaches the tablet. The text
// link switches itself on when a tablet frame arrives and off when Bluetooth disconnects or goes quiet for 30 s.
#pragma once
#include <Arduino.h>
#include <helpers/Cairn.h>

#define CAIRN_TUN_OUT    8192u
#define CAIRN_TUN_IN     512u
#define CAIRN_TUN_CHUNK  160u
#define CAIRN_TUN_QUIET  30000u

class CairnOut : public Print {
 public:
  using Print::write;
  size_t write(uint8_t c) override { Serial.write(c); if (on_) out_.push(c); return 1; }
  size_t write(const uint8_t* b, size_t n) override {
    Serial.write(b, n);
    if (on_) { if (out_.room() >= n) for (size_t i = 0; i < n; i++) out_.push(b[i]); else lost_++; }
    return n;
  }
  // input: lines typed on the USB port, or sent by the tablet over Bluetooth
  int available() { return Serial.available() + (int)in_.count(); }
  int read() { return in_.count() ? in_.pop() : Serial.read(); }
  // output flow control: how many bytes can still be queued for the tablet (a large number when nobody listens)
  size_t room() const { return on_ ? out_.room() : 4096u; }
  bool tunnelOn() const { return on_; }
  void tunnelSeen(uint32_t now) { if (!on_) { on_ = true; out_.clear(); } seen_ = now; }
  void tunnelOff() { on_ = false; out_.clear(); }
  uint32_t quietFor(uint32_t now) const { return now - seen_; }
  bool feed(const uint8_t* p, size_t n) {          // one line from the tablet; a whole line or nothing
    if (n == 0 || n > 200u) return false;
    bool nl = p[n - 1] == '\n';
    if (in_.room() < n + (nl ? 0u : 1u)) return false;
    for (size_t i = 0; i < n; i++) in_.push(p[i]);
    if (!nl) in_.push('\n');
    return true;
  }
  size_t pending() const { return out_.count(); }
  size_t peek(uint8_t* d, size_t max) const { return out_.peek(d, max); }
  void drop(size_t k) { out_.drop(k); }
  uint32_t lost() const { return lost_ + out_.lost(); }
 private:
  bool on_ = false;
  uint32_t seen_ = 0, lost_ = 0;
  cairn::ByteRing<CAIRN_TUN_OUT> out_;
  cairn::ByteRing<CAIRN_TUN_IN> in_;
};
extern CairnOut cairnOut;
