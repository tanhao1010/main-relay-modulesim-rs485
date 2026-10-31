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

  // 4. Test logic moi: R1/R2 set ON -> MAN, set OFF -> AUTO, R3 doc lap
  handleCommand("AUTO", reply, sizeof(reply));
  assert(settings.autoMode);

  // Set R1 ON -> tu dong chuyen MAN
  handleCommand("R1 ON", reply, sizeof(reply));
  assert(!settings.autoMode && relayOn[0]);

  // Set R3 ON/OFF -> khong anh huong che do
  handleCommand("R3 ON", reply, sizeof(reply));
  assert(!settings.autoMode && relayOn[2]);

  // Xoa moc 1 cua R1 de khi ve AUTO khong bi gio 10:16 de len
  handleCommand("R1 DEL1", reply, sizeof(reply));
  // Set R1 OFF -> tu dong ve AUTO lai
  handleCommand("R1 OFF", reply, sizeof(reply));
  assert(settings.autoMode && !relayOn[0]);

  // Dang AUTO, set R3 ON -> van giu AUTO
  handleCommand("R3 ON", reply, sizeof(reply));
  assert(settings.autoMode && relayOn[2]);

  // 4b. Test khi nguoi dung chu dong dat MAN: bat/tat R1 hay R2 khong duoc tu dong ve AUTO
  handleCommand("MAN", reply, sizeof(reply));
  assert(!settings.autoMode);

  handleCommand("R1 ON", reply, sizeof(reply));
  assert(!settings.autoMode && relayOn[0]);

  handleCommand("R1 OFF", reply, sizeof(reply));
  assert(!settings.autoMode && !relayOn[0]); // Van o MAN, khong tu nhay sang AUTO!

  // 5. Test Modbus RTU RS485 (Serial2 tren PA2/PA3)
  const uint8_t reqFc3[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x04, 0x44, 0x09};
  for (uint8_t b : reqFc3) {
    Serial2.feedByte(b);
  }
  handleModbus();      // Doc cac byte vao buffer
  fakeTimeUs += 15000; // Troi qua 15ms (khoang lang giua cac khung Modbus)
  handleModbus();      // Phat hien het khung -> xu ly va gui phan hoi
  assert(Serial2.txLen > 5); // Co phan hoi tra ve!

  printf("ALL TESTS PASSED!\n");
  return 0;
}
