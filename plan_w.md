# Kế hoạch đã thực thi & Danh sách lệnh điều khiển (`plan_w.md`)

## I. Các hạng mục đã thực thi trong [`src/main.cpp`](src/main.cpp)

1. **Gom gọn và viết lại theo phong cách sinh viên (dễ đọc, dễ hiểu, dễ bảo vệ đồ án)**:
   - Toàn bộ chương trình đặt gọn trong 1 file duy nhất [`src/main.cpp`](src/main.cpp), chia thành 11 phần rõ ràng có chú thích tiếng Việt.
   - Đã loại bỏ các file phụ phức tạp trước đó (`sms_pdu.cpp`, `record_store.cpp`, `control_logic.cpp`).
   - Giữ nguyên toàn bộ sơ đồ chân phần cứng (`RELAY1=PA7`, `RELAY2=PB0`, `RELAY3=PB1`, `ACS712=PA1`, `DS3231=PB6/PB7`, `SimSerial=PA9/PA10`, `DebugSerial=PA2/PA3`, `LED_ACT=PB5`).

2. **Chạy độc lập không cần module SIM & Tự phục hồi 24/24 (Self-Healing)**:
   - Khi khởi động, hàm `handleSim()` gửi lệnh `AT` tối đa **3 lần** (mỗi lần cách nhau 2 giây bằng `millis()`, không dùng `delay()` làm đứng vòng lặp).
   - Nếu không lắp module SIM: tự động chuyển sang `SIM_OFFLINE`, in 1 thông báo duy nhất và chuyển sang chu kỳ thăm dò ngầm 30 giây (khi người dùng cắm SIM vào sau, mạch tự nhận diện và khởi tạo lại ngay mà không cần reset MCU).
   - **Cơ chế tự phục hồi khi SIM bị đơ/treo**: Khi đang hoạt động (`SIM_READY`), mỗi 60 giây gửi nhịp Heartbeat kiểm tra. Nếu SIM bị treo/mất kết nối 3 lần liên tiếp (3 phút), mạch **tự động kích hoạt chân Reset phần cứng `PB8 (SIM_RST)`** kéo LOW 200ms để khởi động lại module SIM và tự cấu hình lại `AT+CMGF=1`, `AT+CNMI=2,2,0,0,0`.
   - Toàn bộ chức năng đo dòng ACS712, hẹn giờ `AUTO`, nút nhấn và `RS485 Modbus` hoàn toàn không bị ảnh hưởng.

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
   - Nếu chưa cắm module DS3231 mà gửi lệnh `TIME ...` qua Serial, mạch tự động kích hoạt đồng hồ mềm (`useSoftRtc`) đếm mỗi giây để vẫn test được lịch hẹn `AUTO`.

8. **Logic chuyển đổi AUTO / MAN chuẩn xác cho Relay 1 & 2, Relay 3 độc lập**:
   - **Khi đang ở AUTO**:
     + Nếu Relay 1 hoặc 2 đang tắt (mức 0) mà có lệnh bật lên 1 (`ON`) $\rightarrow$ hệ thống hiểu là bật cưỡng bức tạm thời nên **tự chuyển sang `MAN`** để giữ relay luôn bật.
     + Khi người dùng xong việc, có lệnh tắt về 0 (`OFF`) $\rightarrow$ hệ thống **tự động trả về `AUTO` lại**.
   - **Khi người dùng chủ động đặt `MAN`**:
     + Bật lên 1 hay tắt về 0 **tuyệt đối không tự động chuyển chế độ** (giữ nguyên chế độ `MAN`).
     + Chỉ chuyển sang `AUTO` khi người dùng chủ động gửi lệnh `AUTO` (hoặc Modbus ghi Reg 0 = 1).
   - **Relay 3 (thuần MAN)**:
     + Bật/tắt Relay 3 hoàn toàn độc lập, không bao giờ làm thay đổi chế độ `AUTO/MAN` của hệ thống.

9. **Hoán đổi chân phần cứng Debug và RS485**:
   - **RS485 (`Serial2`)**: `TX = PA2`, `RX = PA3` (USART2).
   - **Debug (`Serial3`)**: `TX = PB10`, `RX = PB11` (USART3).
   - Bộ giải mã Modbus RTU Slave chuẩn trực tiếp (chịu được độ trễ USB của Windows, không bị timeout, tự động in log `[RS485 RX]` và `[RS485 TX]` ra cổng Debug).

10. **Hệ thống 4 nút nhấn điều khiển thủ công (`PB12..PB15`)**:
   - **`BT1` (PB12)**: Chuyển đổi chế độ `AUTO` $\leftrightarrow$ `MANUAL` với hiệu ứng báo hiệu 3 Relay (chạy bằng máy trạng thái non-blocking `millis()`, không làm đứng hệ thống):
     + Khi chuyển sang **`AUTO`**: Cả 3 relay cùng BẬT 1s, TẮT 1s (lặp lại **2 lần**) rồi về `AUTO`.
     + Khi chuyển sang **`MANUAL`**: Cả 3 relay cùng BẬT 1s, TẮT 1s (**1 lần**) rồi về `MANUAL`.
   - **`BT2` (PB13)**: Bật / Tắt **Relay 1** (nếu đang ở AUTO, bật lên sẽ tạm chuyển MAN, khi tắt về sẽ tự động trả lại AUTO).
   - **`BT3` (PB14)**: Bật / Tắt **Relay 2** (áp dụng quy tắc chuyển AUTO/MAN như Relay 1).
   - **`BT4` (PB15)**: Bật / Tắt **Relay 3** (thuần MAN độc lập, không ảnh hưởng đến chế độ hệ thống).
   - Đã tích hợp chống dội phím (debounce) 50ms và kích hoạt ở cạnh xuống (khi nhấn giữ không bị nhảy liên tục).

11. **Bảo vệ chạy liên tục 24/24 (Không tràn RAM, không treo, không rụng kết nối)**:
   - **Không dùng bộ nhớ động (`malloc`/`free`/`String`)**: Sử dụng toàn bộ bộ đệm tĩnh cố định (`char[]`), tổng RAM chiếm chưa đến 2 KB trên tổng số 20 KB của STM32F103 (dưới 10% RAM, an toàn tuyệt đối).
   - **Bảo vệ tràn bộ đệm chuỗi (`snprintf`)**: Hàm `makeStatus` được bọc giới hạn chặt chẽ `pos < maxLen - 1`, triệt tiêu hoàn toàn nguy cơ ghi đè bộ nhớ khi chạy lâu ngày.
   - **An toàn tràn số `millis()`**: Toàn bộ các phép đo thời gian đều sử dụng phép trừ số nguyên không dấu `millis() - lastTime >= interval`, hoạt động chính xác vĩnh viễn kể cả khi `millis()` tràn số sau 49.7 ngày.
   - **Tự động sao lưu điện năng định kỳ**: Ngoài việc chốt số điện khi Relay 1 đổi trạng thái, hệ thống có cơ chế tự động sao lưu số điện vào EEPROM định kỳ mỗi 1 giờ (nếu có tiêu thụ $\ge 0.05\text{ kWh}$) để chống mất dữ liệu khi cúp điện lúc relay chạy liên tục 24/24.

12. **Tính năng Chuyển tiếp tin nhắn SMS (SMS Forwarding)**:
   - Bất kỳ tin nhắn nào nhận được từ module SIM mà **không phải là lệnh điều khiển** (ví dụ: mã OTP ngân hàng, tin nhắn thông báo cước mạng 191/Viettel/Mobi, tin nhắn người lạ...) sẽ **tự động chuyển tiếp (Forward)** nguyên văn đến số điện thoại đã được cài đặt (`settings.forwardPhone`).
   - Mẫu tin nhắn chuyển tiếp: `[FW <SDT_người_gửi>]: <Nội dung tin nhắn gốc>`.
   - Các tin nhắn lệnh điều khiển (`?`, `STATUS`, `AUTO`, `MAN`, `R1..R3`, `TIME`, `REPORT`, `FORWARD`...) vẫn được bộ lọc nhận diện và thực thi lệnh bình thường, phản hồi cho người gửi mà không bị chuyển tiếp nhầm.
   - Hỗ trợ bật/tắt, tra cứu hoặc đổi số điện thoại nhận qua SMS hoặc Serial Monitor (`FW <sdt>`, `FW ?`, `FW OFF`), tự động lưu vào EEPROM.

---

## II. Bảng thanh ghi RS485 Modbus RTU (Slave ID = 1)

| Địa chỉ Reg | Tên thông số | Chức năng / Đơn vị | Quyền hạn |
|:---:|---|---|:---:|
| **0** | Chế độ | `0` = MAN, `1` = AUTO | Đọc / Ghi (lưu EEPROM) |
| **1** | Relay 1 | `0` = OFF, `1` = ON | Đọc / Ghi (lưu kWh EEPROM) |
| **2** | Relay 2 | `0` = OFF, `1` = ON | Đọc / Ghi |
| **3** | Relay 3 | `0` = OFF, `1` = ON | Đọc / Ghi |
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
| `AUTO` *(hoặc `SET AUTO`)* | Chuyển sang chế độ **Tự động** chạy theo lịch hẹn giờ |
| `MAN` *(hoặc `SET MAN`)* | Chuyển sang chế độ **Bằng tay** (không tự bật/tắt theo lịch) |

### 3. Nhóm lệnh bật/tắt Relay bằng tay (chỉ dùng trong chế độ `MAN`)
| Lệnh gửi | Chức năng |
|---|---|
| `R1 ON` *(hoặc `RELAY1 ON`)* | Bật Relay 1 |
| `R1 OFF` *(hoặc `RELAY1 OFF`)* | Tắt Relay 1 |
| `R2 ON` / `R2 OFF` | Bật / Tắt Relay 2 |
| `R3 ON` / `R3 OFF` | Bật / Tắt Relay 3 |
| `MAN;R1 ON;R2 OFF;R3 ON` | Chuyển sang `MAN`, đồng thời bật R1, tắt R2, bật R3 |

*(Nếu đang ở chế độ `AUTO` mà gửi `R1 ON`, mạch sẽ nhắc `ERR AUTO MODE: SEND MAN FIRST`).*

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

### 8. Nhóm lệnh cài đặt chuyển tiếp tin nhắn SMS (SMS Forward)
| Lệnh gửi | Chức năng |
|---|---|
| `FW ?` *(hoặc `FORWARD ?`)* | Xem số điện thoại đang nhận tin nhắn chuyển tiếp (hoặc báo `OFF` nếu chưa bật) |
| `FW 0912345678` *(hoặc `FORWARD 0912345678`)* | Cài đặt SĐT nhận chuyển tiếp là `0912345678` (lưu vào EEPROM) |
| `FW` *(hoặc `FORWARD` gửi qua SMS)* | Bật chuyển tiếp về chính số điện thoại đang gửi tin nhắn |
| `FW OFF` *(hoặc `FORWARD OFF` / `FW 0`)* | Tắt tính năng chuyển tiếp tin nhắn |

