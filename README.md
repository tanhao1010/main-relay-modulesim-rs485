# Hệ thống điều khiển 3 Relay qua SMS / Serial (STM32F103C8 + A7680C + DS3231 + ACS712)

Code viết cho mạch **STM32F103C8T6** trên PlatformIO (Arduino framework), trình bày rõ ràng, dễ đọc, dễ hiểu và **không có module SIM vẫn chạy hoàn toàn bình thường**.

## 1. Tính năng chính

- **Không có module SIM vẫn hoạt động bình thường**:
  - Khi mới cấp nguồn, mạch thử gửi `AT` kiểm tra module SIM 3 lần (không dùng `delay()` làm đứng chương trình).
  - Nếu không gắn module SIM, mạch tự chuyển sang chế độ **Offline** (không báo lỗi lặp lại, không treo), vẫn đọc thời gian thực DS3231, đo dòng điện ACS712, tự động bật/tắt 3 Relay theo lịch hẹn `AUTO` và nhận mọi lệnh điều khiển qua cổng **Serial Monitor (baud 9600)**.
  - Nếu chưa gắn DS3231 mà gửi lệnh `TIME ...` qua Serial, mạch tự chạy đồng hồ mềm bằng `millis()` để vẫn test được chế độ hẹn giờ `AUTO`.
- **Khi có module SIM (A7680C / SIM800L / SIM900A)**:
  - Sử dụng chế độ văn bản **SMS Text Mode (`AT+CMGF=1`, `AT+CNMI=2,2,0,0,0`)** chuẩn, ngắn gọn và dễ hiểu.
- **Hai chế độ hoạt động**:
  - `AUTO`: Tự động bật/tắt 3 Relay theo lịch hẹn giờ hằng ngày (mỗi Relay có 3 mốc bật/tắt, hỗ trợ cả lịch qua đêm).
  - `MAN`: Điều khiển bật/tắt từng Relay bằng lệnh.
- **Đo dòng tải, công suất P và số điện tiêu thụ E (kWh) của Relay 1**:
  - Đọc cảm biến dòng **ACS712 5A** tại chân `PA1` mỗi 1ms, tính dòng xoay chiều hiệu dụng **I (A)**, công suất **P = 220 × I (W)** và cộng dồn điện năng **E (kWh)**.
  - Tự động lưu cài đặt lịch hẹn và số điện `E` vào **EEPROM** (chỉ lưu số điện khi Relay 1 đổi trạng thái ON/OFF để bảo vệ bộ nhớ).

## 2. Sơ đồ chân (trong `src/main.cpp`)

| Chức năng | Chân STM32 |
|---|---|
| Relay 1 / 2 / 3 | `PA7` / `PB0` / `PB1` |
| Cảm biến dòng ACS712 5A (nhánh Relay 1) | `PA1` |
| DS3231 RTC (I2C1) | `SCL = PB6`, `SDA = PB7` (địa chỉ `0x68`) |
| Debug Serial (Serial2 - 9600 baud) | `TX1 = PA2`, `RX1 = PA3` |
| Module SIM (Serial1 - 115200 baud) | `TX2 = PA9`, `RX2 = PA10`, `SIM_RST = PB8` |
| LED báo hoạt động | `PB5` |

## 3. Các lệnh điều khiển (dùng chung cho cả Serial Monitor và SMS)

Có thể gộp nhiều lệnh trên cùng 1 dòng, ngăn cách bởi dấu `;` (không phân biệt chữ hoa/thường):

```text
?
STATUS
TIME 2026-10-31 10:15:00
MAN;R1 ON;R2 OFF;R3 ON
AUTO;R1 1=1015-1110 2=1630-1730 3=1800-1900
RELAY1 ON1 10h15 OFF1 11h10
R1 DEL23
```

- `?` hoặc `STATUS`: Xem trạng thái hiện tại, lịch hẹn 3 relay và các thông số `I`, `P`, `E`.
- `TIME YYYY-MM-DD HH:MM:SS`: Cài đặt ngày giờ thực.
- `MAN` / `AUTO`: Chuyển chế độ điều khiển tay hoặc tự động theo lịch.
- `R1 ON` / `R1 OFF` (hoặc `RELAY1 ON` / `RELAY1 OFF`): Bật/tắt relay trong chế độ `MAN`.
- `R1 1=1015-1110`: Cài mốc 1 của Relay 1 bật lúc 10:15, tắt lúc 11:10.
- `R1 DEL23`: Xóa mốc 2 và mốc 3 của Relay 1.
