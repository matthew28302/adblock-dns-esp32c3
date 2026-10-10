# Ghi chú kỹ thuật — vì sao bản này là native ESP-IDF

> Nhật ký debug khi đem firmware lên mạch ESP32-C3 flash 16MB (JEDEC `5E 40 18`
> — chip XTX). Lưu lại cho ai mua phải mạch giống vậy, hoặc tò mò tại sao
> repo này không dùng Arduino core.

## Triệu chứng

Mạch ESP32-C3 rev v0.4, flash 16MB, USB CDC native (VID 303A:1001). Nạp firmware
Arduino (bản gốc của dự án upstream lẫn bản build lại) → **boot-loop**:

```
ESP-ROM:esp32c3-api1-20210207
rst:0x3 (RTC_SW_SYS_RST)
Saved PC:0x403cf94c
load:0x403cc710 ... entry 0x403cc710
(…) lặp lại ~30 lần/giây, không in được dòng nào của bootloader
```

## Chẩn đoán từng bước

1. **Phân giải địa chỉ crash**: `riscv32-esp-elf-addr2line -e bootloader.elf
   0x403cf94c` → `bootloader_reset` (bootloader_utility.c). Bootloader chết
   trước khi in dòng nào; địa chỉ này **không nằm trong app** → loại bỏ code app.
2. **A/B với firmware dựng sẵn của tác giả** (docs/ trong repo upstream, từng
   chạy thật trên mạch khác) → **vẫn loop**. Chết cả khi nạp đúng từng byte.
3. **Thử mọi tổ hợp SPI**: DIO/DOUT/QIO × 40/80MHz, partition 16MB và 4MB gốc,
   platform espressif32 bản 7.1.3 (Arduino 3.x) lẫn 6.8.1 (Arduino 2.0.x) → như nhau.
4. **Nghi phạm flash cache/MMU**: ROM và esptool **đọc flash chuẩn** (read-back
   khớp 100% khi verify), nhưng bootloader — vốn đọc qua đường cache-mapped —
   thấy toàn `0xFF`. Nghĩa là đường "đọc qua cache" hỏng riêng với chip flash này.
5. **Bootloader IDF build từ source** (esp-idf 6.1, có log đầy đủ) → **chạy ngay**,
   đọc partition chuẩn. Kết luận: khối bootloader dựng sẵn 2024 của Arduino
   không tương thích flash XTX 16MB.
6. **Nhưng app Arduino vẫn hỏng tiếp**: app (IDF 5.x bên trong core) đọc mọi
   partition ra `0xFF` — NVS, app0, storage toàn `ff ff ff ff` dù flash ghi đúng.
   Test NVS erase/write/read trong app Arduino: write báo OK, read về 0xFF.
7. **Điểm danh app native IDF 6.1**: test cùng thứ tự — NVS init/set/get **MATCH**,
   partition erase/write/read **MATCH** → Arduino core (stack flash IDF 5.x + bootloader
   dựng sẵn) không cứu được trên mạch này.

## Quyết định

Port firmware sang **native ESP-IDF 6.1** hoàn toàn:

- `dns.c` chuyển từ `WiFiUDP` sang socket BSD chuẩn (select + recvfrom)
- `store.c` đọc blocklist qua `stdio` trên FAT + wear-leveling thay LittleFS
  (component LittleFS không kèm sẵn trong bundle PlatformIO)
- `web.c` chuyển từ `WebServer` sang `esp_http_server` — tự viết streaming multipart
  cho upload blocklist/firmware OTA, tự viết Basic Auth + CSRF
- WiFi: `esp_wifi` STA + captive portal AP tự quản lý
- Bootloader + app đều biên dịch từ source → không phụ thuộc binary dựng sẵn

Tất cả hành vi của bản gốc được giữ: allowlist ghi đè chặn, fail-open khi lỗi FS,
pausing, ban client, log truy vấn bị chặn, đổi upstream 1.1.1.1 → 8.8.8.8.

## Kết quả

| Kiểm tra | Kết quả |
|---|---|
| Boot | OK, log đầy đủ |
| NVS + partition I/O | MATCH 100% |
| FAT + wear-leveling | mount OK, blocklist 241.760 hash đọc chuẩn |
| DNS chặn/cho phép | doubleclick.net → 0.0.0.0; facebook.com → IP thật |
| OTA qua dashboard | kiểm chứng 2 lần (~8–12s cho firmware 1.2MB) |
| mDNS | `c3adblock.local` ping + HTTP OK |

## Ghi chú phụ

- **PlatformIO cắt component `mdns`** khỏi bundle framework-espidf → đã vendor
  component mdns chính thức của `espressif/esp-protocols` vào `components/mdns`
  (tải lại bằng `python tools/vendor_mdns.py`). `src/CMakeLists.txt` khai báo
  `PRIV_REQUIRES mdns`.
- **Sau `uploadfs` phải xoá WL header**: `esptool erase-region 0x2B0000 0x1000`.
  WL header (4KB đầu partition storage) không nằm trong ảnh FAT do pio ghi;
  để header cũ/tạp sẽ khiến mount fail và format mất blocklist. `flash.ps1`/`flash.sh` tự làm.
- Dấu vết cũ của bug này vẫn thấy trên Windows: `esptool image-info` báo bootloader
  4MB/DIO/80m nhưng bootloader vẫn chết — đừng tin header, hãy A/B bằng bootloader
  build từ source nếu gặp loop tương tự.
