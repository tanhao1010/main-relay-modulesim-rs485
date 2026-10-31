#pragma once
#include <stdint.h>
#include <string.h>

struct FakeEEPROM {
  uint8_t data[1024] = {};
  template <typename T>
  void get(int addr, T &val) {
    if (addr >= 0 && addr + (int)sizeof(T) <= (int)sizeof(data)) {
      memcpy(&val, data + addr, sizeof(T));
    }
  }
  template <typename T>
  void put(int addr, const T &val) {
    if (addr >= 0 && addr + (int)sizeof(T) <= (int)sizeof(data)) {
      memcpy(data + addr, &val, sizeof(T));
    }
  }
};
inline FakeEEPROM EEPROM;

