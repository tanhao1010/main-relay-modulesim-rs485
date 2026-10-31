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
  fakeTimeUs += 16000; // Troi qua 16ms (khoang lang giua cac khung Modbus)
  handleModbus();      // Phat hien het khung -> xu ly va gui phan hoi
  assert(Serial2.txLen > 5); // Co phan hoi tra ve!

  // 6. Test Nut Nhan (BT1..BT4)
  fakePins[PB12] = HIGH;
  fakePins[PB13] = HIGH;
  fakePins[PB14] = HIGH;
  fakePins[PB15] = HIGH;

  // 6a. Test BT2 (Relay 1), BT3 (Relay 2), BT4 (Relay 3)
  // Dang o MAN, tat ca relay dang OFF
  setRelay(0, false); setRelay(1, false); setRelay(2, false);

  // Nhan BT2: chuyen xuong LOW -> cho 60ms debounce -> kiem tra -> nha len HIGH -> cho 60ms
  fakePins[PB13] = LOW;
  handleButtons(); // Bat dau dem debounce khi nut vua nhan xuong
  fakeTimeUs += 60000;
  handleButtons(); // Du 60ms -> Kich hoat su kien nhan nut BT2
  assert(relayOn[0]);

  fakePins[PB13] = HIGH;
  handleButtons(); // Bat dau dem debounce khi nut duoc nha ra
  fakeTimeUs += 60000;
  handleButtons(); // Du 60ms -> Hoan tat nha nut
  assert(relayOn[0]); // Relay 1 van giu nguyen ON

  // Nhan BT3 -> Relay 2 bat len ON
  fakePins[PB14] = LOW;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  assert(relayOn[1]);
  fakePins[PB14] = HIGH;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();

  // Nhan BT4 -> Relay 3 bat len ON
  fakePins[PB15] = LOW;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  assert(relayOn[2]);
  fakePins[PB15] = HIGH;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();

  // Nhan BT4 lan nua -> Relay 3 tat OFF
  fakePins[PB15] = LOW;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  assert(!relayOn[2]);
  fakePins[PB15] = HIGH;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();

  // 6b. Test BT1 chuyen tu MAN sang AUTO (3 relay bat 1s, tat 1s 2 lan)
  assert(!settings.autoMode);
  fakePins[PB12] = LOW;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  fakePins[PB12] = HIGH;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  assert(modeBlinkStep == 1 && relayOn[0] && relayOn[1] && relayOn[2]); // Bat lan 1

  fakeTimeUs += 1001000;
  handleButtons();
  assert(modeBlinkStep == 2 && !relayOn[0] && !relayOn[1] && !relayOn[2]); // Tat lan 1

  fakeTimeUs += 1001000;
  handleButtons();
  assert(modeBlinkStep == 3 && relayOn[0] && relayOn[1] && relayOn[2]); // Bat lan 2

  fakeTimeUs += 1001000;
  handleButtons();
  assert(modeBlinkStep == 4 && !relayOn[0] && !relayOn[1] && !relayOn[2]); // Tat lan 2

  fakeTimeUs += 1001000;
  handleButtons();
  assert(modeBlinkStep == 0 && settings.autoMode); // Hoan tat: ve AUTO!

  // 6c. Test BT1 chuyen tu AUTO sang MAN (3 relay bat 1s, tat 1s 1 lan)
  fakePins[PB12] = LOW;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  fakePins[PB12] = HIGH;
  handleButtons();
  fakeTimeUs += 60000;
  handleButtons();
  assert(modeBlinkStep == 1 && relayOn[0] && relayOn[1] && relayOn[2]); // Bat 1s

  fakeTimeUs += 1001000;
  handleButtons();
  assert(modeBlinkStep == 2 && !relayOn[0] && !relayOn[1] && !relayOn[2]); // Tat 1s

  fakeTimeUs += 1001000;
  handleButtons();
  assert(modeBlinkStep == 0 && !settings.autoMode); // Hoan tat: ve MAN!

  // 7. Test cai dat Bao Cao Dinh Ky qua SMS/Serial & Modbus RTU
  // 7a. Xem trang thai ban dau: mac dinh la OFF
  handleCommand("REPORT ?", reply, sizeof(reply));
  assert(strstr(reply, "OFF") != NULL);

  // 7b. Cai dat qua lenh: REPORT 2 08:30 0987654321 (2 ngay/lan, 8h30 sang)
  handleCommand("REPORT 2 08:30 0987654321", reply, sizeof(reply));
  assert(settings.reportDays == 2);
  assert(settings.reportHour == 8);
  assert(settings.reportMinute == 30);
  assert(strcmp(settings.reportPhone, "0987654321") == 0);
  assert(strstr(reply, "2 NGAY/LAN") != NULL);

  // 7c. Dong bo len vung nho Modbus RTU (Holding Registers 32..35)
  updateHoldingRegsFromSystem();
  assert(holdingRegs[REG_REPORT_DAYS] == 2);
  assert(holdingRegs[REG_REPORT_HOUR] == 8);
  assert(holdingRegs[REG_REPORT_MIN]  == 30);

  // 7d. Ghi qua Modbus RTU (Function 06 ghi vao Reg 32 = 5 ngay/lan)
  // Request: 01 06 00 20 00 05 CRC_L CRC_H (Reg 32 = 0x0020, Val = 5, CRC = 0x48 0x03)
  uint8_t reqFc6[] = {0x01, 0x06, 0x00, 0x20, 0x00, 0x05, 0x48, 0x03};
  Serial2.txLen = 0;
  for (uint8_t b : reqFc6) {
    Serial2.feedByte(b);
  }
  handleModbus();
  fakeTimeUs += 16000;
  handleModbus();
  assert(Serial2.txLen >= 6);
  assert(settings.reportDays == 5); // Duoc cap nhat qua Modbus thanh 5 ngay!

  // 7e. Test tat bao cao dinh ky qua lenh SMS: BC OFF
  handleCommand("BC OFF", reply, sizeof(reply));
  assert(settings.reportDays == 0);
  assert(strstr(reply, "OFF") != NULL);

  // 7f. Test tu dong phat tin nhan bao cao dinh ky khi den gio hen
  settings.reportDays = 1;
  settings.reportHour = 7;
  settings.reportMinute = 0;
  strcpy(settings.reportPhone, "+84912345678");
  settings.lastReportDay = dateToEpochDays(2026, 10, 30); // Lan gui truoc la ngay 30
  rtc = {2026, 10, 31, 7, 0, 0}; // Hom nay la ngay 31 luc 07:00
  rtcValid = true;
  simState = SIM_READY; // SIM dang san sang
  Serial1.txLen = 0;
  Serial1.rxLen = 0;

  checkPeriodicReport();
  assert(Serial1.txLen > 20); // Da phat tin nhan SMS bao cao!
  assert(strstr(Serial1.tx, "AT+CMGS=") != NULL);
  assert(settings.lastReportDay == dateToEpochDays(2026, 10, 31)); // Da cap nhat ngay gui

  // Goi lai ngay lap tuc -> khong duoc gui trung lap
  unsigned oldTxLen = Serial1.txLen;
  checkPeriodicReport();
  assert(Serial1.txLen == oldTxLen); // Khong phat lai nua!

  // 8. Test co che tu phuc hoi SIM 24/24 (Self-Healing Heartbeat & Hardware Reset)
  // 8a. Gia lap SIM bi treo (khong tra loi Heartbeat trong 3 phut)
  simState = SIM_READY;
  simHeartbeatMissed = 0;
  lastSimHeartbeatMs = millis();

  // Phut 1: gui AT heartbeat, khong ai tra loi
  fakeTimeUs += 61000000;
  handleSim();
  assert(simHeartbeatMissed == 1 && simState == SIM_READY);

  // Phut 2: gui AT heartbeat lan 2, khong ai tra loi
  fakeTimeUs += 61000000;
  handleSim();
  assert(simHeartbeatMissed == 2 && simState == SIM_READY);

  // Phut 3: lan thu 3 khong tra loi -> Tu dong kich hoat Reset phan cung qua chan PB8 (SIM_RST)
  fakeTimeUs += 61000000;
  handleSim();
  assert(simState == SIM_CHECK_AT); // Da tu dong chuyen ve khoi tao lai tu dau!

  // Khi module SIM reboot xong va tra loi OK -> tu dong Ready tro lai
  Serial1.feed("OK\r\n");
  handleSim();
  assert(simState == SIM_READY);

  // 8b. Test SIM Offline tu dong tham do va phuc hoi khi nguoi dung cam SIM vao sau
  simState = SIM_OFFLINE;
  lastSimCheckMs = millis();
  fakeTimeUs += 31000000; // Sau 30 giay
  Serial1.txLen = 0;
  handleSim();
  assert(strstr(Serial1.tx, "AT\r") != NULL); // Co gui lenh tham do AT!

  // SIM tra loi OK -> tu dong chuyen ve SIM_READY ma khong can reset MCU!
  Serial1.feed("OK\r\n");
  handleSim();
  assert(simState == SIM_READY);

  // 9. Test Chuc nang Chuyen tiep tin nhan (SMS Forward)
  // 9a. Xem trang thai ban dau: mac dinh la OFF
  handleCommand("FW ?", reply, sizeof(reply));
  assert(strstr(reply, "OFF") != NULL);

  // 9b. Cai dat so dien thoai nhan tin chuyen tiep: FW 0912345678
  handleCommand("FW 0912345678", reply, sizeof(reply));
  assert(strcmp(settings.forwardPhone, "0912345678") == 0);
  assert(strstr(reply, "0912345678") != NULL);

  // 9c. Nhan tin nhan thuong (OTP / nha mang 191 / quang cao) -> Phai chuyen tiep den 0912345678
  Serial1.txLen = 0;
  Serial1.feed("+CMT: \"191\",\"\",\"26/10/31,14:55:00+28\"\r\nMa OTP cua ban la 839201\r\n");
  handleSim();
  assert(strstr(Serial1.tx, "AT+CMGS=\"0912345678\"") != NULL); // Chuyen tiep den dung so da cai!
  assert(strstr(Serial1.tx, "[FW 191]: Ma OTP cua ban la 839201") != NULL);

  // 9d. Nhan tin nhan DIEU KHIEN (vi du R1 ON) -> Xu ly lenh, KHONG duoc forward
  Serial1.txLen = 0;
  Serial1.feed("+CMT: \"+84988888888\",\"\",\"26/10/31,14:56:00+28\"\r\nR1 ON\r\n");
  handleSim();
  assert(strstr(Serial1.tx, "AT+CMGS=\"+84988888888\"") != NULL); // Tra loi cho nguoi gui lenh
  assert(strstr(Serial1.tx, "AT+CMGS=\"0912345678\"") == NULL);  // Tuyet doi khong forward lenh dieu khien!

  // 9e. Tat chuyen tiep: FW OFF
  handleCommand("FW OFF", reply, sizeof(reply));
  assert(settings.forwardPhone[0] == '\0');
  assert(strstr(reply, "OFF") != NULL);

  printf("ALL TESTS PASSED!\n");
  return 0;
}
