#pragma once
// Dashboard HTML cho C3 AdBlock (bản tuỳ biến tiếng Việt) — PROGMEM, xem issue #6.
// Thiết kế: glassmorphism tối, gradient xanh lục–lam, donut tỉ lệ chặn,
// live feed "quảng cáo vừa chặn", kiểm tra tên miền, allow/block tuỳ chỉnh, OTA.

const char PAGE[] = R"HTML(<!doctype html><html lang=vi><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>C3 AdBlock — Bảng điều khiển</title>
<link rel=icon href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'><path d='M24 3l17 6.5v11.8C41 32.5 33.7 40.6 24 45 14.3 40.6 7 32.5 7 21.3V9.5L24 3z' fill='%2334d399'/></svg>">
<style>
:root{--bg:#070b13;--p:rgba(17,25,40,.62);--ln:rgba(148,163,184,.13);--tx:#e6edf6;--mut:#8b9bb4;--gr:#34d399;--cy:#22d3ee;--rd:#f87171;--am:#fbbf24}
*{box-sizing:border-box}
body{margin:0;font:15px/1.55 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;color:var(--tx);min-height:100vh;
background:radial-gradient(900px 480px at 88% -12%,rgba(34,211,238,.13),transparent 60%),
radial-gradient(760px 420px at -12% 2%,rgba(52,211,153,.11),transparent 55%),
radial-gradient(1000px 600px at 50% 120%,rgba(99,102,241,.08),transparent 60%),var(--bg)}
.wrap{max-width:1080px;margin:0 auto;padding:0 18px 46px}
header{display:flex;align-items:center;gap:14px;padding:20px 0 12px;flex-wrap:wrap}
.logo{width:44px;height:44px;flex:0 0 44px;filter:drop-shadow(0 4px 16px rgba(52,211,153,.35))}
h1{margin:0;font-size:20px;letter-spacing:.2px}
h1 small{display:block;font-size:11.5px;color:var(--mut);font-weight:500;letter-spacing:.6px;margin-top:2px}
.pill{display:inline-flex;align-items:center;gap:8px;padding:6px 14px;border-radius:99px;font-size:12.5px;font-weight:700;letter-spacing:.4px;background:rgba(52,211,153,.12);border:1px solid rgba(52,211,153,.35);color:var(--gr)}
.pill.off{background:rgba(251,191,36,.1);border-color:rgba(251,191,36,.35);color:var(--am)}
.pill .dot{width:8px;height:8px;border-radius:50%;background:currentColor;box-shadow:0 0 10px currentColor;animation:pl 2.2s infinite}
@keyframes pl{50%{opacity:.35}}
.chip{margin-left:auto;display:flex;gap:8px;flex-wrap:wrap;color:var(--mut);font-size:12px;font-variant-numeric:tabular-nums}
.chip span{border:1px solid var(--ln);background:var(--p);padding:5px 11px;border-radius:9px;backdrop-filter:blur(6px)}
.warn{display:none;background:rgba(248,113,113,.08);border:1px solid rgba(248,113,113,.4);color:#ffb4ad;border-radius:14px;padding:12px 16px;margin:10px 0;font-size:13.5px}
.panel{background:var(--p);border:1px solid var(--ln);border-radius:18px;padding:20px;backdrop-filter:blur(10px);box-shadow:0 8px 30px rgba(2,6,17,.45)}
h2{font-size:12.5px;text-transform:uppercase;letter-spacing:1.2px;color:var(--mut);margin:0 0 12px;font-weight:700}
.hero{display:grid;grid-template-columns:1.5fr 1fr;gap:16px;margin:16px 0 14px}
.donutrow{display:flex;align-items:center;gap:22px}
.donut{position:relative;width:126px;height:126px;flex:0 0 126px}
.donut svg{width:100%;height:100%;transform:rotate(-90deg)}
.donut .c{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center}
.donut .c b{font-size:24px;font-weight:800}
.donut .c i{font-style:normal;font-size:9.5px;letter-spacing:1.4px;color:var(--mut);text-transform:uppercase}
.big{font-size:40px;font-weight:800;line-height:1.05;font-variant-numeric:tabular-nums;background:linear-gradient(90deg,var(--gr),var(--cy));-webkit-background-clip:text;background-clip:text;color:transparent}
.sub{color:var(--mut);font-size:13px;margin-top:6px}.sub b{color:var(--tx)}
.pbox{display:flex;flex-direction:column;gap:10px;justify-content:center}
.pbox .st{font-weight:700;font-size:15px}
.pbox .row{display:flex;gap:8px;flex-wrap:wrap}
button{cursor:pointer;font:inherit;border-radius:10px;border:1px solid var(--ln);background:rgba(148,163,184,.08);color:var(--tx);padding:8px 14px;transition:.15s}
button:hover{background:rgba(148,163,184,.16);border-color:rgba(148,163,184,.3)}
button.primary{background:linear-gradient(90deg,rgba(52,211,153,.18),rgba(34,211,238,.18));border-color:rgba(52,211,153,.4);color:#a7f3d0;font-weight:600}
button.primary:hover{border-color:rgba(52,211,153,.7)}
button.danger{color:#fda4af;border-color:rgba(248,113,113,.35)}
button.danger:hover{background:rgba(248,113,113,.12)}
select,input{font:inherit;color:var(--tx);background:rgba(7,11,19,.8);border:1px solid var(--ln);border-radius:10px;padding:8px 12px;outline:0}
input:focus,select:focus{border-color:rgba(34,211,238,.55)}
.hint{color:var(--mut);font-size:12px;margin:8px 0 0}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin:14px 0}
.card{background:var(--p);border:1px solid var(--ln);border-radius:14px;padding:13px 16px;backdrop-filter:blur(8px)}
.card .v{font-size:19px;font-weight:700;font-variant-numeric:tabular-nums;word-break:break-word}
.card .l{color:var(--mut);font-size:11.5px;margin-top:2px;letter-spacing:.3px}
.v.g{color:var(--gr)}.v.c{color:var(--cy)}.v.r{color:var(--rd)}
.fent{display:flex;align-items:center;gap:10px;padding:8px 4px;border-bottom:1px solid rgba(148,163,184,.08);animation:fin .4s ease;font-size:13.5px}
.fent:last-child{border-bottom:0}
@keyframes fin{from{opacity:0;transform:translateY(-4px)}}
.fent .fd{font-family:ui-monospace,Consolas,monospace;font-size:12.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1;color:#d7e3f4}
.fent .fi,.fent .ft{color:var(--mut);font-size:11.5px;white-space:nowrap;font-variant-numeric:tabular-nums}
.fdot{width:7px;height:7px;border-radius:50%;background:var(--gr);box-shadow:0 0 8px rgba(52,211,153,.8);flex:0 0 7px}
.empty{color:var(--mut);font-size:13px;padding:6px 2px}
.tabs{display:flex;gap:8px;margin:18px 0 14px;flex-wrap:wrap}
.tabs button{border-radius:99px;padding:8px 18px;font-weight:600;font-size:13.5px}
.tabs button.on{background:linear-gradient(90deg,rgba(52,211,153,.2),rgba(34,211,238,.2));border-color:rgba(34,211,238,.5);color:#a7f3d0}
.tab{display:none}.tab.on{display:block}
table{width:100%;border-collapse:collapse;font-size:13.5px}
th{color:var(--mut);text-align:left;font-size:11.5px;text-transform:uppercase;letter-spacing:.8px;padding:8px 10px;border-bottom:1px solid var(--ln);font-weight:700}
td{padding:9px 10px;border-bottom:1px solid rgba(148,163,184,.07);font-variant-numeric:tabular-nums}
tr:hover td{background:rgba(148,163,184,.04)}
.mono{font-family:ui-monospace,Consolas,monospace;font-size:12.5px}
.tag{font-size:10.5px;font-weight:700;border-radius:6px;padding:2px 7px;letter-spacing:.4px}
.tag.b{background:rgba(248,113,113,.15);color:#fda4af}
.mini{color:var(--gr)}.plus{color:var(--cy)}
.two{display:grid;grid-template-columns:1fr 1fr;gap:16px}
.addrow{display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap}
.addrow input{flex:1;min-width:160px}
.domlist{max-height:340px;overflow:auto}
.ckrow{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}
.ckrow input{flex:1;min-width:200px}
.result{padding:14px 16px;border-radius:14px;font-size:14px;border:1px solid var(--ln);display:none;word-break:break-all}
.result.bad{background:rgba(248,113,113,.1);border-color:rgba(248,113,113,.4);color:#ffc9c4}
.result.ok{background:rgba(52,211,153,.1);border-color:rgba(52,211,153,.35);color:#b9f5d9}
.result.warn{background:rgba(34,211,238,.09);border-color:rgba(34,211,238,.35);color:#c4f3fb}
.uprow{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:6px}
.uprow input[type=file]{border:0;padding:6px 0;background:transparent;color:var(--mut)}
.sec{margin-bottom:24px}
footer{margin-top:26px;color:var(--mut);font-size:11.5px;text-align:center;letter-spacing:.4px}
#toast{position:fixed;bottom:22px;left:50%;transform:translateX(-50%) translateY(80px);background:#10192b;border:1px solid rgba(148,163,184,.3);padding:11px 20px;border-radius:12px;font-size:13.5px;transition:.3s;opacity:0;z-index:99;box-shadow:0 10px 30px rgba(0,0,0,.5);max-width:90vw}
#toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
@media(max-width:760px){.hero{grid-template-columns:1fr}.big{font-size:32px}.donut{width:110px;height:110px;flex-basis:110px}.two{grid-template-columns:1fr}}
</style></head><body>
<div class=wrap>
<header>
<svg class=logo viewBox="0 0 48 48" fill=none>
<defs><linearGradient id=lg x1=0 y1=0 x2=1 y2=1><stop offset=0 stop-color=#34d399/><stop offset=1 stop-color=#22d3ee/></linearGradient></defs>
<path d="M24 3l17 6.5v11.8C41 32.5 33.7 40.6 24 45 14.3 40.6 7 32.5 7 21.3V9.5L24 3z" fill=url(#lg) opacity=.9/>
<path d="M16.5 24l5.2 5.2L32 18.8" stroke=#062018 stroke-width=3.4 stroke-linecap=round stroke-linejoin=round/>
</svg>
<h1>C3 AdBlock<small>Trình chặn quảng cáo DNS cho toàn bộ mạng nhà</small></h1>
<div class=pill id=state><span class=dot></span>…</div>
<div class=chip><span id=ip></span><span id=fwv></span></div>
</header>
<div class=warn id=credwarn>⚠️ <b>Đang dùng mật khẩu mặc định (admin).</b> Hãy đổi ngay trong mục Cập nhật → Đổi mật khẩu bên dưới.</div>
<section class=hero>
<div class="panel donutrow">
<div class=donut>
<svg viewBox="0 0 120 120">
<defs><linearGradient id=gr1 x1=0 y1=0 x2=1 y2=1><stop offset=0 stop-color=#34d399/><stop offset=1 stop-color=#22d3ee/></linearGradient></defs>
<circle cx=60 cy=60 r=52 fill=none stroke=rgba(148,163,184,.12) stroke-width=11/>
<circle id=ring cx=60 cy=60 r=52 fill=none stroke=url(#gr1) stroke-width=11 stroke-linecap=round stroke-dasharray="0 327"/>
</svg>
<div class=c><b id=rate>0%</b><i>tỉ lệ chặn</i></div>
</div>
<div>
<div class=big id=nblocked>0</div>
<div style=font-weight:600>truy vấn quảng cáo đã chặn</div>
<div class=sub><b id=nallowed>0</b> lượt được cho phép · <b id=ndomains>0</b> tên miền trong danh sách</div>
</div>
</div>
<div class="panel pbox">
<div class=st id=pausetitle>—</div>
<div class=row>
<select id=pausedur><option value=30>30 giây</option><option value=300 selected>5 phút</option><option value=1800>30 phút</option><option value=7200>2 giờ</option><option value=0>cho tới khi bật lại</option></select>
<button class=primary id=pausebtn onclick=togglePause()>Tạm dừng</button>
</div>
<p class=hint>Tạm dừng khi có app/web bị lỗi do chặn nhầm — sẽ tự bật lại sau thời gian đã chọn.</p>
</div>
</section>
<div class=cards id=sys></div>
<section class="panel feed">
<h2>Quảng cáo vừa chặn</h2>
<div id=feed></div>
</section>
<nav class=tabs>
<button data-t=devices class=on>Thiết bị</button>
<button data-t=check>Kiểm tra tên miền</button>
<button data-t=lists>Chặn & cho phép</button>
<button data-t=updates>Cập nhật</button>
</nav>
<div id=tab-devices class="tab panel on">
<h2>Thiết bị trong mạng</h2>
<table><thead><tr><th>IP</th><th>MAC</th><th>Đã chặn</th><th>Đã cho phép</th><th></th></tr></thead><tbody id=ct></tbody></table>
<p class=hint>"Cấm" = sinkhole toàn bộ DNS của thiết bị đó (mọi tên miền trả về 0.0.0.0).</p>
</div>
<div id=tab-check class="tab panel">
<h2>Kiểm tra tên miền</h2>
<div class=ckrow><input id=ckdom placeholder="ví dụ: ads.tiktok.com hoặc facebook.com" onkeydown="if(event.key=='Enter')doCheck()"><button class=primary onclick=doCheck()>Kiểm tra</button></div>
<div id=ckres class=result></div>
<p class=hint>Xem nhanh một tên miền đang bị chặn hay được cho phép — không cần đăng nhập.</p>
</div>
<div id=tab-lists class="tab panel">
<h2>Danh sách chặn / cho phép tuỳ chỉnh</h2>
<div class=two>
<div>
<div class=addrow><input id=bdom placeholder="chặn thêm: ads.vi-du.com"><button class=primary onclick=addDom()>+ Chặn</button></div>
<div class=domlist id=bl></div>
</div>
<div>
<div class=addrow><input id=adom placeholder="cho phép: vi-du.com"><button class=primary onclick=addAlw()>+ Cho phép</button></div>
<div class=domlist id=al></div>
</div>
</div>
<p class=hint>"Cho phép" ghi đè danh sách chặn cho tên miền đó và mọi subdomain của nó — dùng khi chặn nhầm.</p>
</div>
<div id=tab-updates class="tab panel">
<div class=sec>
<h2>Danh sách chặn — tải lên</h2>
<form id=upf class=uprow><input type=file id=blf accept=.bin><button class=primary>Tải lên</button><span class=hint id=upmsg></span></form>
<p class=hint>Tự dựng bằng <code>python ../esp32-c3-adblock/tools/build_blocklist.py data/blocklist.bin</code> rồi tải lên đây — không cần cáp USB.</p>
</div>
<div class=sec>
<h2>Danh sách chặn — tự cập nhật từ xa</h2>
<div class=uprow><input id=uurl placeholder="https://may-chu/blocklist.bin" style="flex:1;min-width:220px"><span class=hint>mỗi</span><input id=uiv size=3 value=24> giờ
<button onclick=saveUpd()>Lưu</button> <button onclick=fetchNow()>Tải ngay</button></div>
<p class=hint>Thiết bị tự tải <code>blocklist.bin</code> từ URL theo lịch. Lần cuối: <span id=ustat>—</span></p>
</div>
<div class=sec>
<h2>Firmware — cập nhật OTA</h2>
<form id=fwf class=uprow><input type=file id=fwb accept=.bin><button class=primary>Nạp firmware</button><span class=hint id=fwmsg></span></form>
<p class=hint>Tải lên file <code>.pio/build/c3/firmware.bin</code> — thiết bị tự xác minh và khởi động lại (~15 giây).</p>
</div>
<div class=sec>
<h2>Đổi mật khẩu</h2>
<div class=uprow><input id=auser placeholder="tên đăng nhập" value=admin style="width:130px"><input id=apass type=password placeholder="mật khẩu web (≥4 ký tự)" style="flex:1;min-width:160px"><input id=aota type=password placeholder="mật khẩu OTA (≥4 ký tự)" style="flex:1;min-width:160px"><button class=primary onclick=saveAuth()>Lưu</button></div>
<p class=hint>Tên + mật khẩu bảo vệ các thao tác thay đổi (chặn/bỏ chặn, tải lên, OTA, WiFi). Mật khẩu OTA dùng khi nạp firmware.</p>
</div>
<div class=sec>
<h2>WiFi</h2>
<button class=danger onclick="if(confirm('Xoá WiFi đã lưu và khởi động lại vào cổng cấu hình?'))forgetWifi()">Quên WiFi & cấu hình lại</button>
<p class=hint>Hoặc giữ nút BOOT trên mạch khi cắm điện để mở lại cổng cấu hình.</p>
</div>
</div>
<footer>C3 AdBlock · ESP32-C3 · DNS sinkhole kiểu Pi-hole · c3adblock.local · IP: <span id=host2></span></footer>
</div>
<div id=toast></div>
<script>
const $=i=>document.getElementById(i);
const CSRF={'X-Requested-With':'c3-adblock'};  // header chống CSRF — giống server yêu cầu
let S=null;
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&#38;','<':'&#60;','>':'&#62;','"':'&#34;',"'":'&#39;'}[c]))}
function fmt(n){return Number(n).toLocaleString('vi-VN')}
function ago(t){if(t<5)return'vừa xong';if(t<60)return Math.floor(t)+' giây trước';if(t<3600)return Math.floor(t/60)+' phút trước';return Math.floor(t/3600)+' giờ trước'}
async function load(){try{S=await(await fetch('stats.json')).json();render()}catch(e){}}
function render(){
if(!S)return;
$('ip').textContent=S.ip;$('fwv').textContent='FW '+S.fw;$('host2')&&($('host2').textContent=S.ip);
const on=S.blocking!==false;
$('state').className='pill'+(on?'':' off');
$('state').innerHTML='<span class=dot></span>'+(on?'ĐANG CHẶN':(S.resumeIn>0?'TẠM DỪNG · còn '+S.resumeIn+'s':'TẠM DỪNG'));
$('pausetitle').textContent=on?'Chặn quảng cáo đang bật':'Đang tạm dừng chặn';
$('pausebtn').textContent=on?'Tạm dừng':'Bật lại ngay';
$('pausedur').style.display=on?'':'none';
const tot=S.blocked+S.allowed,rate=tot?Math.round(100*S.blocked/tot):0;
$('rate').textContent=rate+'%';
const C=2*Math.PI*52;$('ring').style.strokeDasharray=(rate/100*C)+' '+C;
$('nblocked').textContent=fmt(S.blocked);$('nallowed').textContent=fmt(S.allowed);$('ndomains').textContent=fmt(S.domains);
$('credwarn').style.display=S.defcreds?'block':'none';
$('sys').innerHTML=[
['Thiết bị đã thấy',S.clients.length,'g'],
['Tín hiệu WiFi',S.rssi+' dBm',(S.rssi>-65?'g':S.rssi>-75?'c':'r')],
['Nhiệt độ chip',S.temp+' °C',(S.temp>75?'r':'')],
['RAM trống',Math.round(S.heap/1024)+' KB',''],
['Danh sách chặn',(S.listbytes/1048576).toFixed(1)+' / '+(S.fst/1048576).toFixed(1)+' MB','c'],
['Uptime',S.uptime,''],
['Cập nhật từ xa',S.upstat||'chưa đặt URL','']
].map(c=>'<div class=card><div class="v '+c[2]+'">'+esc(c[1])+'</div><div class=l>'+c[0]+'</div></div>').join('');
const log=S.log||[];
$('feed').innerHTML=log.length?log.map(e=>'<div class=fent><span class=fdot></span><span class=fd>'+esc(e.d)+'</span><span class=fi>'+esc(e.ip)+'</span><span class=ft>'+ago(e.t)+'</span></div>').join(''):'<div class=empty>Chưa có truy vấn nào bị chặn — đang chờ quảng cáo đầu tiên gõ cửa…</div>';
$('ct').innerHTML=S.clients.length?S.clients.slice().sort((a,b)=>(b.blocked+b.allowed)-(a.blocked+a.allowed)).map(c=>
'<tr><td class=mono>'+esc(c.ip)+(c.banned?' <span class="tag b">CẤM</span>':'')+'</td><td class=mono>'+esc(c.mac)+'</td><td class=mini>'+fmt(c.blocked)+'</td><td class=plus>'+fmt(c.allowed)+'</td><td style="text-align:right"><button class=ban data-ip="'+esc(c.ip)+'">'+(c.banned?'Bỏ cấm':'Cấm')+'</button></td></tr>').join(''):'<tr><td colspan=5 class=empty>Chưa có thiết bị nào truy vấn DNS…</td></tr>';
$('bl').innerHTML=S.custom.length?S.custom.map(d=>'<div class=fent><span class=fdot style="background:var(--rd);box-shadow:0 0 8px rgba(248,113,113,.8)"></span><span class=fd>'+esc(d)+'</span><button class=rmbtn data-d="'+esc(d)+'">Gỡ</button></div>').join(''):'<div class=empty>Trống — thêm tên miền muốn chặn thêm ở trên.</div>';
$('al').innerHTML=S.allow.length?S.allow.map(d=>'<div class=fent><span class=fdot style="background:var(--cy);box-shadow:0 0 8px rgba(34,211,238,.8)"></span><span class=fd>'+esc(d)+'</span><button class=rmbtn data-d="'+esc(d)+'">Gỡ</button></div>').join(''):'<div class=empty>Trống — nếu app/web nào bị chặn nhầm, thêm tên miền của nó vào đây.</div>';
if(document.activeElement!=$('uurl'))$('uurl').value=S.upurl||'';
if(document.activeElement!=$('uiv'))$('uiv').value=S.upiv||24;
$('ustat').textContent=S.upstat||'—';
}
let tt;function toast(m){$('toast').textContent=m;$('toast').classList.add('show');clearTimeout(tt);tt=setTimeout(()=>$('toast').classList.remove('show'),2600)}
function togglePause(){if(!S)return;fetch(S.blocking!==false?'/pause?s='+$('pausedur').value:'/resume',{headers:CSRF}).then(()=>load())}
function addDom(){const d=$('bdom').value.trim();if(!d)return;fetch('/addblock?d='+encodeURIComponent(d),{headers:CSRF}).then(r=>{if(r.ok){toast('Đã thêm: chặn '+d);$('bdom').value='';load()}else toast('Cần đăng nhập (admin) trước')})}
function addAlw(){const d=$('adom').value.trim();if(!d)return;fetch('/addallow?d='+encodeURIComponent(d),{headers:CSRF}).then(r=>{if(r.ok){toast('Đã thêm: cho phép '+d);$('adom').value='';load()}else toast('Cần đăng nhập (admin) trước')})}
$('ct').addEventListener('click',e=>{const b=e.target.closest('.ban');if(b)fetch('/ban?ip='+b.dataset.ip,{headers:CSRF}).then(()=>{toast('Đã cập nhật thiết bị');load()})});
$('bl').addEventListener('click',e=>{const b=e.target.closest('.rmbtn');if(b)fetch('/unblock?d='+encodeURIComponent(b.dataset.d),{headers:CSRF}).then(()=>{toast('Đã gỡ khỏi danh sách chặn');load()})});
$('al').addEventListener('click',e=>{const b=e.target.closest('.rmbtn');if(b)fetch('/removeallow?d='+encodeURIComponent(b.dataset.d),{headers:CSRF}).then(()=>{toast('Đã gỡ khỏi danh sách cho phép');load()})});
async function doCheck(){const d=$('ckdom').value.trim().toLowerCase();if(!d)return;
try{const r=await(await fetch('/check?d='+encodeURIComponent(d))).json();const el=$('ckres');el.style.display='block';
if(!r.valid){el.className='result warn';el.textContent='“'+d+'” không trông giống một tên miền hợp lệ.'}
else if(r.allowed){el.className='result ok';el.textContent='✓ '+r.d+' đang ĐƯỢC CHO PHÉP (nằm trong danh sách cho phép tuỳ chỉnh) — sẽ không bao giờ bị chặn.'}
else if(r.blocked){el.className='result bad';el.textContent='✗ '+r.d+' đang BỊ CHẶN — có trong danh sách chặn.'}
else{el.className='result ok';el.textContent='✓ '+r.d+' không bị chặn — hoạt động bình thường.'}
}catch(e){toast('Lỗi kết nối')}}
function saveAuth(){const u=$('auser').value.trim()||'admin',w=$('apass').value,o=$('aota').value;if(w&&w.length<4){toast('Mật khẩu web phải ≥4 ký tự');return}if(o&&o.length<4){toast('Mật khẩu OTA phải ≥4 ký tự');return}fetch('/setauth?u='+encodeURIComponent(u)+'&w='+encodeURIComponent(w)+'&o='+encodeURIComponent(o),{headers:CSRF}).then(()=>{toast('Đã đổi mật khẩu — trình duyệt sẽ hỏi đăng nhập lại');$('apass').value='';$('aota').value='';setTimeout(load,800)})}
function saveUpd(){fetch('/setupdate?u='+encodeURIComponent($('uurl').value.trim())+'&h='+(parseInt($('uiv').value)||24),{headers:CSRF}).then(()=>{toast('Đã lưu cấu hình tự cập nhật');load()})}
function fetchNow(){$('ustat').textContent='đang tải…';fetch('/fetchnow',{headers:CSRF}).then(r=>r.text()).then(t=>{toast(t);load()})}
function forgetWifi(){fetch('/forgetwifi',{headers:CSRF}).then(r=>r.text()).then(t=>alert(t))}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{
document.querySelectorAll('.tabs button').forEach(x=>x.classList.remove('on'));b.classList.add('on');
document.querySelectorAll('.tab').forEach(x=>x.classList.remove('on'));$('tab-'+b.dataset.t).classList.add('on')});
$('upf').onsubmit=async e=>{e.preventDefault();const f=$('blf').files[0];if(!f)return;
$('upmsg').textContent='đang tải lên '+(f.size/1048576).toFixed(2)+' MB…';
const fd=new FormData();fd.append('f',f);
try{const r=await fetch('/upload',{method:'POST',headers:CSRF,body:fd});const t=await r.text();$('upmsg').textContent=r.ok?'✓ đã cập nhật danh sách':'✗ '+t;toast(r.ok?'Danh sách chặn đã được cập nhật':'Tải lên bị từ chối');}
catch(_){$('upmsg').textContent='✗ lỗi tải lên'}
$('blf').value='';setTimeout(load,800)};
$('fwf').onsubmit=async e=>{e.preventDefault();const f=$('fwb').files[0];if(!f)return;
$('fwmsg').textContent='đang nạp '+(f.size/1048576).toFixed(2)+' MB…';
const fd=new FormData();fd.append('f',f);
try{const r=await fetch('/update',{method:'POST',headers:CSRF,body:fd});$('fwmsg').textContent=r.ok?'✓ xong, đang khởi động lại…':'✗ '+await r.text();}
catch(_){$('fwmsg').textContent='✓ có thể đã xong — thiết bị đang khởi động lại'}
$('fwb').value=''};
load();setInterval(()=>{if(!document.hidden)load()},3000);
</script></body></html>)HTML";
