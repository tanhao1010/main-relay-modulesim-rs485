#include "variant_generic.h"
#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ==================== 1. KHAI BAO CHAN PHAN CUNG ====================
#define LED_ACT     PB5
#define RELAY1      PA7
#define RELAY2      PB1
#define RELAY3      PB0
#define ACS712      PA1
#define DS3231_ADDR 0x68

#define BUTTON_1    PB12
#define BUTTON_2    PB13
#define BUTTON_3    PB14
#define BUTTON_4    PB15

#define RX2         PA10   // SIM: USART1 RX
#define TX2         PA9    // SIM: USART1 TX
#define SIM_RST     PB8

// 1. Debug (USART3 tren PB10 TX, PB11 RX)
#define RX1         PB11   // Debug RX
#define TX1         PB10   // Debug TX
#define DebugSerial Serial3

// 2. RS485 (USART2 tren PA2 TX, PA3 RX)
#define RS485_RX    PA3    // RS485 RX
#define RS485_TX    PA2    // RS485 TX
#define RS485_DE    -1     // Chân DE/RE (-1 nếu module tự động chuyển chiều)
#define RS485Serial Serial2

#define SimSerial   Serial1


// ==================== 2. CAU HINH CO BAN ====================
// De trong "" thi so dien thoai nao nhan tin cung dieu khien duoc (de test).
// Neu dien so (vi du "+84912345678" hoac "0912345678") thi chi nhan so do.
const char ALLOWED_PHONE[] = "";

const uint32_t SIM_BAUD        = 115200;
const uint32_t RS485_BAUD      = 9600;
const uint8_t  MODBUS_SLAVE_ID = 1;    // ID Slave Modbus RS485 = 1
const uint8_t  RELAY_ON_LEVEL  = HIGH; // Doi thanh LOW neu mach relay kich muc thap

// Thong so tinh dong dien ACS712-05B (5A) va cong suat
const float VOLTAGE_DEFAULT = 220.0f;  // Dien ap mac dinh 220V
const float ADC_REF_VOLT    = 3.3f;    // Dien ap tham chieu ADC STM32
const float ACS_SENSITIVITY = 0.185f;  // Do nhay ACS712 5A: 185mV/A = 0.185V/A
const float ACS_NO_LOAD_A   = 0.05f;   // Nguong loc nhieu khi khong tai (A)

// Dia chi luu trong EEPROM
const int EEPROM_ADDR_SETTINGS = 0;
const int EEPROM_ADDR_ENERGY   = 128;
const uint32_t MAGIC_SETTINGS  = 0x12345678;
const uint32_t MAGIC_ENERGY    = 0x87654321;

// ==================== 3. CAU TRUC DU LIEU & BIEN TOAN CUC ====================
// Cau truc 1 moc hen gio
struct ScheduleSlot {
  bool enabled;        // true: dang bat moc nay, false: trong
  uint16_t onMinute;   // Thoi gian bat tinh bang phut trong ngay (gio * 60 + phut)
  uint16_t offMinute;  // Thoi gian tat tinh bang phut trong ngay (gio * 60 + phut)
};

// Cau truc cai dat luu EEPROM
struct Settings {
  uint32_t magic;
  bool autoMode;                 // true = AUTO, false = MAN
  ScheduleSlot schedules[3][3];  // 3 relay (0..2), moi relay 3 moc (0..2)
};

// Cau truc luu so dien kWh
struct EnergyStore {
  uint32_t magic;
  double energyKwh;
};

// Cau truc thoi gian thuc
struct RtcTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

const uint32_t relayPins[3] = {RELAY1, RELAY2, RELAY3};
bool relayOn[3] = {false, false, false};

Settings settings;
RtcTime rtc = {2026, 1, 1, 0, 0, 0};
bool rtcValid = false;
bool useSoftRtc = false; // Cho phep chay gio mem neu chua cam DS3231 ma cai gio qua Serial/RS485
bool autoOverridden[2] = {false, false}; // Co danh dau Relay 1, 2 duoc bat tam thoi tu AUTO sang MAN

// Bien do dong dien, cong suat, dien nang cua Relay 1
float currentAmp = 0.0f;
float powerWatt  = 0.0f;
double energyKwh = 0.0;

// ==================== 4. LUU VA DOC EEPROM ====================
void saveSettings() {
  settings.magic = MAGIC_SETTINGS;
  EEPROM.put(EEPROM_ADDR_SETTINGS, settings);
}

void loadSettings() {
  EEPROM.get(EEPROM_ADDR_SETTINGS, settings);

  // Neu EEPROM chua co du lieu hop le thi khoi tao mac dinh (MAN, tat ca lich trong)
  if (settings.magic != MAGIC_SETTINGS) {
    settings.magic = MAGIC_SETTINGS;
    settings.autoMode = false;
    for (int r = 0; r < 3; r++) {
      for (int i = 0; i < 3; i++) {
        settings.schedules[r][i].enabled = false;
        settings.schedules[r][i].onMinute = 0;
        settings.schedules[r][i].offMinute = 0;
      }
    }
    saveSettings();
  }
}

void saveEnergy() {
  EnergyStore store;
  store.magic = MAGIC_ENERGY;
  store.energyKwh = energyKwh;
  EEPROM.put(EEPROM_ADDR_ENERGY, store);
}

void loadEnergy() {
  EnergyStore store;
  EEPROM.get(EEPROM_ADDR_ENERGY, store);
  if (store.magic == MAGIC_ENERGY && store.energyKwh >= 0.0 && isfinite(store.energyKwh)) {
    energyKwh = store.energyKwh;
  } else {
    energyKwh = 0.0;
    saveEnergy();
  }
}

// ==================== 5. DONG HO THOI GIAN THUC DS3231 ====================
uint8_t decToBcd(uint8_t val) {
  return (val / 10 * 16) + (val % 10);
}

uint8_t bcdToDec(uint8_t val) {
  return (val / 16 * 10) + (val % 16);
}

bool isValidDate(const RtcTime &t) {
  if (t.year < 2000 || t.year > 2099) return false;
  if (t.month < 1 || t.month > 12) return false;
  if (t.day < 1 || t.day > 31) return false;
  if (t.hour > 23 || t.minute > 59 || t.second > 59) return false;
  return true;
}

bool readRtc() {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    // Neu khong cam DS3231 nhung da cai gio mem qua lenh TIME / RS485 thi van tang gio mem
    if (useSoftRtc) {
      rtc.second++;
      if (rtc.second >= 60) { rtc.second = 0; rtc.minute++; }
      if (rtc.minute >= 60) { rtc.minute = 0; rtc.hour++; }
      if (rtc.hour >= 24)   { rtc.hour = 0; }
      rtcValid = true;
      return true;
    }
    rtcValid = false;
    return false;
  }

  if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) != 7) {
    rtcValid = false;
    return false;
  }

  uint8_t sec   = bcdToDec(Wire.read() & 0x7F);
  uint8_t min   = bcdToDec(Wire.read() & 0x7F);
  uint8_t hour  = bcdToDec(Wire.read() & 0x3F);
  Wire.read(); // Bo qua thu trong tuan
  uint8_t day   = bcdToDec(Wire.read() & 0x3F);
  uint8_t month = bcdToDec(Wire.read() & 0x1F);
  uint8_t year  = bcdToDec(Wire.read());

  RtcTime next = {(uint16_t)(2000 + year), month, day, hour, min, sec};
  if (!isValidDate(next)) {
    rtcValid = false;
    return false;
  }

  rtc = next;
  rtcValid = true;
  useSoftRtc = false;
  return true;
}

bool setRtc(const RtcTime &t) {
  if (!isValidDate(t)) return false;

  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  Wire.write(decToBcd(t.second));
  Wire.write(decToBcd(t.minute));
  Wire.write(decToBcd(t.hour));
  Wire.write(1); // Thu trong tuan
  Wire.write(decToBcd(t.day));
  Wire.write(decToBcd(t.month));
  Wire.write(decToBcd(t.year - 2000));

  if (Wire.endTransmission() == 0) {
    // Xoa co OSF trong thanh ghi Status (0x0F)
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x0F);
    Wire.write(0x00);
    Wire.endTransmission();

    rtc = t;
    rtcValid = true;
    useSoftRtc = false;
    return true;
  }

  // Neu chua lap DS3231 tren mach thi van luu vao gio mem de test binh thuong
  rtc = t;
  rtcValid = true;
  useSoftRtc = true;
  return true;
}

// ==================== 6. DO DONG ACS712, TINH CONG SUAT P & DIEN NANG E ====================
uint32_t lastSampleTimeMs = 0;
uint32_t windowStartMs    = 0;
uint32_t sampleCount      = 0;
double sumAdc             = 0;
double sumSqAdc           = 0;

void updateEnergyWindow(uint32_t nowMs) {
  uint32_t elapsedMs = nowMs - windowStartMs;
  if (sampleCount > 10 && elapsedMs > 0) {
    // Tinh gia tri trung binh (diem 0 dong AC) va do lech hieu dung RMS
    double meanAdc = sumAdc / sampleCount;
    double variance = (sumSqAdc / sampleCount) - (meanAdc * meanAdc);
    if (variance < 0) variance = 0;

    double rmsAdc = sqrt(variance);
    float rmsVolt = (float)rmsAdc * (ADC_REF_VOLT / 4095.0f);
    float amp = rmsVolt / ACS_SENSITIVITY;

    // Loc nhieu khi khong co tai
    if (amp < ACS_NO_LOAD_A) {
      amp = 0.0f;
    }

    currentAmp = amp;
    powerWatt  = VOLTAGE_DEFAULT * currentAmp;

    // Cong tich luy dien nang: E (kWh) += P (W) * thoi_gian (gio) / 1000
    // 1 gio = 3,600,000 ms -> chia cho 3,600,000,000
    energyKwh += (double)powerWatt * (double)elapsedMs / 3600000000.0;
  }

  // Reset cua so do moi
  windowStartMs = nowMs;
  sampleCount   = 0;
  sumAdc        = 0;
  sumSqAdc      = 0;
}

void readCurrentRelay1() {
  uint32_t nowMs = millis();

  // Lay mau moi 1 ms
  if (nowMs - lastSampleTimeMs >= 1) {
    lastSampleTimeMs = nowMs;
    uint16_t adcVal = analogRead(ACS712);
    sampleCount++;
    sumAdc   += adcVal;
    sumSqAdc += (double)adcVal * adcVal;
  }

  // Moi 200 ms (10 chu ky dien 50Hz) tinh toan I, P, E mot lan
  if (nowMs - windowStartMs >= 200) {
    updateEnergyWindow(nowMs);
  }
}

// ==================== 7. DIEU KHIEN RELAY VA KIEM TRA LICH HEN ====================
void setRelay(int relayIndex, bool turnOn) {
  if (relayIndex < 0 || relayIndex >= 3) return;
  if (relayOn[relayIndex] == turnOn) return; // Khong doi trang thai thi khong can lam gi

  // Neu Relay 1 doi trang thai thi chot dien nang truoc
  if (relayIndex == 0) {
    updateEnergyWindow(millis());
  }

  digitalWrite(relayPins[relayIndex], turnOn ? RELAY_ON_LEVEL : !RELAY_ON_LEVEL);
  relayOn[relayIndex] = turnOn;

  // Chi luu so dien vao EEPROM khi Relay 1 doi trang thai ON <-> OFF
  if (relayIndex == 0) {
    saveEnergy();
  }
}

bool isRelayScheduledOn(int relayIndex, uint16_t currentMinute) {
  for (int i = 0; i < 3; i++) {
    ScheduleSlot slot = settings.schedules[relayIndex][i];
    if (!slot.enabled || slot.onMinute == slot.offMinute) continue;

    if (slot.onMinute < slot.offMinute) {
      // Lich binh thuong trong ngay (vi du 10:15 -> 11:10)
      if (currentMinute >= slot.onMinute && currentMinute < slot.offMinute) {
        return true;
      }
    } else {
      // Lich qua dem (vi du 22:00 -> 06:00 sang hom sau)
      if (currentMinute >= slot.onMinute || currentMinute < slot.offMinute) {
        return true;
      }
    }
  }
  return false;
}

void checkSchedule() {
  // Neu RTC loi va dang AUTO thi tat Relay 1 & Relay 2 de an toan
  if (!rtcValid) {
    if (settings.autoMode) {
      setRelay(0, false);
      setRelay(1, false);
    }
    return;
  }

  // Relay 1 va Relay 2 chay theo lich khi o che do AUTO
  // Relay 3 hoan toan doc lap, dieu khien tay (MAN) khong bi can thiep
  if (settings.autoMode) {
    uint16_t nowMinute = rtc.hour * 60 + rtc.minute;
    setRelay(0, isRelayScheduledOn(0, nowMinute));
    setRelay(1, isRelayScheduledOn(1, nowMinute));
  }
}

// ==================== 8. SOAN TIN NHAN TRANG THAI & XU LY LENH ====================
void makeStatus(char *out, size_t maxLen) {
  char timeStr[24];
  if (rtcValid) {
    snprintf(timeStr, sizeof(timeStr), "%02u/%02u/%02u %02u:%02u",
             rtc.day, rtc.month, (rtc.year - 2000) % 100, rtc.hour, rtc.minute);
  } else {
    strcpy(timeStr, "RTC:ERR");
  }

  // Doi so thuc sang so nguyen de in bang snprintf tren STM32 (tranh loi khong ho tro %f)
  int ampX100 = (int)(currentAmp * 100.0f + 0.5f);
  int wattInt = (int)(powerWatt + 0.5f);
  unsigned long kwhX1000 = (unsigned long)(energyKwh * 1000.0 + 0.5);

  int pos = snprintf(out, maxLen, "OK %s %s I=%d.%02dA P=%dW E=%lu.%03lukWh",
                     settings.autoMode ? "AUTO" : "MAN",
                     timeStr,
                     ampX100 / 100, ampX100 % 100,
                     wattInt,
                     kwhX1000 / 1000, kwhX1000 % 1000);

  for (int r = 0; r < 3; r++) {
    pos += snprintf(out + pos, maxLen - pos, "\nR%d=%d:", r + 1, relayOn[r] ? 1 : 0);
    for (int i = 0; i < 3; i++) {
      ScheduleSlot s = settings.schedules[r][i];
      if (s.enabled) {
        pos += snprintf(out + pos, maxLen - pos, "%s%02u%02u-%02u%02u",
                        (i > 0) ? "," : "",
                        s.onMinute / 60, s.onMinute % 60,
                        s.offMinute / 60, s.offMinute % 60);
      } else {
        pos += snprintf(out + pos, maxLen - pos, "%s--", (i > 0) ? "," : "");
      }
    }
  }
}

// Ham chuyen chuoi "1015" hoac "10:15" hoac "10H15" sang so phut trong ngay
bool parseTimeMinute(const char *str, uint16_t &outMin) {
  int h = -1, m = -1;
  int len = strlen(str);
  if (len == 4 && isdigit(str[0]) && isdigit(str[1]) && isdigit(str[2]) && isdigit(str[3])) {
    h = (str[0] - '0') * 10 + (str[1] - '0');
    m = (str[2] - '0') * 10 + (str[3] - '0');
  } else if (len == 5 && (str[2] == ':' || str[2] == 'H') &&
             isdigit(str[0]) && isdigit(str[1]) && isdigit(str[3]) && isdigit(str[4])) {
    h = (str[0] - '0') * 10 + (str[1] - '0');
    m = (str[3] - '0') * 10 + (str[4] - '0');
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  outMin = h * 60 + m;
  return true;
}

// Ham cat khoang trang 2 dau chuoi
char *trimString(char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
    *(--end) = '\0';
  }
  return s;
}

// Xu ly chuoi lenh (dung chung cho ca Serial Monitor va SMS)
void handleCommand(const char *rawInput, char *reply, size_t replySize) {
  char buffer[180];
  strncpy(buffer, rawInput, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  // Doi sang chu in hoa de khong phan biet hoa/thuong
  for (int i = 0; buffer[i]; i++) {
    buffer[i] = toupper((unsigned char)buffer[i]);
  }

  Settings nextSettings = settings;
  int manualOutput[3] = {-1, -1, -1}; // -1: giu nguyen, 0: OFF, 1: ON
  bool settingsChanged = false;

  // Tach cac nhom lenh ngan cach boi dau ';'
  char *group = buffer;
  while (group != NULL && *group != '\0') {
    char *nextGroup = strchr(group, ';');
    if (nextGroup != NULL) {
      *nextGroup = '\0';
      nextGroup++;
    }

    char *cmd = trimString(group);
    group = nextGroup;

    // Tach cac tu (token) cach nhau boi dau cach
    char *tokens[10];
    int tokenCount = 0;
    char *word = strtok(cmd, " ");
    while (word != NULL && tokenCount < 10) {
      tokens[tokenCount++] = word;
      word = strtok(NULL, " ");
    }

    if (tokenCount == 0) {
      continue;
    }

    // 1. Lenh xem trang thai: ? hoac STATUS
    if (tokenCount == 1 && (strcmp(tokens[0], "?") == 0 || strcmp(tokens[0], "STATUS") == 0)) {
      // Khong thay doi gi, cuoi ham se in trang thai
    }
    // 2. Lenh doi che do: AUTO / MAN / SET AUTO / SET MAN
    else if ((tokenCount == 1 && (strcmp(tokens[0], "AUTO") == 0 || strcmp(tokens[0], "MAN") == 0)) ||
             (tokenCount == 2 && strcmp(tokens[0], "SET") == 0 &&
              (strcmp(tokens[1], "AUTO") == 0 || strcmp(tokens[1], "MAN") == 0))) {
      const char *modeStr = tokens[tokenCount - 1];
      nextSettings.autoMode = (strcmp(modeStr, "AUTO") == 0);
      autoOverridden[0] = false;
      autoOverridden[1] = false;
      settingsChanged = true;
    }
    // 3. Lenh cai thoi gian: TIME YYYY-MM-DD HH:MM:SS
    else if (tokenCount == 3 && strcmp(tokens[0], "TIME") == 0) {
      int y, m, d, hh, mm, ss;
      if (sscanf(tokens[1], "%d-%d-%d", &y, &m, &d) == 3 &&
          sscanf(tokens[2], "%d:%d:%d", &hh, &mm, &ss) == 3) {
        RtcTime newTime = {(uint16_t)y, (uint8_t)m, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss};
        if (!setRtc(newTime)) {
          snprintf(reply, replySize, "ERR INVALID TIME");
          return;
        }
      } else {
        snprintf(reply, replySize, "ERR TIME FORMAT: TIME YYYY-MM-DD HH:MM:SS");
        return;
      }
    }
    // 4. Cac lenh lien quan den R1, R2, R3 (hoac RELAY1, RELAY2, RELAY3)
    else {
      int relayIdx = -1;
      if (strncmp(tokens[0], "RELAY", 5) == 0 && strlen(tokens[0]) == 6) {
        relayIdx = tokens[0][5] - '1';
      } else if (tokens[0][0] == 'R' && strlen(tokens[0]) == 2) {
        relayIdx = tokens[0][1] - '1';
      }

      if (relayIdx < 0 || relayIdx > 2 || tokenCount < 2) {
        snprintf(reply, replySize, "ERR SYNTAX");
        return;
      }

      // R1 LIST
      if (tokenCount == 2 && strcmp(tokens[1], "LIST") == 0) {
        // Chi xem trang thai
      }
      // R1 ON hoac R1 OFF
      else if (tokenCount == 2 && (strcmp(tokens[1], "ON") == 0 || strcmp(tokens[1], "OFF") == 0)) {
        bool turnOn = (strcmp(tokens[1], "ON") == 0);
        manualOutput[relayIdx] = turnOn ? 1 : 0;

        // Quy tac nguoi dung:
        // - Khi dang o che do AUTO:
        //   + Set Relay 1 hoac 2 len 1 (ON) -> he thong chuyen sang MAN, danh dau override
        //   + Khi xong set ve 0 (OFF) -> tu dong tro ve AUTO lai
        // - Khi he thong dang o MAN (chu dong dat MAN):
        //   + Set 1 hay 0 deu giu nguyen MAN, khong tu chuyen mode
        // - Relay 3: MAN doc lap, khong anh huong che do
        if (relayIdx == 0 || relayIdx == 1) {
          if (turnOn) {
            if (nextSettings.autoMode) {
              nextSettings.autoMode = false; // Chuyen sang MAN
              autoOverridden[relayIdx] = true;
              settingsChanged = true;
            }
          } else {
            if (autoOverridden[relayIdx]) {
              autoOverridden[relayIdx] = false;
              if (!autoOverridden[0] && !autoOverridden[1]) {
                nextSettings.autoMode = true; // Tra ve AUTO
                settingsChanged = true;
              }
            }
          }
        }
      }
      // R1 DEL1, R1 DEL23, R1 DEL123
      else if (tokenCount == 2 && strncmp(tokens[1], "DEL", 3) == 0) {
        const char *delList = tokens[1] + 3;
        if (*delList == '\0') {
          snprintf(reply, replySize, "ERR DEL SLOT (USE DEL1..3)");
          return;
        }
        for (int k = 0; delList[k] != '\0'; k++) {
          int slotIdx = delList[k] - '1';
          if (slotIdx < 0 || slotIdx > 2) {
            snprintf(reply, replySize, "ERR SLOT 1..3 ONLY");
            return;
          }
          nextSettings.schedules[relayIdx][slotIdx] = {false, 0, 0};
          settingsChanged = true;
        }
      }
      // Cu phap dai: RELAY1 ON1 10H15 OFF1 11H10
      else if (tokenCount == 5 && strncmp(tokens[1], "ON", 2) == 0 && strncmp(tokens[3], "OFF", 3) == 0) {
        int slotIdx = tokens[1][2] - '1';
        int slotIdxOff = tokens[3][3] - '1';
        uint16_t onMin = 0, offMin = 0;
        if (slotIdx < 0 || slotIdx > 2 || slotIdx != slotIdxOff ||
            !parseTimeMinute(tokens[2], onMin) || !parseTimeMinute(tokens[4], offMin) || onMin == offMin) {
          snprintf(reply, replySize, "ERR SCHEDULE TIME");
          return;
        }
        nextSettings.schedules[relayIdx][slotIdx] = {true, onMin, offMin};
        settingsChanged = true;
      }
      // Cu phap ngan: R1 1=1015-1110 2=1630-1730
      else {
        for (int n = 1; n < tokenCount; n++) {
          char *item = tokens[n];
          // Dinh dang: X=HHMM-HHMM (11 ky tu)
          if (strlen(item) != 11 || item[1] != '=' || item[6] != '-') {
            snprintf(reply, replySize, "ERR USE 1=HHMM-HHMM");
            return;
          }
          int slotIdx = item[0] - '1';
          item[6] = '\0';
          uint16_t onMin = 0, offMin = 0;
          if (slotIdx < 0 || slotIdx > 2 ||
              !parseTimeMinute(item + 2, onMin) || !parseTimeMinute(item + 7, offMin) || onMin == offMin) {
            snprintf(reply, replySize, "ERR SCHEDULE TIME");
            return;
          }
          nextSettings.schedules[relayIdx][slotIdx] = {true, onMin, offMin};
          settingsChanged = true;
        }
      }
    }
  }

  // Ap dung cai dat moi va luu EEPROM
  if (settingsChanged) {
    settings = nextSettings;
    saveSettings();
  }

  // 1. Thuc hien bat/tat tay cac relay duoc yeu cau
  for (int r = 0; r < 3; r++) {
    if (manualOutput[r] != -1) {
      setRelay(r, manualOutput[r] == 1);
    }
  }

  // 2. Neu che do la AUTO, doc RTC va cap nhat lai lich cho Relay 1 & 2
  readRtc();
  if (settings.autoMode) {
    checkSchedule();
  }

  makeStatus(reply, replySize);
}

// ==================== 9. GIAO TIEP MODULE SIM (TEXT MODE - KHONG CO SIM VAN CHAY) ====================
enum SimInitState {
  SIM_CHECK_AT,
  SIM_READY,
  SIM_OFFLINE
};

SimInitState simState = SIM_CHECK_AT;
uint32_t lastSimCheckMs = 0;
int simRetryCount = 0;

bool waitingSmsBody = false;
char senderPhone[32] = "";
char simLineBuf[200];
int simLineLen = 0;

bool isPhoneAllowed(const char *incomingPhone) {
  if (ALLOWED_PHONE[0] == '\0') return true;

  const char *p1 = ALLOWED_PHONE;
  const char *p2 = incomingPhone;
  if (strncmp(p1, "+84", 3) == 0) p1 += 3;
  else if (p1[0] == '0') p1 += 1;

  if (strncmp(p2, "+84", 3) == 0) p2 += 3;
  else if (p2[0] == '0') p2 += 1;

  return strcmp(p1, p2) == 0;
}

void sendSms(const char *phone, const char *message) {
  if (simState != SIM_READY || phone[0] == '\0') return;

  SimSerial.print("AT+CMGS=\"");
  SimSerial.print(phone);
  SimSerial.print("\"\r");

  uint32_t startWait = millis();
  bool gotPrompt = false;
  while (millis() - startWait < 2000) {
    readCurrentRelay1();
    if (SimSerial.available()) {
      char c = SimSerial.read();
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
  }

  if (gotPrompt) {
    SimSerial.print(message);
    SimSerial.write(26); // Ctrl+Z (0x1A)
    DebugSerial.println("-> Da gui SMS phan hoi");
  } else {
    SimSerial.write(27); // ESC
  }
}

void processSimLine(char *line) {
  char *s = trimString(line);
  if (*s == '\0') return;

  if (simState != SIM_READY && strcmp(s, "OK") == 0) {
    simState = SIM_READY;
    SimSerial.print("ATE0\r");
    delay(50);
    SimSerial.print("AT+CMGF=1\r");
    delay(50);
    SimSerial.print("AT+CNMI=2,2,0,0,0\r");
    DebugSerial.println("Module SIM da san sang (Text Mode)");
    return;
  }

  if (waitingSmsBody) {
    waitingSmsBody = false;
    DebugSerial.print("Nhan SMS tu ");
    DebugSerial.print(senderPhone);
    DebugSerial.print(": ");
    DebugSerial.println(s);

    if (isPhoneAllowed(senderPhone)) {
      char reply[180];
      handleCommand(s, reply, sizeof(reply));
      DebugSerial.println(reply);
      sendSms(senderPhone, reply);
    } else {
      DebugSerial.println("Bo qua SMS: So dien thoai khong duoc phep");
    }
    return;
  }

  if (strncmp(s, "+CMT:", 5) == 0) {
    simState = SIM_READY;
    char *firstQuote = strchr(s, '"');
    if (firstQuote) {
      char *secondQuote = strchr(firstQuote + 1, '"');
      if (secondQuote) {
        size_t len = secondQuote - (firstQuote + 1);
        if (len < sizeof(senderPhone)) {
          strncpy(senderPhone, firstQuote + 1, len);
          senderPhone[len] = '\0';
          waitingSmsBody = true;
        }
      }
    }
  }
}

void handleSim() {
  while (SimSerial.available()) {
    char c = SimSerial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      simLineBuf[simLineLen] = '\0';
      processSimLine(simLineBuf);
      simLineLen = 0;
    } else if (simLineLen < (int)sizeof(simLineBuf) - 1) {
      simLineBuf[simLineLen++] = c;
    }
  }

  if (simState == SIM_CHECK_AT) {
    if (millis() - lastSimCheckMs >= 2000) {
      lastSimCheckMs = millis();
      if (simRetryCount < 3) {
        SimSerial.print("AT\r");
        simRetryCount++;
      } else {
        simState = SIM_OFFLINE;
        DebugSerial.println("Khong phat hien module SIM -> Chay binh thuong (Serial & RS485)");
      }
    }
  }
}

// ==================== 10. GIAO TIEP RS485 MODBUS RTU (SLAVE ID = 1) ====================
// Tat ca thong so deu nam tren Holding Registers (Function 03 Doc, Function 06/16 Ghi):
//   Reg 0 : Che do (0 = MAN, 1 = AUTO)                    [Doc / Ghi -> luu EEPROM]
//   Reg 1 : Relay 1 (0 = OFF, 1 = ON)                     [Doc / Ghi -> luu kWh EEPROM]
//   Reg 2 : Relay 2 (0 = OFF, 1 = ON)                     [Doc / Ghi]
//   Reg 3 : Relay 3 (0 = OFF, 1 = ON)                     [Doc / Ghi]
//   Reg 4 : Dong dien I (don vi 0.01 A, VD: 25 = 0.25A)   [Chi doc]
//   Reg 5 : Cong suat P (don vi 1 W, VD: 55 = 55W)        [Chi doc]
//   Reg 6 : Dien nang E (kWh) - 16 bit cao (don vi Wh)    [Doc / Ghi -> luu EEPROM]
//   Reg 7 : Dien nang E (kWh) - 16 bit thap (VD: 1234 = 1.234 kWh) [Doc / Ghi -> luu EEPROM]
//   Reg 8 : Gio DS3231 (0..23)                            [Doc / Ghi -> luu DS3231]
//   Reg 9 : Phut DS3231 (0..59)                           [Doc / Ghi -> luu DS3231]
//   Reg 10: Giay DS3231 (0..59)                           [Doc / Ghi -> luu DS3231]
//   Reg 11: Ngay DS3231 (1..31)                           [Doc / Ghi -> luu DS3231]
//   Reg 12: Thang DS3231 (1..12)                          [Doc / Ghi -> luu DS3231]
//   Reg 13: Nam DS3231 (2000..2099 hoac 0..99)            [Doc / Ghi -> luu DS3231]
//   Reg 14..19: Lich Relay 1 (ON1, OFF1, ON2, OFF2, ON3, OFF3) dang HHMM (VD: 1015 = 10h15, 0 = tat)
//   Reg 20..25: Lich Relay 2 (ON1, OFF1, ON2, OFF2, ON3, OFF3) dang HHMM
//   Reg 26..31: Lich Relay 3 (ON1, OFF1, ON2, OFF2, ON3, OFF3) dang HHMM
enum HoldingRegMap {
  REG_MODE       = 0,
  REG_RELAY1     = 1,
  REG_RELAY2     = 2,
  REG_RELAY3     = 3,
  REG_CURRENT    = 4,
  REG_POWER      = 5,
  REG_ENERGY_HI  = 6,
  REG_ENERGY_LO  = 7,
  REG_RTC_HOUR   = 8,
  REG_RTC_MIN    = 9,
  REG_RTC_SEC    = 10,
  REG_RTC_DAY    = 11,
  REG_RTC_MONTH  = 12,
  REG_RTC_YEAR   = 13,
  REG_SCHED_BASE = 14, // 18 thanh ghi (14..31) cho 3 relay x 3 moc (ON, OFF)
  NUM_HOLDING_REGS = 128
};

uint16_t holdingRegs[NUM_HOLDING_REGS] = {};

// Tinh CRC16 Modbus RTU chuan (Poly 0xA001, Init 0xFFFF)
uint16_t modbusCrc16(const uint8_t *buf, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// Chuyen so phut trong ngay (0..1439) sang dang HHMM (VD: 615 -> 1015)
uint16_t minuteToHhmm(uint16_t totalMinutes) {
  return (totalMinutes / 60) * 100 + (totalMinutes % 60);
}

// Chuyen dang HHMM (VD: 1015) sang so phut trong ngay (615)
bool hhmmToMinute(uint16_t hhmm, uint16_t &outMinute) {
  uint16_t h = hhmm / 100;
  uint16_t m = hhmm % 100;
  if (h > 23 || m > 59) return false;
  outMinute = h * 60 + m;
  return true;
}

// Cap nhat du lieu tu bien he thong len bang thanh ghi Holding Registers
void updateHoldingRegsFromSystem() {
  holdingRegs[REG_MODE]    = settings.autoMode ? 1 : 0;
  holdingRegs[REG_RELAY1]  = relayOn[0] ? 1 : 0;
  holdingRegs[REG_RELAY2]  = relayOn[1] ? 1 : 0;
  holdingRegs[REG_RELAY3]  = relayOn[2] ? 1 : 0;

  holdingRegs[REG_CURRENT] = (uint16_t)(currentAmp * 100.0f + 0.5f); // 0.01 A
  holdingRegs[REG_POWER]   = (uint16_t)(powerWatt + 0.5f);           // 1 W

  uint32_t whTotal = (uint32_t)(energyKwh * 1000.0 + 0.5);           // 0.001 kWh (1 Wh)
  holdingRegs[REG_ENERGY_HI] = (uint16_t)(whTotal >> 16);
  holdingRegs[REG_ENERGY_LO] = (uint16_t)(whTotal & 0xFFFF);

  holdingRegs[REG_RTC_HOUR]  = rtc.hour;
  holdingRegs[REG_RTC_MIN]   = rtc.minute;
  holdingRegs[REG_RTC_SEC]   = rtc.second;
  holdingRegs[REG_RTC_DAY]   = rtc.day;
  holdingRegs[REG_RTC_MONTH] = rtc.month;
  holdingRegs[REG_RTC_YEAR]  = rtc.year;

  for (int r = 0; r < 3; r++) {
    for (int i = 0; i < 3; i++) {
      int baseIdx = REG_SCHED_BASE + (r * 6) + (i * 2);
      ScheduleSlot s = settings.schedules[r][i];
      if (s.enabled) {
        holdingRegs[baseIdx]     = minuteToHhmm(s.onMinute);
        holdingRegs[baseIdx + 1] = minuteToHhmm(s.offMinute);
      } else {
        holdingRegs[baseIdx]     = 0;
        holdingRegs[baseIdx + 1] = 0;
      }
    }
  }
}

// Xu ly khi Master RS485 ghi du lieu xuong cac thanh ghi Holding
void applyHoldingRegChanges(const uint16_t oldRegs[NUM_HOLDING_REGS]) {
  bool settingsChanged = false;

  // 1. Kiem tra thay doi che do AUTO / MAN (Reg 0)
  if (holdingRegs[REG_MODE] != oldRegs[REG_MODE]) {
    settings.autoMode = (holdingRegs[REG_MODE] != 0);
    autoOverridden[0] = false;
    autoOverridden[1] = false;
    settingsChanged = true;
  }

  // 2. Kiem tra thay doi lich hen gio 3 Relay (Reg 14..31)
  for (int r = 0; r < 3; r++) {
    for (int i = 0; i < 3; i++) {
      int baseIdx = REG_SCHED_BASE + (r * 6) + (i * 2);
      if (holdingRegs[baseIdx] != oldRegs[baseIdx] ||
          holdingRegs[baseIdx + 1] != oldRegs[baseIdx + 1]) {
        uint16_t onMin = 0, offMin = 0;
        if (hhmmToMinute(holdingRegs[baseIdx], onMin) &&
            hhmmToMinute(holdingRegs[baseIdx + 1], offMin)) {
          if (onMin == offMin) {
            settings.schedules[r][i] = {false, 0, 0};
          } else {
            settings.schedules[r][i] = {true, onMin, offMin};
          }
          settingsChanged = true;
        }
      }
    }
  }

  // 3. Kiem tra thay doi Relay 1, 2, 3 (Reg 1..3)
  for (int r = 0; r < 3; r++) {
    int regIdx = REG_RELAY1 + r;
    if (holdingRegs[regIdx] != oldRegs[regIdx]) {
      bool turnOn = (holdingRegs[regIdx] != 0);

      // Chi Relay 1 & 2 moi ap dung quy tac override AUTO <-> MAN
      if (r == 0 || r == 1) {
        if (turnOn) {
          // Set len 1: Neu he thong dang o AUTO -> chuyen sang MAN va danh dau override
          if (settings.autoMode) {
            settings.autoMode = false;
            autoOverridden[r] = true;
            settingsChanged = true;
          }
        } else {
          // Set ve 0: Neu relay nay truoc do duoc override tu AUTO -> tra ve AUTO lai
          if (autoOverridden[r]) {
            autoOverridden[r] = false;
            if (!autoOverridden[0] && !autoOverridden[1]) {
              settings.autoMode = true;
              settingsChanged = true;
            }
          }
        }
      }

      setRelay(r, turnOn);
    }
  }

  // Luu EEPROM neu co thay doi che do hoac lich hen
  if (settingsChanged) {
    saveSettings();
    if (settings.autoMode) {
      checkSchedule();
    }
  }

  // 4. Kiem tra thay doi so dien kWh (Reg 6, 7)
  if (holdingRegs[REG_ENERGY_HI] != oldRegs[REG_ENERGY_HI] ||
      holdingRegs[REG_ENERGY_LO] != oldRegs[REG_ENERGY_LO]) {
    uint32_t newWh = ((uint32_t)holdingRegs[REG_ENERGY_HI] << 16) | holdingRegs[REG_ENERGY_LO];
    energyKwh = (double)newWh / 1000.0;
    saveEnergy();
  }

  // 5. Kiem tra thay doi Gio, Phut, Giay, Ngay, Thang, Nam cua DS3231 (Reg 8..13)
  bool rtcChanged = false;
  for (int idx = REG_RTC_HOUR; idx <= REG_RTC_YEAR; idx++) {
    if (holdingRegs[idx] != oldRegs[idx]) {
      rtcChanged = true;
      break;
    }
  }
  if (rtcChanged) {
    uint16_t yr = holdingRegs[REG_RTC_YEAR];
    if (yr < 100) yr += 2000;
    RtcTime newTime = {
      yr,
      (uint8_t)holdingRegs[REG_RTC_MONTH],
      (uint8_t)holdingRegs[REG_RTC_DAY],
      (uint8_t)holdingRegs[REG_RTC_HOUR],
      (uint8_t)holdingRegs[REG_RTC_MIN],
      (uint8_t)holdingRegs[REG_RTC_SEC]
    };
    if (setRtc(newTime)) {
      checkSchedule();
    }
  }
}

// Bo dem nhan khung Modbus RTU
uint8_t mbRxBuf[256];
uint16_t mbRxLen = 0;
uint32_t mbLastByteMs = 0;

void handleModbus() {
  updateHoldingRegsFromSystem();

  // 1. Doc tat ca byte co trong buffer RS485 UART
  while (RS485Serial.available()) {
    uint8_t b = RS485Serial.read();
    if (mbRxLen < sizeof(mbRxBuf)) {
      mbRxBuf[mbRxLen++] = b;
    }
    mbLastByteMs = millis();
  }

  // 2. Neu da nhan byte va cach nhau >= 15ms (khoang lang giua 2 khung truyen Modbus)
  if (mbRxLen > 0 && (millis() - mbLastByteMs >= 15)) {
    // In ra Serial Debug de giam sat (giup biet ngay co tin hieu RS485 hay khong)
    DebugSerial.print("[RS485 RX] ");
    for (uint16_t i = 0; i < mbRxLen; i++) {
      char hexStr[4];
      snprintf(hexStr, sizeof(hexStr), "%02X ", mbRxBuf[i]);
      DebugSerial.print(hexStr);
    }
    DebugSerial.println("");

    // Khung Modbus chuan toi thieu phai co 8 bytes va dung ID Slave
    if (mbRxLen >= 8 && mbRxBuf[0] == MODBUS_SLAVE_ID) {
      uint16_t calcCrc = modbusCrc16(mbRxBuf, mbRxLen - 2);
      uint16_t rxCrc = mbRxBuf[mbRxLen - 2] | ((uint16_t)mbRxBuf[mbRxLen - 1] << 8);

      if (calcCrc == rxCrc) {
        uint8_t funcCode = mbRxBuf[1];
        uint16_t startAddr = ((uint16_t)mbRxBuf[2] << 8) | mbRxBuf[3];
        uint16_t quantity = ((uint16_t)mbRxBuf[4] << 8) | mbRxBuf[5];

        // Ho tro tu dong can chinh dia chi neu Modbus Poll cau hinh 40001 / 40000 / 30001
        if (startAddr >= 40001) {
          startAddr -= 40001;
        } else if (startAddr >= 40000) {
          startAddr -= 40000;
        } else if (startAddr >= 30001) {
          startAddr -= 30001;
        }

        uint16_t snapshot[NUM_HOLDING_REGS];
        memcpy(snapshot, holdingRegs, sizeof(snapshot));

        uint8_t respBuf[256];
        uint16_t respLen = 0;

        // Function 03 (Read Holding Registers) & Function 04 (Read Input Registers)
        if (funcCode == 0x03 || funcCode == 0x04) {
          if (startAddr + quantity <= NUM_HOLDING_REGS && quantity <= 125) {
            respBuf[0] = MODBUS_SLAVE_ID;
            respBuf[1] = funcCode;
            respBuf[2] = (uint8_t)(quantity * 2);
            respLen = 3;
            for (uint16_t i = 0; i < quantity; i++) {
              uint16_t val = holdingRegs[startAddr + i];
              respBuf[respLen++] = (uint8_t)(val >> 8);
              respBuf[respLen++] = (uint8_t)(val & 0xFF);
            }
          } else {
            // Exception 02: Illegal Data Address
            respBuf[0] = MODBUS_SLAVE_ID;
            respBuf[1] = funcCode | 0x80;
            respBuf[2] = 0x02;
            respLen = 3;
          }
        }
        // Function 06: Write Single Register
        else if (funcCode == 0x06) {
          if (startAddr < NUM_HOLDING_REGS) {
            holdingRegs[startAddr] = quantity; // Trong FC 06, bytes 4..5 la gia tri can ghi
            applyHoldingRegChanges(snapshot);
            updateHoldingRegsFromSystem();

            // Echo lai 6 bytes dau
            memcpy(respBuf, mbRxBuf, 6);
            respLen = 6;
          } else {
            respBuf[0] = MODBUS_SLAVE_ID;
            respBuf[1] = 0x86;
            respBuf[2] = 0x02;
            respLen = 3;
          }
        }
        // Function 16 (0x10): Write Multiple Registers
        else if (funcCode == 0x10) {
          uint8_t byteCount = mbRxBuf[6];
          if (startAddr + quantity <= NUM_HOLDING_REGS && byteCount == quantity * 2 && mbRxLen >= (uint16_t)(7 + byteCount + 2)) {
            for (uint16_t i = 0; i < quantity; i++) {
              holdingRegs[startAddr + i] = ((uint16_t)mbRxBuf[7 + i * 2] << 8) | mbRxBuf[8 + i * 2];
            }
            applyHoldingRegChanges(snapshot);
            updateHoldingRegsFromSystem();

            // Phan hoi 6 bytes
            memcpy(respBuf, mbRxBuf, 6);
            respLen = 6;
          } else {
            respBuf[0] = MODBUS_SLAVE_ID;
            respBuf[1] = 0x90;
            respBuf[2] = 0x02;
            respLen = 3;
          }
        }
        else {
          // Exception 01: Illegal Function
          respBuf[0] = MODBUS_SLAVE_ID;
          respBuf[1] = funcCode | 0x80;
          respBuf[2] = 0x01;
          respLen = 3;
        }

        // Tinh CRC va gui phan hoi ra RS485
        if (respLen > 0) {
          uint16_t crc = modbusCrc16(respBuf, respLen);
          respBuf[respLen++] = (uint8_t)(crc & 0xFF);
          respBuf[respLen++] = (uint8_t)(crc >> 8);

          if (RS485_DE >= 0) {
            digitalWrite(RS485_DE, HIGH);
            delayMicroseconds(500); // Doi on dinh DE truoc khi truyen
          }
          RS485Serial.write(respBuf, respLen);
          RS485Serial.flush();
          delay(3); // Doi byte cuoi cung (CRC) truyen xong hoan toan khoi thanh ghi phan cung
          if (RS485_DE >= 0) {
            digitalWrite(RS485_DE, LOW);
          }

          // Xoa sach cac byte echo bi doi lai vao bo dem UART RX tren duong truyen RS485 2 day
          while (RS485Serial.available()) {
            RS485Serial.read();
          }

          DebugSerial.print("[RS485 TX] Gui ");
          DebugSerial.print(respLen);
          DebugSerial.println(" bytes OK");
        }
      } else {
        DebugSerial.println("[RS485 ERR] Sai ma CRC!");
      }
    }

    mbRxLen = 0; // Xoa buffer san sang cho khung tiep theo
  }
}

// ==================== 11. NHAN LENH QUA CONG SERIAL MONITOR ====================
void handleDebugSerial() {
  static char cmdBuf[180];
  static int cmdLen = 0;

  while (DebugSerial.available()) {
    char c = DebugSerial.read();
    if (c == '\r' || c == '\n') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        char reply[180];
        handleCommand(cmdBuf, reply, sizeof(reply));
        DebugSerial.println(reply);
        cmdLen = 0;
      }
    } else if (cmdLen < (int)sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

// ==================== 12. SETUP & LOOP ====================
void setup() {
  // Tat tat ca relay ngay khi cap nguon
  for (int r = 0; r < 3; r++) {
    digitalWrite(relayPins[r], !RELAY_ON_LEVEL);
    pinMode(relayPins[r], OUTPUT);
  }

  pinMode(LED_ACT, OUTPUT);
  digitalWrite(LED_ACT, LOW);
  pinMode(SIM_RST, INPUT);

  // Khoi tao UART Debug (9600) va UART SIM (115200)
  DebugSerial.setRx(RX1);
  DebugSerial.setTx(TX1);
  DebugSerial.begin(9600);

  SimSerial.setRx(RX2);
  SimSerial.setTx(TX2);
  SimSerial.begin(SIM_BAUD);

  // Khoi tao UART RS485 (ID = 1, 9600 baud)
  if (RS485_DE >= 0) {
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW);
  }
  RS485Serial.setRx(RS485_RX);
  RS485Serial.setTx(RS485_TX);
  RS485Serial.begin(RS485_BAUD);

  // Khoi tao I2C cho DS3231
  Wire.setSCL(PB6);
  Wire.setSDA(PB7);
  Wire.begin();
  Wire.setClock(100000);

  // Khoi tao ADC 12-bit (0..4095) cho cam bien dong ACS712
  analogReadResolution(12);
  pinMode(ACS712, INPUT_ANALOG);

  DebugSerial.println("=== HE THONG DIEU KHIEN 3 RELAY (SMS + SERIAL + RS485 MODBUS ID=1) ===");
  DebugSerial.println("Go '?' hoac 'STATUS' de xem trang thai, 'MAN;R1 ON' de bat Relay 1");

  // Doc du lieu da luu tu EEPROM
  loadSettings();
  loadEnergy();

  // Doc thoi gian DS3231 va cap nhat lich hen + thanh ghi Holding
  readRtc();
  checkSchedule();
  updateHoldingRegsFromSystem();

  windowStartMs  = millis();
  lastSimCheckMs = millis();
}

void loop() {
  // 1. Do dong dien, cong suat P va dien nang E cua Relay 1
  readCurrentRelay1();

  // 2. Xu ly module SIM (neu khong co SIM van tu dong bo qua, khong treo)
  handleSim();

  // 3. Xu ly giao tiep RS485 Modbus RTU (Slave ID = 1, Holding Registers)
  handleModbus();

  // 4. Nhan lenh tu may tinh qua cong Debug Serial
  handleDebugSerial();

  // 5. Moi 1 giay doc RTC va kiem tra lich hen AUTO
  static uint32_t lastRtcMs = 0;
  if (millis() - lastRtcMs >= 1000) {
    lastRtcMs = millis();
    readRtc();
    checkSchedule();
  }

  // 6. Nhay LED bao hieu he thong dang hoat dong (moi 200ms)
  static uint32_t lastLedMs = 0;
  if (millis() - lastLedMs >= 200) {
    lastLedMs = millis();
    digitalWrite(LED_ACT, !digitalRead(LED_ACT));
  }
}
