# ==============================================================================
# flash.ps1 — Cài đặt adblock-dns-esp32c3 lên thiết bị ESP32-C3 (Windows 10/11)
#
# Cách dùng (PowerShell, chạy từ thư mục gốc của repo):
#   powershell -ExecutionPolicy Bypass -File tools\flash.ps1            # tự dò cổng
#   powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Port COM6  # chỉ định cổng
#
# Script sẽ:
#   1. Kiểm tra/cài Python + PlatformIO + esptool (nếu thiếu)
#   2. Build firmware từ source (ESP-IDF, có thể mất vài phút lần đầu)
#   3. Nạp firmware + blocklist (data/blocklist.bin) vào thiết bị
#   4. Xoá 4KB WL header của partition storage (bắt buộc sau uploadfs)
#   5. Hướng dẫn các bước tiếp theo (cấu hình WiFi qua cổng portal)
# ==============================================================================
param(
    [string]$Port = "auto",   # "auto" hoặc "COM6", "COM11"...
    [switch]$SkipBlocklist    # bỏ qua nạp blocklist (chỉ firmware)
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Say($msg, $color = "Cyan") { Write-Host "[flash] $msg" -ForegroundColor $color }

# ---- 1) kiểm tra công cụ ----
Say "Kiem tra cong cu..."
try { python --version *> $null } catch {
    Say "KHONG tim thay Python. Cai Python 3.9+ tu https://python.org roi chay lai." "Red"
    exit 1
}
if (-not (Get-Command pio -ErrorAction SilentlyContinue) -and
    -not (Get-Command platformio -ErrorAction SilentlyContinue)) {
    Say "Chua co PlatformIO -> cai qua pip (vai phut)..."
    pip install -U platformio
    if ($LASTEXITCODE -ne 0) { Say "Cai PlatformIO that bai." "Red"; exit 1 }
}
if (-not (Get-Command esptool -ErrorAction SilentlyContinue)) {
    Say "Chua co esptool -> cai qua pip..."
    pip install -U esptool
    if ($LASTEXITCODE -ne 0) { Say "Cai esptool that bai." "Red"; exit 1 }
}

# ---- 2) dò cổng COM của ESP32-C3 (USB VID 303A = Espressif) ----
if ($Port -eq "auto") {
    Say "Do cong COM (tim USB ESP32 - VID 303A)..."
    $cands = Get-CimInstance Win32_SerialPort |
             Where-Object { $_.PNPDeviceID -like '*VID_303A*' } |
             Select-Object -ExpandProperty DeviceID
    if ($cands) { $Port = @($cands)[0] }
    if (-not $Port -or $Port -eq "auto") {
        Say "Khong tu do duoc cong ESP32." "Yellow"
        Say "  - Kiem tra cap USB (phai la cap DATA, khong phai cap sac)."
        Say "  - Xem cong trong Device Manager, roi chay lai voi: -Port COM<x>"
        Say "  - Neu board chi co UART thuong (khong co USB natlen): giu nut BOOT khi cam, dung cong COM tu USB-UART."
        exit 1
    }
}
Say "Dung cong: $Port"

# ---- 3) build + nạp firmware ----
Say "Build + nap firmware (lan dau build lau, ~3-8 phut tai toolchain)..."
pio run -e c3 -t upload --upload-port $Port
if ($LASTEXITCODE -ne 0) { Say "Nap firmware that bai." "Red"; exit 1 }

# ---- 4) nạp blocklist ----
if (-not $SkipBlocklist) {
    if (-not (Test-Path "$Repo\data\blocklist.bin")) {
        Say "Khong co data\blocklist.bin -> dung tools\build_blocklist.py de tao, hoac nap sau qua dashboard." "Yellow"
    } else {
        Say "Nap blocklist (data\blocklist.bin)..."
        pio run -e c3 -t uploadfs --upload-port $Port
        if ($LASTEXITCODE -ne 0) { Say "Nap blocklist that bai." "Red"; exit 1 }
        # QUAN TRONG: xoa WL header 4KB dau partition storage (0x2B0000) de
        # wear-levelling khoi tao lai sach se roi doc dung anh FAT vua nap.
        Say "Xoa WL header cua partition storage..."
        esptool --port $Port erase-region 0x2B0000 0x1000
        if ($LASTEXITCODE -ne 0) { Say "erase-region that bai - chay tay: esptool --port $Port erase-region 0x2B0000 0x1000" "Red"; exit 1 }
    }
}

# ---- 5) hướng dẫn ----
Say "XONG! Thiet bi da duoc nap." "Green"
Write-Host ""
Write-Host "  Cac buoc tiep theo:" -ForegroundColor White
Write-Host "   1. Tim mang WiFi moi ten 'C3-AdBlock-XXXX' -> noi bang dien thoai/may tinh." -ForegroundColor Gray
Write-Host "      (Neu khong thay: cam lai thiet bi va GIU nut BOOT khi cam dien.)" -ForegroundColor DarkGray
Write-Host "   2. Trinh duyet tu mo trang cau hinh -> chon WiFi nha ban + nhap mat khau." -ForegroundColor Gray
Write-Host "   3. Sau khi thiet bi khoi dong lai: mo http://c3adblock.local (hoac IP tren router)." -ForegroundColor Gray
Write-Host "   4. Dang nhap admin/admin -> VAO MUC 'Cap nhat -> Doi mat khau' DOI MAT KHAU NGAY." -ForegroundColor Yellow
Write-Host "   5. Tren router: dat DHCP DNS = IP cua thiet bi de ca nha duoc chan quang cao." -ForegroundColor Gray
Write-Host ""
Write-Host "  Kiem tra nhanh:  nslookup doubleclick.net <IP-thiet-bi>  ->  phai tra 0.0.0.0" -ForegroundColor White
Write-Host "  Chi tiet: xem README.md" -ForegroundColor DarkGray
