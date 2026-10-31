#pragma once
#include "Arduino.h"

class ModbusRTUSlave {
public:
  ModbusRTUSlave(Stream&, int = -1, int = -1) {}
  void configureHoldingRegisters(uint16_t*, uint16_t) {}
  void begin(uint8_t, unsigned long, uint32_t = 0) {}
  bool poll() { return false; }
};

