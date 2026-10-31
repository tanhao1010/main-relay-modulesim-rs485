#pragma once
// Chi dung cho host tests: khong duoc include khi build firmware.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_ANALOG 2
#define INPUT_PULLUP 3
enum {
  PA1 = 1,
  PA2,
  PA3,
  PA7 = 7,
  PA9 = 9,
  PA10,
  PB0 = 16,
  PB1,
  PB5 = 21,
  PB6,
  PB7,
  PB8,
  PB10 = 26,
  PB11,
  PB12,
  PB13,
  PB14,
  PB15
};
extern uint64_t fakeTimeUs;
extern unsigned fakeAdc;
extern int fakePins[64];
inline uint32_t micros() { return (uint32_t)fakeTimeUs; }
inline uint32_t millis() { return (uint32_t)(fakeTimeUs / 1000); }
inline void delay(unsigned ms) { fakeTimeUs += (uint64_t)ms * 1000; }
inline void delayMicroseconds(unsigned us) { fakeTimeUs += us; }
inline void pinMode(unsigned pin, int mode) {
  if (mode == INPUT_PULLUP)
    fakePins[pin] = HIGH;
}
inline void digitalWrite(unsigned pin, int state) { fakePins[pin] = state; }
inline int digitalRead(unsigned pin) { return fakePins[pin]; }
inline void analogReadResolution(int) {}
inline uint16_t analogRead(unsigned) { return fakeAdc; }
struct Stream {};
struct FakeSerial : public Stream {
  char rx[2048] = {};
  unsigned rxLen = 0;
  char tx[4096] = {};
  unsigned txLen = 0;
  void setRx(unsigned) {}
  void setTx(unsigned) {}
  void begin(unsigned) {}
  int available() { return (int)rxLen; }
  int availableForWrite() { return 1024; }
  int peek() {
    if (!rxLen)
      return -1;
    return (unsigned char)rx[0];
  }
  int read() {
    if (!rxLen)
      return -1;
    unsigned char c = rx[0];
    memmove(rx, rx + 1, --rxLen);
    return c;
  }
  void feedByte(uint8_t b) {
    if (rxLen + 1 < sizeof(rx)) {
      rx[rxLen++] = (char)b;
    }
  }
  void feed(const char *s) {
    size_t n = strlen(s);
    if (rxLen + n < sizeof(rx)) {
      memcpy(rx + rxLen, s, n);
      rxLen += (unsigned)n;
    }
  }
  void write(uint8_t c) {
    if (c == '\r') {
      int start = (int)txLen - 1;
      while (start >= 0 && tx[start] != '\r' && tx[start] != '\n') {
        start--;
      }
      start++;
      if (strstr(tx + start, "AT+CMGS=") != NULL) {
        feed(">");
      }
    }
    if (txLen + 1 < sizeof(tx)) {
      tx[txLen++] = (char)c;
      tx[txLen] = 0;
    }
  }
  void write(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
      write(b[i]);
  }
  void flush() {}
  void print(char c) { write((uint8_t)c); }
  void print(int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    print(buf);
  }
  void print(const char *s) {
    while (*s)
      write((uint8_t)*s++);
  }
  void println(const char *s = "") {
    print(s);
    print("\r\n");
  }
};
extern FakeSerial Serial1, Serial2, Serial3;
