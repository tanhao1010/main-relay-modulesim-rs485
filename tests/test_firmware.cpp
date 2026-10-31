#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/main.cpp"

extern "C" {
  void *__CTOR_LIST__[2] = {0, 0};
  void *__DTOR_LIST__[2] = {0, 0};
}

uint64_t fakeTimeUs = 0;
unsigned fakeAdc = 2048;
int fakePins[64] = {};
FakeSerial Serial1, Serial2, Serial3;
FakeWire Wire;

int main() {
  memset(EEPROM.data, 0xFF, sizeof(EEPROM.data));
  setup();

  // 1. Test chay khong co module SIM: sau 8 giay tu dong chuyen sang SIM_OFFLINE
  for (int i = 0; i < 8500; i++) {
    fakeTimeUs += 1000;
    loop();
  }
  assert(simState == SIM_OFFLINE);

  // 2. Test dieu khien qua Serial / handleCommand khi khong co SIM
  char reply[180];
  handleCommand("TIME 2026-10-31 10:16:00", reply, sizeof(reply));
  assert(rtcValid && rtc.year == 2026 && rtc.month == 10 && rtc.day == 31);

  handleCommand("AUTO;R1 1=1015-1110 2=1630-1730", reply, sizeof(reply));
  assert(settings.autoMode && relayOn[0]);

  handleCommand("MAN;R1 OFF;R2 ON;R3 ON", reply, sizeof(reply));
  assert(!settings.autoMode && !relayOn[0] && relayOn[1] && relayOn[2]);

  // 3. Test khi cam module SIM vao (Text Mode)
  Serial1.feed("OK\r\n");
  handleSim();
  assert(simState == SIM_READY);

  Serial1.feed("+CMT: \"+84912345678\",\"\",\"26/10/31,10:20:00+28\"\r\nMAN;R1 ON\r\n");
  Serial1.feed(">");
  handleSim();
  assert(relayOn[0]);

  // 4. Test Modbus RTU RS485
  // Doc 4 thanh ghi tu dia chi 0 (01 03 00 00 00 04 44 09)
  const uint8_t reqFc3[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x04, 0x44, 0x09};
  for (uint8_t b : reqFc3) {
    char s[2] = {(char)b, 0};
    Serial3.feed(s);
  }
  fakeTimeUs += 15000; // Doi 15ms cho het khoang lang Modbus
  handleModbus();
  assert(Serial3.txLen > 5); // Co phan hoi tra ve!

  printf("ALL TESTS PASSED!\n");
  return 0;
}
