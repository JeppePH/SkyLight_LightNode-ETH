#pragma once
#include <Arduino.h>

// ===== LED outputs (EDIT to match your PCB) =====
constexpr uint8_t kNumOutputs = 8;
constexpr uint8_t kDataPins[kNumOutputs] = {36, 37, 38, 39, 40, 41, 14, 15}; 

// ===== Status LEDs (flip if inverted by transistor) =====
constexpr bool LED_ACTIVE_LOW = false;
constexpr uint8_t PIN_LED_STATUS = 33;
constexpr uint8_t PIN_LED_DMX    = 34;
constexpr uint8_t PIN_LED_POLL   = 35;

inline void ledInit() {
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_LED_DMX,    OUTPUT);
  pinMode(PIN_LED_POLL,   OUTPUT);
}

inline void ledWrite(uint8_t pin, bool on) {
  digitalWrite(pin, LED_ACTIVE_LOW ? !on : on);
}

// ===== DIP SWITCH: directly on GPIO =====
// Set the 8 pins, DIP1 = bit0 (LSB), DIP8 = bit7 (MSB)
constexpr uint8_t kDipPins[8] = {
  8, 7, 6, 5, 4, 3, 2, 1 
};

// If DIP "ON" shorts to GND, keep true to enable INPUT_PULLUP and invert reads.
constexpr bool DIP_PULLUP     = true;
constexpr bool DIP_ACTIVE_LOW = true;

inline void dipInit() {
  for (uint8_t i = 0; i < 8; ++i) {
    pinMode(kDipPins[i], DIP_PULLUP ? INPUT_PULLUP : INPUT);
  }
}

// Read once (at boot). No debounce needed since it’s static.
inline uint8_t readDip8() {
  uint8_t v = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    uint8_t b = (uint8_t)digitalRead(kDipPins[i]);   // 1 = HIGH, 0 = LOW
    if (DIP_ACTIVE_LOW) b = !b;                      // normalize so "ON" = 1
    v |= (b & 0x01) << i;                            // DIP1 -> bit0, DIP8 -> bit7
  }
  return v;
}
