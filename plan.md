# Kế hoạch đã thực thi & Danh sách lệnh điều khiển (`plan_w.md`)

## I. Các hạng mục đã thực thi trong [`src/main.cpp`](src/main.cpp)

1. **Gom gọn và viết lại theo phong cách sinh viên (dễ đọc, dễ hiểu, dễ bảo vệ đồ án)**:
   - Toàn bộ chương trình đặt gọn trong 1 file duy nhất [`src/main.cpp`](src/main.cpp), chia thành các phần rõ ràng có chú thích tiếng Việt.
   - Đã loại bỏ các file phụ phức tạp trước đó (`sms_pdu.cpp`, `record_store.cpp`, `control_logic.cpp`).
   - Giữ nguyên toàn bộ sơ đồ chân phần cứng (`RELAY1=PA7`, `RELAY2=PB0`, `RELAY3=PB1`, `ACS712=PA1`, `DS3231=PB6/PB7`, `SimSerial=PA9/PA10`, `DebugSerial=PB10/PB11`, `RS485=PA2/PA3`, `LED_ACT=PB5`).

2. **Chạy độc lập không cần module SIM (Offline Mode)**:
   - Khi khởi động, hàm `handleSim()` gửi lệnh `AT` tối đa **3 lần** (mỗi lần cách nhau 2 giây bằng `millis()`, không dùng `delay()` làm đứng vòng lặp).
   - Nếu không lắp module SIM: tự động chuyển sang trạng thái `SIM_OFFLINE`, in 1 thông báo duy nhất ra Serial và **ngừng hỏi AT** (không báo lỗi timeout lặp lại).
   - Toàn bộ chức năng đọc giờ DS3231, đo dòng ACS712, hẹn giờ tự động `AUTO`, bật/tắt bằng tay `MAN` và điều khiển qua cổng **Serial Monitor (baud 9600)** hoặc **RS485 Modbus** vẫn chạy bình thường.
   - Nếu cắm module SIM vào sau, mạch tự động nhận diện ngay khi có tín hiệu UART từ SIM.

3. **Giao tiếp SIM bằng SMS Text Mode (`AT+CMGF=1`)**:
   - Sử dụng tập lệnh AT cơ bản, dễ giải thích: `AT` -> `ATE0` -> `AT+CMGF=1` -> `AT+CNMI=2,2,0,0,0`.
   - Tự động nhận tin nhắn mới qua chuỗi `+CMT:`, xử lý lệnh và phản hồi bằng `AT+CMGS="sdt"`.
   - Cấu hình `ALLOWED_PHONE = ""` mặc định cho phép mọi số điện thoại nhắn tin điều khiển (có thể điền số cố định nếu muốn giới hạn).

4. **Đo dòng điện ACS712 5A, tính công suất P và điện năng E (nhánh Relay 1)**:
   - Lấy mẫu ADC 12-bit tại chân `PA1` mỗi `1 ms`, tính dòng xoay chiều hiệu dụng **I (RMS)** mỗi `200 ms` (10 chu kỳ lưới điện 50Hz), tự động trừ điểm 0 DC và lọc nhiễu không tải (`< 0.05A`).
   - Tính công suất ước tính `P = 220 * I` (W) và cộng dồn số điện `E` (kWh) theo thời gian thực tế.

5. **Lưu trữ cài đặt và số điện vào EEPROM (`<EEPROM.h>`)**:
   - Lưu chế độ `AUTO`/`MAN` và 9 mốc hẹn giờ (3 relay × 3 mốc) bằng `EEPROM.put()` / `EEPROM.get()` khi có thay đổi.
   - Chỉ chốt lưu số điện `E (kWh)` vào EEPROM khi **Relay 1 đổi trạng thái BẬT ↔ TẮT** để bảo vệ tuổi thọ bộ nhớ Flash/EEPROM.

6. **Đồng hồ thời gian thực DS3231 + Đồng hồ mềm dự phòng**:
   - Đọc/ghi thời gian thực qua I2C (`0x68`) bằng hàm `decToBcd` / `bcdToDec`.
   - Nếu chưa cắm module DS3231 mà gửi lệnh `TIME ...` qua Serial hoặc ghi qua RS485, mạch tự động kích hoạt đồng hồ mềm (`useSoftRtc`) đếm mỗi giây để vẫn test được lịch hẹn `AUTO`.

7. **RS485 Modbus RTU Slave (ID = 1, Baud 9600, Chân PA2 TX, PA3 RX - USART2)**:
   - Dùng toàn bộ **Holding Registers (Function 03 đọc, Function 06/16 ghi)**.
   - Tự viết bộ giải mã Modbus trực tiếp, phát hiện khoảng lặng $\ge 10\text{ ms}$, chịu được độ trễ USB của Windows, không bị timeout.
   - Tự động in log `[RS485 RX]` và `[RS485 TX]` ra cổng Debug Serial để giám sát.

8. **Logic chuyển đổi AUTO / MAN chuẩn xác cho Relay 1 & 2, Relay 3 độc lập**:
   - **Khi đang ở AUTO**:
     + Nếu Relay 1 hoặc 2 đang tắt (mức 0) mà có lệnh bật lên 1 (`ON`) $\rightarrow$ hệ thống hiểu là bật cưỡng bức tạm thời nên **tự chuyển sang `MAN`** để giữ relay luôn bật.
     + Khi người dùng xong việc, có lệnh tắt về 0 (`OFF`) $\rightarrow$ hệ thống **tự động trả về `AUTO` lại**.
   - **Khi người dùng chủ động đặt `MAN`**:
     + Bật lên 1 hay tắt về 0 **tuyệt đối không tự động chuyển chế độ** (giữ nguyên chế độ `MAN`).
     + Chỉ chuyển sang `AUTO` khi người dùng chủ động gửi lệnh `AUTO` (hoặc Modbus ghi Reg 0 = 1).
   - **Relay 3 (thuần MAN)**:
     + Bật/tắt Relay 3 hoàn toàn độc lập, không bao giờ làm thay đổi chế độ `AUTO/MAN` của hệ thống.

9. **Hệ thống 4 nút nhấn điều khiển thủ công (`PB12..PB15`)**:
   - **`BT1` (PB12)**: Chuyển đổi qua lại giữa `AUTO` và `MANUAL`:
     + Khi chuyển sang **`AUTO`**: 3 relay BẬT 1s, TẮT 1s (lặp lại **2 lần**) rồi về `AUTO`.
     + Khi chuyển sang **`MANUAL`**: 3 relay BẬT 1s, TẮT 1s (**1 lần**) rồi về `MANUAL`.
   - **`BT2` (PB13)**: Bật / Tắt **Relay 1** (hỗ trợ quy tắc override AUTO $\leftrightarrow$ MAN).
   - **`BT3` (PB14)**: Bật / Tắt **Relay 2** (hỗ trợ quy tắc override AUTO $\leftrightarrow$ MAN).
   - **`BT4` (PB15)**: Bật / Tắt **Relay 3** (MAN độc lập, không ảnh hưởng chế độ hệ thống).
   - Chạy hoàn toàn non-blocking bằng `millis()`, có chống dội phím 50ms, không làm gián đoạn Modbus / SMS.

10. **Bảo vệ chạy liên tục 24/24 & Tự phục hồi module SIM**:
   - Tự động Reset phần cứng module SIM qua chân `PB8 (SIM_RST)` khi SIM bị đơ/treo quá 3 phút.
   - Thăm dò ngầm tự kết nối lại SIM mỗi 30 giây khi ở chế độ Offline.
   - Không sử dụng bộ nhớ động (`String`/`malloc`), RAM chiếm < 2 KB / 20 KB.
   - Tránh tràn số `millis()` và bảo vệ nghiêm ngặt chống tràn bộ đệm `snprintf`.
   - Tự động chốt số điện vào EEPROM định kỳ mỗi 1 giờ khi tải chạy liên tục 24/24.

---

## II. Bảng thanh ghi RS485 Modbus RTU (Slave ID = 1)

| Địa chỉ Reg | Tên thông số | Chức năng / Đơn vị | Quyền hạn |
|:---:|---|---|:---:|
| **0** | Chế độ | `0` = MAN, `1` = AUTO | Đọc / Ghi (lưu EEPROM) |
| **1** | Relay 1 | `0` = OFF, `1` = ON (1 -> MAN, 0 -> AUTO) | Đọc / Ghi (lưu kWh EEPROM) |
| **2** | Relay 2 | `0` = OFF, `1` = ON (1 -> MAN, 0 -> AUTO) | Đọc / Ghi |
| **3** | Relay 3 | `0` = OFF, `1` = ON (Không đổi chế độ AUTO/MAN) | Đọc / Ghi |
| **4** | Dòng điện I | Đơn vị `0.01 A` (VD: `25` = 0.25 A) | Chỉ đọc |
| **5** | Công suất P | Đơn vị `1 W` (VD: `55` = 55 W) | Chỉ đọc |
| **6** | Số điện E (kWh) - Cao | 16 bit cao của tổng Wh | Đọc / Ghi (lưu EEPROM) |
| **7** | Số điện E (kWh) - Thấp | 16 bit thấp của tổng Wh (VD: `1234` = 1.234 kWh) | Đọc / Ghi (lưu EEPROM) |
| **8** | Giờ DS3231 | `0..23` | Đọc / Ghi (lưu DS3231) |
| **9** | Phút DS3231 | `0..59` | Đọc / Ghi (lưu DS3231) |
| **10** | Giây DS3231 | `0..59` | Đọc / Ghi (lưu DS3231) |
| **11** | Ngày DS3231 | `1..31` | Đọc / Ghi (lưu DS3231) |
| **12** | Tháng DS3231 | `1..12` | Đọc / Ghi (lưu DS3231) |
| **13** | Năm DS3231 | `2000..2099` (hoặc `0..99`) | Đọc / Ghi (lưu DS3231) |
| **14..19** | Lịch Relay 1 | 3 mốc (ON1, OFF1, ON2, OFF2, ON3, OFF3) dạng `HHMM` | Đọc / Ghi (lưu EEPROM) |
| **20..25** | Lịch Relay 2 | 3 mốc (ON1, OFF1, ON2, OFF2, ON3, OFF3) dạng `HHMM` | Đọc / Ghi (lưu EEPROM) |
| **26..31** | Lịch Relay 3 | 3 mốc (ON1, OFF1, ON2, OFF2, ON3, OFF3) dạng `HHMM` | Đọc / Ghi (lưu EEPROM) |
| **32** | Chu kỳ báo cáo | Số ngày gửi 1 lần (`0` = tắt, `1..365` ngày) | Đọc / Ghi (lưu EEPROM) |
| **33** | Giờ gửi báo cáo | Giờ gửi trong ngày (`0..23`, mặc định `7` = 07:00 sáng) | Đọc / Ghi (lưu EEPROM) |
| **34** | Phút gửi báo cáo | Phút gửi trong ngày (`0..59`, mặc định `0`) | Đọc / Ghi (lưu EEPROM) |
| **35** | Đếm ngược báo cáo | Số ngày còn lại đến đợt gửi báo cáo tiếp theo | Chỉ đọc |

*(Ví dụ lịch: ghi `1015` là 10h15, ghi `0` là tắt mốc).*

---

## III. Danh sách các lệnh điều khiển (dùng chung cho cả SMS và Serial Monitor 9600)

> **Lưu ý**:
> - Không phân biệt chữ HOA hay chữ thường (`man;r1 on` giống `MAN;R1 ON`).
> - Có thể **gộp nhiều lệnh trong 1 lần gửi**, ngăn cách nhau bằng dấu chấm phẩy `;`.

### 1. Nhóm lệnh xem trạng thái
| Lệnh gửi | Chức năng |
|---|---|
| `?` | Xem toàn bộ trạng thái (Chế độ, Giờ, I, P, E và 9 mốc hẹn giờ của 3 Relay) |
| `STATUS` | Tương đương lệnh `?` |
| `R1 LIST` *(hoặc `RELAY1 LIST`)* | Xem bảng trạng thái tổng hợp |

**Mẫu phản hồi trả về:**
```text
OK AUTO 31/10/26 10:16 I=0.25A P=55W E=1.234kWh
R1=1:1015-1110,1630-1730,1800-1900
R2=0:0600-0700,1200-1300,1800-1900
R3=0:0800-0900,1400-1500,2200-0600
```

### 2. Nhóm lệnh chuyển chế độ (`AUTO` / `MAN`)
| Lệnh gửi | Chức năng |
|---|---|
| `AUTO` *(hoặc `SET AUTO`)* | Chuyển sang chế độ **Tự động** chạy theo lịch hẹn giờ cho Relay 1 & 2 |
| `MAN` *(hoặc `SET MAN`)* | Chuyển sang chế độ **Bằng tay** |

### 3. Nhóm lệnh bật/tắt Relay
| Lệnh gửi | Chức năng |
|---|---|
| `R1 ON` *(hoặc `RELAY1 ON`)* | Bật Relay 1 $\rightarrow$ **tự động chuyển chế độ về `MAN`** |
| `R1 OFF` *(hoặc `RELAY1 OFF`)* | Tắt Relay 1 $\rightarrow$ **tự động chuyển chế độ về `AUTO` lại** |
| `R2 ON` | Bật Relay 2 $\rightarrow$ **tự động chuyển chế độ về `MAN`** |
| `R2 OFF` | Tắt Relay 2 $\rightarrow$ **tự động chuyển chế độ về `AUTO` lại** |
| `R3 ON` / `R3 OFF` | Bật / Tắt Relay 3 bằng tay $\rightarrow$ **hoàn toàn độc lập, không làm thay đổi chế độ `AUTO/MAN`** |

### 4. Nhóm lệnh cài đặt lịch hẹn giờ (3 Relay × 3 mốc)
| Lệnh gửi | Chức năng |
|---|---|
| `R1 1=1015-1110` | Cài Relay 1 - Mốc 1: Bật lúc `10:15`, Tắt lúc `11:10` |
| `R1 1=1015-1110 2=1630-1730 3=1800-1900` | Cài cả 3 mốc cho Relay 1 trong một lệnh |
| `R3 1=2200-0600` | Cài lịch chạy qua đêm (Bật `22:00` tối hôm trước đến `06:00` sáng hôm sau) |
| `RELAY1 ON1 10h15 OFF1 11h10` | Cú pháp dài: Cài Relay 1 - Mốc 1 từ `10:15` đến `11:10` (chấp nhận cả `10:15`) |
| `AUTO;R1 1=0700-0800;R2 1=1700-1830` | Vừa chuyển sang `AUTO`, vừa cài lịch cho Relay 1 và Relay 2 |

### 5. Nhóm lệnh xóa lịch hẹn giờ
| Lệnh gửi | Chức năng |
|---|---|
| `R1 DEL1` *(hoặc `RELAY1 DEL1`)* | Xóa mốc 1 của Relay 1 (mốc 2, 3 giữ nguyên) |
| `R1 DEL23` | Xóa mốc 2 và mốc 3 của Relay 1 |
| `R1 DEL123` | Xóa toàn bộ 3 mốc của Relay 1 |
| `R1 DEL123;R2 DEL123;R3 DEL123` | Xóa toàn bộ lịch của cả 3 Relay |

### 6. Lệnh cài đặt ngày giờ thực (DS3231 / Đồng hồ mềm)
| Lệnh gửi | Chức năng |
|---|---|
| `TIME 2026-10-31 10:15:00` | Cài đặt ngày giờ theo định dạng `TIME YYYY-MM-DD HH:MM:SS` |

### 7. Nhóm lệnh cài đặt báo cáo định kỳ (SMS / Serial)
| Lệnh gửi | Chức năng |
|---|---|
| `REPORT ?` *(hoặc `BC ?`)* | Xem cấu hình báo cáo hiện tại (Số ngày, giờ gửi, số điện thoại nhận) |
| `REPORT 1` *(hoặc `BC 1`)* | Cài đặt mỗi ngày gửi 1 lần (lúc 07:00 sáng, tự lấy SĐT người gửi SMS) |
| `REPORT 2 08:30` | Cài đặt 2 ngày gửi 1 lần vào lúc 08h30 sáng |
| `REPORT 7 07:00 0912345678` | Cài đặt 7 ngày (1 tuần) gửi 1 lần vào lúc 07:00 tới SĐT `0912345678` |
| `REPORT OFF` *(hoặc `BC OFF` / `REPORT 0`)* | Tắt tính năng gửi báo cáo định kỳ |
