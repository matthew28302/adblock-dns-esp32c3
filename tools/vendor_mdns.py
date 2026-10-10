"""Tải component mdns chính thức (espressif/esp-protocols) vào components/mdns.

Chỉ cần chạy nếu muốn cập nhật mdns (repo ĐÃ kèm sẵn bản đã vendored).
PlatformIO cắt component mdns khỏi bundle framework-espidf nên phải vendored thủ công.

Cách dùng:  python tools/vendor_mdns.py
"""
import os, sys, json, urllib.request

API = 'https://api.github.com/repos/espressif/esp-protocols/contents'
DEST = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     '..', 'components', 'mdns'))

def fetch_json(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'adblock-dns-esp32c3'})
    return json.load(urllib.request.urlopen(req, timeout=60))

def fetch_raw(url, path):
    req = urllib.request.Request(url, headers={'User-Agent': 'adblock-dns-esp32c3'})
    data = urllib.request.urlopen(req, timeout=60).read()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    print('  ', os.path.relpath(path, os.path.dirname(DEST)), f'({len(data)} B)')

def walk(api_url, dest_dir):
    for item in fetch_json(api_url):
        if item['type'] == 'dir':
            walk(item['url'], os.path.join(dest_dir, item['name']))
        elif item['type'] == 'file':
            if '/tests/' in item['path'] or '/examples/' in item['path']:
                continue
            fetch_raw(item['download_url'], os.path.join(dest_dir, item['name']))

print('Tai components/mdns tu espressif/esp-protocols...')
walk(f'{API}/components/mdns', DEST)
print('Xong ->', DEST)
