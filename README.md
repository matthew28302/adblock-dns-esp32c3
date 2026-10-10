# 🛡️ adblock-dns-esp32c3

**Thiết bị chặn quảng cáo toàn mạng (DNS sinkhole kiểu Pi-hole) chạy trên con chip ESP32-C3 giá ~2$.**

Trỏ DNS của router về thiết bị này là **mọi điện thoại, máy tính, smart TV trong nhà hết quảng cáo**: quảng cáo trong app (AdMob, Pangle/TikTok, Meta Audience Network, AppLovin, Unity Ads…), tracker, malware — **241.000+ tên miền** — nhưng **Facebook, TikTok, Zalo, YouTube dùng bình thường**.

Firmware native ESP-IDF (không dùng Arduino core — xem [docs/DEBUG-NOTES-VI.md](docs/DEBUG-NOTES-VI.md) để biết tại sao), dashboard tiếng Việt, cập nhật firmware + blocklist qua WiFi (OTA), không cần PSRAM.

```
truy vấn DNS ──▶ trích tên miền ──▶ FNV-1a hash 40-bit (+ tên miền cha)
              ──▶ binary search hash trong flash (index 2 cấp + cache)
                   ├─ khớp   ──▶ trả lời 0.0.0.0  (quảng cáo chết)
                   └─ không  ──▶ chuyển tiếp 1.1.1.1 (dự phòng 8.8.8.8)
```

## ✨ Tính năng

- **Chặn quảng cáo toàn mạng** ở tầng DNS — không cài app lọc quảng cáo trên từng thiết bị
- **241.760 tên miền** (build sẵn kèm repo): StevenBlack + HaGeZi Pro + AdAway + chặn DNS-over-HTTPS (app không "leo rào" qua DoH được) + danh sách curated cho **quảng cáo TikTok/Pangle, Meta Audience Network và ad-network Việt Nam** (eclick.vn, admicro.vn, zad.vn…)
- **Allowlist build-time**: facebook.com, tiktok.com, zalo.vn, youtube.com… được kiểm chứng *không bao giờ* lọt vào danh sách chặn
- **Dashboard tiếng Việt** tại `http://c3adblock.local`: donut tỉ lệ chặn, live feed "quảng cáo vừa chặn", kiểm tra tên miền, chặn/cho phép tuỳ chỉnh, cấm thiết bị, tạm dừng chặn
- **OTA**: nạp firmware + blocklist mới qua WiFi, không cần cáp; tự tải blocklist từ URL theo lịch
- **Captive portal**: cấu hình WiFi qua điện thoại, không cần nấu mật khẩu WiFi vào firmware
- **Bảo mật**: endpoint thay đổi trạng thái yêu cầu Basic Auth + header chống CSRF; đổi mật khẩu ngay trên UI
- **Fail-open**: hỏng blocklist/FS thì chuyển tiếp hết — không làm đứt mạng nhà bạn
- **Chạy được trên mạch ESP32-C3 flash 16MB XTX** (bản Arduino gốc boot-loop trên mạch này — xem [docs/DEBUG-NOTES-VI.md](docs/DEBUG-NOTES-VI.md))
- RAM dùng ~100KB, không cần PSRAM

---

## 🔩 1. Phần cứng yêu cầu — ĐỌC KỸ

### ⚠️ BẮT BUỘC: board ESP32-C3

Bản build này **chỉ chạy trên ESP32-C3** (chip RISC-V 1 nhân, Wi-Fi 2.4GHz + BLE). Đây là bản firmware **native ESP-IDF biên dịch cho target `esp32c3`** — các chip khác **không nạp được**:

| Chip | Chạy được? | Ghi chú |
|---|---|---|
| **ESP32-C3** (SuperMini, DevKitM-1, các bản clone 4MB/16MB) | ✅ Đã kiểm chứng | Đây là chip được hỗ trợ |
| ESP32 (WROOM cổ điển), ESP32-S2, S3 | ❌ | Khác target — cần port (xem mục 7) |
| ESP32-C6, C2, H2 | ❌ | Khác target — cần port |
| ESP8266 | ❌ | Khác hẳn kiến trúc |

Lý do C3-only: bảng partition, sensor nhiệt độ, nút BOOT (GPIO9), USB CDC và toàn bộ sdkconfig đều build riêng cho esp32c3. Nguyên bản Arduino của dự án gốc có hỗ trợ ESP32 cổ điển, nhưng bản IDF này được tối ưu và kiểm chứng cho C3.

**Board nào nên mua:** ESP32-C3 SuperMini (~2$, nhỏ bằng ngón tay, cắm thẳng vào cổng USB router) hoặc ESP32-C3 DevKitM-1. Flash **tối thiểu 4MB** — cả 4MB lẫn 16MB đều chạy được.

### Yêu cầu khác

- **Wi-Fi 2.4GHz** (C3 không bắt được 5GHz — nếu router phát chung SSID 2 băng thì vẫn ổn)
- **Nguồn USB ổn định** ≥ 400mA: cổng USB router, sạc điện thoại đều được. Cáp sạc rẻ/cáp chỉ nguồn có thể làm sóng Wi-Fi yếu
- **Máy tính để cài đặt** (chỉ 1 lần): Windows 10/11, macOS hoặc Linux, có **Python 3.9+**, **cáp USB DATA** (không phải cáp chỉ sạc)

---

## 🚀 2. Cài đặt (thiết bị mới)

### Cách 1 — script (khuyên dùng)

```powershell
git clone https://github.com/matthew28302/adblock-dns-esp32c3.git
cd adblock-dns-esp32c3
powershell -ExecutionPolicy Bypass -File tools\flash.ps1            # tự dò cổng
# hoặc chỉ định cổng:
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Port COM6
```

macOS/Linux: `bash tools/flash.sh` (hoặc `bash tools/flash.sh /dev/ttyACM0`)

Script tự cài PlatformIO + esptool nếu thiếu, build firmware, nạp firmware + blocklist, dọn WL header, rồi in hướng dẫn tiếp theo. Lần build đầu tải toolchain (~10 phút), các lần sau ~2 phút.

### Cách 2 — thủ công

```powershell
pip install -U platformio esptool
pio run -e c3 -t upload --upload-port COM6        # 1. firmware
pio run -e c3 -t uploadfs --upload-port COM6       # 2. blocklist (data/blocklist.bin)
esptool --port COM6 erase-region 0x2B0000 0x1000   # 3. BẮT BUỘC: làm mới WL header
```

> Bước 3 không được bỏ: partition storage dùng FAT + wear-leveling, WL header nằm ở 4KB đầu (0x2B0000). Bỏ qua bước này có thể khiến thiết bị format lại partition và mất blocklist. (`flash.ps1` tự làm.)

### Sau khi nạp — cấu hình WiFi (1 lần)

1. Thiết bị mở điểm phát **`C3-AdBlock-XXXX`** (mạng mở). Không thấy? Cắm lại thiết bị và **giữ nút BOOT** khi cắm điện để xoá WiFi cũ.
2. Nối điện thoại/máy tính vào mạng đó → trang cấu hình tự hiện ra → chọn WiFi nhà bạn + nhập mật khẩu → thiết bị lưu lại và khởi động vào WiFi.
3. Mở **http://c3adblock.local** (hoặc IP thiết bị — xem trong danh sách client của router, hostname `c3adblock`). Đăng nhập **admin / admin**.
4. ⚠️ **Vào mục "Cập nhật → Đổi mật khẩu" và đổi mật khẩu NGAY** (mật khẩu web + mật khẩu OTA).

---

## 📡 3. Bật chặn toàn mạng — trỏ DNS về thiết bị

Đặt DNS = IP của thiết bị (ví dụ `192.168.104.5`). Hai cách:

**Cả nhà luôn được chặn (khuyên dùng):** vào trang quản trị router (thường `192.168.1.1` / `192.168.0.1` / `192.168.104.1`) → **DHCP / LAN settings** → đặt *DNS server* = IP thiết bị → các thiết bị kết nối lại WiFi là xong.

**Từng thiết bị (nhanh, không cần đụng router):**
- **iPhone:** Cài đặt → Wi-Fi → (i) của mạng → Cấu hình DNS → Thủ công → thêm IP thiết bị
- **Android:** Cài đặt → Mạng → Wi-Fi → Sửa mạng → Nâng cao → DNS riêng (Android 9+)
- **Windows:** Cài đặt → Mạng → Wi-Fi → Gán DNS thủ công

**Kiểm tra:**

```
nslookup doubleclick.net 192.168.104.5   → 0.0.0.0     (bị chặn ✓)
nslookup ads.tiktok.com   192.168.104.5   → 0.0.0.0     (ads TikTok bị chặn ✓)
nslookup facebook.com    192.168.104.5   → IP thật     (Facebook hoạt động ✓)
nslookup tiktok.com       192.168.104.5   → IP thật     (TikTok hoạt động ✓)
```

> 💡 iPhone đang bật **iCloud Private Relay** sẽ thấy nó bị tắt — đó là chủ đích: Private Relay chính là đường "leo rào" qua DNS chặn quảng cáo.

---

## 🖥️ 4. Sử dụng dashboard

Mở **http://c3adblock.local** (Windows 10+, iOS, Android 12+; Android cũ dùng IP).

| Tab / khu vực | Chức năng |
|---|---|
| **Donut + thẻ thống kê** | Tổng lượt chặn/cho phép, số tên miền, WiFi, nhiệt độ, RAM, uptime |
| **Quảng cáo vừa chặn** | 24 truy vấn bị chặn gần nhất + thiết bị nào bị chặn (live, 3s/lượt) |
| **Tạm dừng** | Tắt chặn 30s–2h hoặc cho đến khi bật lại (khi app/web nào bị lỗi do chặn nhầm) |
| **Thiết bị** | Danh sách client trong mạng + số lượt chặn/cho phép + **Cấm** (sinkhole toàn bộ DNS của thiết bị đó) |
| **Kiểm tra tên miền** | Nhập domain xem có bị chặn hay được cho phép (không cần đăng nhập) |
| **Chặn & cho phép** | Thêm/gỡ chặn tuỳ chỉnh; **Cho phép** ghi đè danh sách chặn cho domain + subdomain — dùng khi chặn nhầm |
| **Cập nhật** | Tải lên blocklist mới (`blocklist.bin`), tự tải blocklist từ URL theo lịch, **nạp firmware OTA** (`firmware.bin`), **đổi mật khẩu**, quên WiFi |

Bảo mật: mọi thao tác thay đổi yêu cầu đăng nhập (Basic Auth) + header chống CSRF tự động từ trang — không lo trang web lạ "mượn tay" trình duyệt đổi cấu hình thiết bị.

**Khôi phục / đổi WiFi:** bấm *Quên WiFi* trên dashboard, hoặc giữ nút **BOOT** trên mạch khi cắm điện.

---

## 🔧 5. Tuỳ biến blocklist

Danh sách chặn là file hash nhị phân `data/blocklist.bin` (mỗi tên miền = 5 byte hash FNV-1a 40-bit, sắp xếp sẵn) — mỗi partition 1.375MB chứa được **~275.000 tên miền**.

```powershell
# dựng lại từ các nguồn (có sẵn trong tools/build_blocklist.py):
python tools/build_blocklist.py data/blocklist.bin
python tools/verify_blocklist.py data/blocklist.bin     # phải in OK / OK rồi mới dùng

# nạp: hoặc qua cáp (uploadfs + erase-region như mục 2),
# hoặc KHÔNG cần cáp: upload file qua dashboard → tab Cập nhật
```

- **Thêm nguồn:** sửa `tools/build_blocklist.py` (chấp nhận hosts / plain list / cú pháp AdGuard `||domain^`; rule `@@||domain^` = cho phép)
- **Chặn thêm ads social/VIỆT NAM:** sửa `tools/social-ads-extra.txt` (đã có sẵn ads.tiktok.com, pangle.io, an.facebook.com, eclick.vn, admicro.vn, zad.vn…)
- **Bảo vệ nền tảng xã hội:** sửa `tools/allow-social.txt` + danh sách `MUST_ALLOW` trong `verify_blocklist.py` — bước verify bảo đảm Facebook/TikTok/Zalo không bao giờ bị chặn nhầm
- **Gợi ý nguồn lớn hơn:** GoodbyeAds (`jerryn70/GoodbyeAds/Hosts/GoodbyeAds.txt`) thêm ~250k domain — vượt partition 4MB, chỉ dùng khi mở rộng storage (xem partitions.csv)

---

## 🩺 6. Xử lý sự cố

| Hiện tượng | Nguyên nhân & cách xử lý |
|---|---|
| Nạp firmware Arduino gốc xong boot-loop `load partition table error` | Mạch dùng flash 16MB XTX mà bootloader dựng sẵn của Arduino không đọc nổi. **Dùng bản IDF này** (đã fix). Chi tiết: [docs/DEBUG-NOTES-VI.md](docs/DEBUG-NOTES-VI.md) |
| Sau `uploadfs`, dashboard báo 0 domain / blocklist mất | Quên bước `esptool erase-region 0x2B0000 0x1000` (WL header). Chạy lại bước đó, hoặc nạp blocklist qua dashboard |
| Không thấy mạng `C3-AdBlock-XXXX` | Giữ nút BOOT trên mạch khi cắm điện → portal mở lại |
| `c3adblock.local` không mở | Android <12 không hỗ trợ mDNS — dùng IP (xem router); Windows cần mạng ở profile *Private* |
| `pio` không thấy cổng | Cáp chỉ sạc (cần cáp DATA); kiểm tra Device Manager; thử `-Port COM<x>` thủ công |
| App nào đó vỡ giao diện | Có thể chặn nhầm — dashboard → Kiểm tra tên miền → thêm vào **Cho phép**; hoặc bấm Tạm dừng |
| Muốn chặn mạnh hơn/thay nguồn | Xem mục 5 |

---

## 🏗️ 7. Kiến trúc & port sang chip khác

```
src/main.c   — khởi tạo: FAT+wear-leveling, WiFi STA/portal (captive), mDNS, updater
src/dns.c    — UDP :53: parse → allow → block → sinkhole/forward (1.1.1.1 → 8.8.8.8)
src/store.c  — hash 40-bit: index 4096 mẫu + cache 512 + blocklist/custom/allow/banned
src/web.c    — http_server :80: dashboard, API, Basic Auth + CSRF, multipart OTA
src/page.h   — dashboard HTML (tiếng Việt)
```

Muốn port sang ESP32-S3/C6: đổi `board` trong `platformio.ini` + target tương ứng, chỉnh lại offset/size trong `partitions.csv` (không quá flash thật), kiểm tra GPIO BOOT + sensor nhiệt. Logic nghiệp vụ (dns/store/web) viết bằng POSIX + IDF chuẩn nên hầu như không phải sửa.

Hành trình debug flash 16MB XTX và lý do bỏ Arduino core: **[docs/DEBUG-NOTES-VI.md](docs/DEBUG-NOTES-VI.md)**.

---

## 🙏 Ghi công & License

- Fork cải tiến từ [M-Abozaid/esp32-c3-adblock](https://github.com/M-Abozaid/esp32-c3-adblock) (MIT) — ý tưởng hash 40-bit trong flash, tool dựng blocklist, UX dashboard
- Component [mdns](components/mdns) của Espressif (espressif/esp-protocols, Apache-2.0) — vendored vì PlatformIO cắt nó khỏi bundle IDF
- License: **MIT** — xem [LICENSE](LICENSE)

---

## 🇬🇧 English quick start

**Network-wide DNS ad-blocker on a $2 ESP32-C3** (native ESP-IDF, no PSRAM). Blocks 241k+ ad/tracker/malware domains including TikTok/Pangle, Meta Audience Network and Vietnamese ad networks — social platforms themselves stay usable. Vietnamese web dashboard at `http://c3adblock.local`, OTA firmware/blocklist updates, captive-portal WiFi setup.

**Requires: any ESP32-C3 board (4MB+ flash, 2.4GHz Wi-Fi), USB data cable, Python 3.9+. Other ESP32 variants are NOT supported by this build.**

```bash
git clone https://github.com/matthew28302/adblock-dns-esp32c3.git
cd adblock-dns-esp32c3
powershell -ExecutionPolicy Bypass -File tools\flash.ps1   # Windows
bash tools/flash.sh                                        # macOS/Linux
```

Then: join the `C3-AdBlock-XXXX` Wi-Fi to configure your network → open `http://c3adblock.local` → login `admin`/`admin` → **change the password** → point your router's DHCP DNS at the device. Test: `nslookup doubleclick.net <device-ip>` → `0.0.0.0`.
