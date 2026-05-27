#!/usr/bin/env python3
"""
Pharus TrackLink Monitor
Listens on UDP 44345, serves live visualization at http://localhost:5000
No dependencies — pure stdlib.
"""
import socket, struct, threading, time, json
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse

UDP_PORT      = 44345
HTTP_PORT     = 5000
TRACK_TIMEOUT = 2.0

tracks    = {}
_stats    = {"total_packets": 0, "total_bytes": 0, "pps": 0.0, "last_seen": None}
_pkt_times = []
_lock     = threading.Lock()
STATE     = {0: "NEW", 1: "CONT", 2: "OFF"}


def parse(data):
    pos, n, out = 0, len(data), []
    while pos < n:
        if data[pos:pos+1] != b'T':
            pos += 1
            continue
        pos += 1
        if pos + 44 > n:
            break
        try:
            tid, st      = struct.unpack_from('<ii', data, pos); pos += 8
            cx, cy       = struct.unpack_from('<ff', data, pos); pos += 8
            _,  _        = struct.unpack_from('<ff', data, pos); pos += 8  # expectPos
            _,  _        = struct.unpack_from('<ff', data, pos); pos += 8  # orientation
            spd,         = struct.unpack_from('<f',  data, pos); pos += 4
            rx, ry       = struct.unpack_from('<ff', data, pos); pos += 8  # relPos (TUIO 0-1)
            while pos < n and data[pos:pos+1] == b'E':
                pos += 10  # 'E' + x(4) + y(4) + 'e'(1)
            if pos < n and data[pos:pos+1] == b't':
                pos += 1
            out.append(dict(id=tid, state=STATE.get(st, '?'),
                            x=round(cx, 3), y=round(cy, 3),
                            rx=round(rx, 4), ry=round(ry, 4),
                            spd=round(spd, 2), t=time.time()))
        except struct.error:
            break
    return out


def udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', UDP_PORT))
    sock.settimeout(1.0)
    print(f"[UDP] Listening on :{UDP_PORT}")
    while True:
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        now     = time.time()
        records = parse(data)
        with _lock:
            _stats['total_packets'] += 1
            _stats['total_bytes']   += len(data)
            _stats['last_seen']      = now
            _pkt_times.append(now)
            cutoff = now - 2.0
            while _pkt_times and _pkt_times[0] < cutoff:
                _pkt_times.pop(0)
            _stats['pps'] = len(_pkt_times) / 2.0
            for r in records:
                if r['state'] == 'OFF':
                    tracks.pop(r['id'], None)
                else:
                    tracks[r['id']] = r


def cleanup_loop():
    while True:
        now = time.time()
        with _lock:
            stale = [k for k, v in tracks.items() if now - v['t'] > TRACK_TIMEOUT]
            for k in stale:
                del tracks[k]
        time.sleep(0.5)


HTML = r"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Pharus Monitor</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { background: #0e0e0e; color: #ddd; font-family: monospace; display: flex; flex-direction: column; height: 100vh; }
#header {
  padding: 10px 16px; background: #161616; border-bottom: 1px solid #2a2a2a;
  display: flex; gap: 20px; align-items: center;
}
#header h1 { font-size: 13px; color: #0af; letter-spacing: 3px; }
.stat { font-size: 12px; color: #555; }
.stat span { color: #ccc; }
#status { font-size: 11px; padding: 3px 10px; border-radius: 3px; font-weight: bold; letter-spacing: 1px; }
.alive { background: #0d2e0d; color: #0f0; border: 1px solid #0a0; }
.dead  { background: #2e0d0d; color: #f55; border: 1px solid #a00; }
#main { display: flex; flex: 1; overflow: hidden; }
#canvas-wrap {
  flex: 1; display: flex; align-items: center; justify-content: center;
  background: #080808; padding: 16px;
}
canvas { border: 1px solid #222; background: #050505; }
#sidebar { width: 220px; background: #111; border-left: 1px solid #222; overflow-y: auto; padding: 10px; }
#sidebar h2 { font-size: 10px; color: #444; letter-spacing: 2px; margin-bottom: 10px; }
.track {
  background: #181818; border: 1px solid #252525; border-radius: 3px;
  padding: 7px 9px; margin-bottom: 6px; font-size: 11px;
}
.tid  { color: #0af; font-weight: bold; font-size: 13px; display: flex; justify-content: space-between; }
.spd  { color: #fa0; font-size: 11px; }
.pos  { color: #666; margin-top: 3px; line-height: 1.6; }
.pos b { color: #999; }
#raw  { padding: 8px 10px; background: #0a0a0a; border-top: 1px solid #1e1e1e; font-size: 10px; color: #444; max-height: 80px; overflow-y: auto; }
</style>
</head>
<body>
<div id="header">
  <h1>PHARUS MONITOR</h1>
  <div id="status" class="dead">○ NO SIGNAL</div>
  <div class="stat">pkt/s <span id="pps">0</span></div>
  <div class="stat">total <span id="total">0</span></div>
  <div class="stat">tracks <span id="tcount">0</span></div>
  <div class="stat" style="margin-left:auto">UDP :44345</div>
</div>
<div id="main">
  <div id="canvas-wrap">
    <canvas id="c"></canvas>
  </div>
  <div id="sidebar">
    <h2>ACTIVE TRACKS</h2>
    <div id="tlist"></div>
  </div>
</div>
<div id="raw">Waiting for packets...</div>

<script>
const canvas = document.getElementById('c');
const ctx    = canvas.getContext('2d');
const trails = {};
const TRAIL  = 40;
const COLORS = ['#0af','#0fa','#fa0','#f0a','#a0f','#ff0','#0ff','#f80'];
const trackColor = id => COLORS[Math.abs(id) % COLORS.length];

function resize() {
  const wrap = document.getElementById('canvas-wrap');
  const W = wrap.clientWidth  - 32;
  const H = wrap.clientHeight - 32;
  const sz = Math.min(W, H);
  canvas.width  = sz;
  canvas.height = Math.round(sz * 0.75);
}
resize();
window.addEventListener('resize', resize);

function draw(data) {
  const W = canvas.width, H = canvas.height;

  // fade
  ctx.fillStyle = 'rgba(5,5,5,0.25)';
  ctx.fillRect(0, 0, W, H);

  // grid
  ctx.strokeStyle = '#151515';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 10; i++) {
    ctx.beginPath(); ctx.moveTo(i * W / 10, 0);    ctx.lineTo(i * W / 10, H);    ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, i * H / 10);    ctx.lineTo(W, i * H / 10);    ctx.stroke();
  }

  const live = new Set(Object.keys(data.tracks).map(Number));
  for (const id of Object.keys(trails)) {
    if (!live.has(Number(id))) delete trails[id];
  }

  for (const [id, t] of Object.entries(data.tracks)) {
    const px = t.rx * W;
    const py = t.ry * H;
    const col = trackColor(Number(id));

    if (!trails[id]) trails[id] = [];
    trails[id].push([px, py]);
    if (trails[id].length > TRAIL) trails[id].shift();

    // trail
    const tr = trails[id];
    for (let i = 1; i < tr.length; i++) {
      ctx.strokeStyle = col.replace(')', `,${(i / tr.length) * 0.5})`).replace('rgb', 'rgba').replace('#', 'rgba(').replace('0af', '0,170,255,').replace('0fa','0,255,170,').replace('fa0','255,170,0,').replace('f0a','255,0,170,').replace('a0f','170,0,255,').replace('ff0','255,255,0,').replace('0ff','0,255,255,').replace('f80','255,136,0,');
      // simpler:
      ctx.globalAlpha = (i / tr.length) * 0.6;
      ctx.strokeStyle = col;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(tr[i-1][0], tr[i-1][1]);
      ctx.lineTo(tr[i][0],   tr[i][1]);
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    // circle
    ctx.beginPath();
    ctx.arc(px, py, 20, 0, Math.PI * 2);
    ctx.strokeStyle = col;
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.fillStyle = col + '22';
    ctx.fill();

    // id
    ctx.fillStyle = col;
    ctx.font = 'bold 11px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(id, px, py);
  }
}

function sidebar(data) {
  const n = Object.keys(data.tracks).length;
  document.getElementById('tcount').textContent = n;
  document.getElementById('pps').textContent    = data.pps.toFixed(1);
  document.getElementById('total').textContent  = data.total_packets;

  const alive = data.last_seen && (Date.now() / 1000 - data.last_seen) < 2;
  const el = document.getElementById('status');
  el.textContent = alive ? '● SIGNAL' : '○ NO SIGNAL';
  el.className   = alive ? 'alive' : 'dead';

  document.getElementById('tlist').innerHTML = Object.entries(data.tracks).map(([id, t]) =>
    `<div class="track">
      <div class="tid">Track ${id} <span class="spd">${t.spd} m/s</span></div>
      <div class="pos">
        <b>abs</b>  ${t.x.toFixed(2)} m,  ${t.y.toFixed(2)} m<br>
        <b>rel</b>  ${t.rx.toFixed(3)},  ${t.ry.toFixed(3)}
      </div>
    </div>`
  ).join('') || '<div style="color:#333;font-size:11px">no active tracks</div>';

  if (data.total_packets > 0) {
    document.getElementById('raw').textContent =
      `last packet: ${new Date().toLocaleTimeString()}  |  ${data.total_packets} total  |  ${data.pps.toFixed(1)} pkt/s`;
  }
}

async function poll() {
  try {
    const r = await fetch('/api/state');
    const d = await r.json();
    draw(d);
    sidebar(d);
  } catch(e) {}
  setTimeout(poll, 100);
}
poll();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_GET(self):
        path = urlparse(self.path).path
        if path == '/api/state':
            with _lock:
                payload = json.dumps({
                    'tracks': {
                        str(k): {kk: vv for kk, vv in v.items() if kk != 't'}
                        for k, v in tracks.items()
                    },
                    'pps':           round(_stats['pps'], 1),
                    'total_packets': _stats['total_packets'],
                    'last_seen':     _stats['last_seen'],
                })
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(payload.encode())
        elif path in ('/', '/index.html'):
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(HTML.encode())
        else:
            self.send_response(404)
            self.end_headers()


if __name__ == '__main__':
    threading.Thread(target=udp_listener, daemon=True).start()
    threading.Thread(target=cleanup_loop, daemon=True).start()
    print(f"[Pharus Monitor]  http://localhost:{HTTP_PORT}")
    print(f"[UDP]             :{UDP_PORT}  (TrackLink binary protocol)")
    print("Ctrl+C to stop\n")
    try:
        HTTPServer(('', HTTP_PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
