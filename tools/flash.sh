#!/usr/bin/env bash
# ==============================================================================
# flash.sh — Cài đặt adblock-dns-esp32c3 lên ESP32-C3 (macOS / Linux)
#
# Cách dùng (chạy từ thư mục gốc repo):
#   bash tools/flash.sh             # tự dò cổng (/dev/ttyACM* trước, rồi /dev/ttyUSB*)
#   bash tools/flash.sh /dev/ttyACM0
#
# Tương đương flash.ps1 trên Windows: cài deps nếu thiếu, build, nạp firmware
# + blocklist, xoá WL header, in hướng dẫn tiếp theo.
# ==============================================================================
set -e
PORT="${1:-auto}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
say() { echo -e "\033[36m[flash]\033[0m $1"; }
die() { echo -e "\033[31m[flash] $1\033[0m" >&2; exit 1; }

# ---- 1) công cụ ----
say "Kiem tra cong cu..."
command -v python3 >/dev/null || die "Khong co python3 (cai Python 3.9+ truoc)."
command -v pio >/dev/null || { say "Chua co PlatformIO -> cai qua pip..."; python3 -m pip install -U platformio; }
command -v esptool >/dev/null || { say "Chua co esptool -> cai qua pip..."; python3 -m pip install -U esptool; }

# ---- 2) dò cổng ----
if [ "$PORT" = "auto" ]; then
  say "Do cong (ttyACM truoc, roi ttyUSB)..."
  PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1 || true)"
  [ -n "$PORT" ] || die "Khong tim thay cong. Cam thiet bi (cap DATA) hoac truyen vao cong vid du: bash tools/flash.sh /dev/ttyACM0"
fi
say "Dung cong: $PORT"

# ---- 3) build + nạp firmware ----
say "Build + nap firmware (lan dau build lau, ~3-8 phut)..."
pio run -e c3 -t upload --upload-port "$PORT"

# ---- 4) nạp blocklist + WL header ----
if [ -f "$REPO/data/blocklist.bin" ]; then
  say "Nap blocklist..."
  pio run -e c3 -t uploadfs --upload-port "$PORT"
  # QUAN TRONG: xoá WL header (4KB đầu partition storage @0x2B0000)
  say "Xoa WL header cua partition storage..."
  esptool --port "$PORT" erase-region 0x2B0000 0x1000
else
  say "Khong co data/blocklist.bin -> dung tools/build_blocklist.py de tao, hoac nap sau qua dashboard."
fi

# ---- 5) hướng dẫn ----
say "XONG!"
echo ""
echo "  Cac buoc tiep theo:"
echo "   1. Tim WiFi moi ten 'C3-AdBlock-XXXX' -> noi bang dien thoai/may tinh."
echo "      (Neu khong thay: cam lai va GIU nut BOOT khi cam dien.)"
echo "   2. Trinh duyet tu mo trang cau hinh -> chon WiFi nha ban + mat khau."
echo "   3. Mo http://c3adblock.local (hoac IP tren router) -> admin/admin."
echo "   4. VAO 'Cap nhat -> Doi mat khau' DOI MAT KHAU NGAY."
echo "   5. Router: DHCP DNS = IP thiet bi de ca nha duoc chan."
echo ""
echo "  Kiem tra: nslookup doubleclick.net <IP-thiet-bi>  ->  0.0.0.0"
