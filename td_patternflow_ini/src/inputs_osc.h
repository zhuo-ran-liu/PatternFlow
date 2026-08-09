#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

// ---------------------------------------------------------------------
// Encoder / button reading (unchanged from original)
// ---------------------------------------------------------------------

volatile long encPos[4]      = {0, 0, 0, 0};
volatile uint8_t encState[4] = {0, 0, 0, 0};

static inline void IRAM_ATTR handleEncoder(int idx, int pinA, int pinB) {
  uint8_t s = (digitalRead(pinA) << 1) | digitalRead(pinB);
  uint8_t combined = (encState[idx] << 2) | s;
  switch (combined) {
    case 0b0001: case 0b0111: case 0b1110: case 0b1000:
#if INVERT_ENCODER
      encPos[idx]--;
#else
      encPos[idx]++;
#endif
      break;
    case 0b0010: case 0b1011: case 0b1101: case 0b0100:
#if INVERT_ENCODER
      encPos[idx]++;
#else
      encPos[idx]--;
#endif
      break;
  }
  encState[idx] = s;
}

void IRAM_ATTR isr1() { handleEncoder(0, ENC1_A, ENC1_B); }
void IRAM_ATTR isr2() { handleEncoder(1, ENC2_A, ENC2_B); }
void IRAM_ATTR isr3() { handleEncoder(2, ENC3_A, ENC3_B); }
void IRAM_ATTR isr4() { handleEncoder(3, ENC4_A, ENC4_B); }

inline long getClicks(int idx) { return encPos[idx] / 4; }

struct Button {
  int pin;
  bool lastState = HIGH;
  uint32_t lastChangeMs = 0;
  uint32_t pressStartMs = 0;
  bool longPressFired = false;

  void begin(int p) { pin = p; pinMode(pin, INPUT_PULLUP); }

  bool pressed() {
    bool cur = digitalRead(pin);
    uint32_t now = millis();
    if (cur != lastState && (now - lastChangeMs) > 50) {
      lastState = cur;
      lastChangeMs = now;
      if (cur == LOW) {
        pressStartMs = now;
        longPressFired = false;
        return true;
      }
    }
    return false;
  }

  bool isDown() { return digitalRead(pin) == LOW; }

  bool longPressed(uint32_t threshold = 1000) {
    if (!isDown()) {
      longPressFired = false;
      return false;
    }
    uint32_t now = millis();
    if (!longPressFired && (now - pressStartMs) > threshold) {
      longPressFired = true;
      return true;
    }
    return false;
  }
};

struct InputFrame {
  long knobs[4];
  int knobDeltas[4];
  bool btnPressed[4];
  bool btnHeld[4];
  uint32_t now;
};

Button btn1, btn2, btn3, btn4;

inline void initEncoders() {
  pinMode(ENC1_A, INPUT_PULLUP); pinMode(ENC1_B, INPUT_PULLUP);
  pinMode(ENC2_A, INPUT_PULLUP); pinMode(ENC2_B, INPUT_PULLUP);
  pinMode(ENC3_A, INPUT_PULLUP); pinMode(ENC3_B, INPUT_PULLUP);
  pinMode(ENC4_A, INPUT_PULLUP); pinMode(ENC4_B, INPUT_PULLUP);

  encState[0] = (digitalRead(ENC1_A) << 1) | digitalRead(ENC1_B);
  encState[1] = (digitalRead(ENC2_A) << 1) | digitalRead(ENC2_B);
  encState[2] = (digitalRead(ENC3_A) << 1) | digitalRead(ENC3_B);
  encState[3] = (digitalRead(ENC4_A) << 1) | digitalRead(ENC4_B);

  attachInterrupt(ENC1_A, isr1, CHANGE); attachInterrupt(ENC1_B, isr1, CHANGE);
  attachInterrupt(ENC2_A, isr2, CHANGE); attachInterrupt(ENC2_B, isr2, CHANGE);
  attachInterrupt(ENC3_A, isr3, CHANGE); attachInterrupt(ENC3_B, isr3, CHANGE);
  attachInterrupt(ENC4_A, isr4, CHANGE); attachInterrupt(ENC4_B, isr4, CHANGE);

  btn1.begin(ENC1_SW);
  btn2.begin(ENC2_SW);
  btn3.begin(ENC3_SW);
  btn4.begin(ENC4_SW);
}

// ---------------------------------------------------------------------
// OSC-over-UDP sender to TouchDesigner
//
// Sent on a separate UDP port from the video stream so it never
// competes with or interferes with frame data. Only fires when a
// value actually changes -- no polling traffic, no fixed-rate spam.
// ---------------------------------------------------------------------

// Set these in config.h, or override here:
#ifndef TD_IP
#define TD_IP "192.168.1.255"   // TouchDesigner machine's IP
#endif
#ifndef TD_OSC_PORT
#define TD_OSC_PORT 8890       // separate from VIDEO_PORT (8888)
#endif

WiFiUDP oscUdp;

// Pads a buffer length up to the next multiple of 4, per OSC spec.
static inline int oscPad4(int len) {
  return (len + 3) & ~3;
}

// Writes an OSC string: bytes + null terminator, padded to 4-byte boundary.
static int oscWriteString(uint8_t *buf, int offset, const char *s) {
  int len = strlen(s);
  memcpy(buf + offset, s, len);
  offset += len;
  int padded = oscPad4(len + 1); // +1 for at least one null terminator
  memset(buf + offset, 0, padded - len);
  return offset + (padded - len);
}

// Sends a single OSC message with one int32 argument, e.g. "/enc/0", 5
static void sendOscInt(const char *address, int32_t value) {
  uint8_t buf[64];
  int offset = 0;

  offset = oscWriteString(buf, offset, address);
  offset = oscWriteString(buf, offset, ",i");

  int32_t be = (int32_t)__builtin_bswap32((uint32_t)value); // OSC is big-endian
  memcpy(buf + offset, &be, 4);
  offset += 4;

  oscUdp.beginPacket(TD_IP, TD_OSC_PORT);
  oscUdp.write(buf, offset);
  oscUdp.endPacket();
}

// Sends a single OSC message with one float32 argument.
static void sendOscFloat(const char *address, float value) {
  uint8_t buf[64];
  int offset = 0;

  offset = oscWriteString(buf, offset, address);
  offset = oscWriteString(buf, offset, ",f");

  uint32_t raw;
  memcpy(&raw, &value, 4);
  raw = __builtin_bswap32(raw);
  memcpy(buf + offset, &raw, 4);
  offset += 4;

  oscUdp.beginPacket(TD_IP, TD_OSC_PORT);
  oscUdp.write(buf, offset);
  oscUdp.endPacket();
}

inline void initInputOsc() {
  oscUdp.begin(0); // any local port for sending
}

// Previous-frame snapshot, used to detect changes.
static long prevKnobs[4]   = {0, 0, 0, 0};
static bool prevBtnHeld[4] = {false, false, false, false};
static bool oscInitialSent = false;

// Call every loop(). Reads current input state, compares to the last
// sent state, and fires OSC messages only for values that changed.
// Addresses sent:
//   /enc/0 .. /enc/3   (int, absolute click count)
//   /btn/0 .. /btn/3   (int, 1 = pressed, 0 = released)
inline void updateInputsAndSendOsc() {
  Button *btns[4] = {&btn1, &btn2, &btn3, &btn4};

  for (int i = 0; i < 4; i++) {
    long clicks = getClicks(i);
    if (!oscInitialSent || clicks != prevKnobs[i]) {
      char addr[16];
      snprintf(addr, sizeof(addr), "/enc/%d", i);
      sendOscInt(addr, (int32_t)clicks);
      prevKnobs[i] = clicks;
    }

    bool held = btns[i]->isDown();
    if (!oscInitialSent || held != prevBtnHeld[i]) {
      char addr[16];
      snprintf(addr, sizeof(addr), "/btn/%d", i);
      sendOscInt(addr, held ? 1 : 0);
      prevBtnHeld[i] = held;
    }
  }

  oscInitialSent = true;
}
