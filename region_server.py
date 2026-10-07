#!/usr/bin/env python3
"""region-server.py — 区域编辑 HTTP 小服务

提供:
  /              -> web/region-editor.html (唯一静态页面)
  /snapshot.jpg  -> 从 V4L2/RTSP/文件抓一帧 JPEG（通过 ffmpeg）
  /regions.json  -> GET: 返回当前 regions.json；POST: 保存 regions.json

用法:
  # 默认: camera 模式，端口 9191
  python3 region-server.py

  # 指定源和端口
  python3 region-server.py --source rtsp --url rtsp://<user>:<password>@192.168.0.200:554/stream1 --port 8080

选项:
  --source    -s  抓帧源: camera / rtsp / file (默认: camera)
  --device    -d  V4L2 设备 (默认: /dev/video41)
  --url       -u  RTSP URL
  --path      -p  视频文件路径
  --port          HTTP 端口 (默认: 9191)
  --host          监听地址 (默认: 0.0.0.0)
  --regions       区域配置文件路径 (默认: 当前目录 regions.json)
"""

import functools
import os, sys, subprocess, json, shutil, tempfile, signal, time
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.parse import parse_qs, urlparse

# ── 默认配置 ──────────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WEB_DIR = os.path.join(SCRIPT_DIR, "web")
DEFAULT_PORT = 9191
DEFAULT_HOST = "0.0.0.0"
DEFAULT_SOURCE = "camera"
DEFAULT_DEVICE = "/dev/video41"
DEFAULT_REGIONS = os.path.join(SCRIPT_DIR, "configs", "regions.json")

# ── 参数解析 ──────────────────────────────────────────────────────────
import argparse
parser = argparse.ArgumentParser(description="Region Editor HTTP Server")
parser.add_argument("-s", "--source", default=DEFAULT_SOURCE,
                    choices=["camera", "rtsp", "file"])
parser.add_argument("-d", "--device", default=DEFAULT_DEVICE)
parser.add_argument("-u", "--url", default="")
parser.add_argument("-p", "--path", default="")
parser.add_argument("--port", type=int, default=DEFAULT_PORT)
parser.add_argument("--host", default=DEFAULT_HOST)
parser.add_argument("--regions", default=DEFAULT_REGIONS)
args = parser.parse_args()
VALIDATOR = os.path.join(SCRIPT_DIR, "tools", "validate-regions.py")

# ── 确保 regions 目录存在 ──────────────────────────────────────────
REGIONS_DIR = os.path.dirname(args.regions)
if REGIONS_DIR and not os.path.exists(REGIONS_DIR):
    os.makedirs(REGIONS_DIR, exist_ok=True)


class RegionHandler(SimpleHTTPRequestHandler):
    """HTTP handler for region editing tools."""

    # 禁止目录列表
    def list_directory(self, path):
        self.send_error(403, "Forbidden")
        return None

    def translate_path(self, path):
        """Serve web assets independent of the process working directory."""
        relative = os.path.normpath(urlparse(path).path.lstrip("/"))
        root = os.path.realpath(WEB_DIR)
        candidate = os.path.realpath(os.path.join(root, relative))
        if candidate != root and not candidate.startswith(root + os.sep):
            return root
        return candidate

    def end_headers(self):
        origin = self.headers.get("Origin", "")
        if origin and ("127.0.0.1" in origin or "localhost" in origin):
            self.send_header("Access-Control-Allow-Origin", origin)
        else:
            # The editor is intended to be opened from another host on the
            # board LAN; the API is deliberately same-purpose and has no
            # credential-bearing endpoints.
            self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_HEAD(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/snapshot.jpg":
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Cache-Control", "no-cache, no-store")
            self.end_headers()
        elif path == "/regions.json":
            self._serve_get_regions(head_only=True)
        else:
            if path == "/":
                self.path = "/region-editor.html"
            super().do_HEAD()

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/snapshot.jpg":
            self._serve_snapshot(parsed.query)
        elif path == "/regions.json":
            self._serve_get_regions()
        else:
            # 默认服务静态文件（相对于 SCRIPT_DIR）
            if path == "/":
                path = "/region-editor.html"
            self.path = path
            super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/regions.json":
            self._serve_post_regions()
        elif parsed.path == "/reload":
            self._serve_reload()
        else:
            self.send_error(404, "Not Found")

    # ── snapshot.jpg ────────────────────────────────────────────────

    def _serve_snapshot(self, query=""):
        """抓一帧并返回 JPEG"""
        tmp = tempfile.NamedTemporaryFile(suffix=".jpg", delete=False)
        tmp.close()
        try:
            params = parse_qs(query, keep_blank_values=True)
            ok = self._capture_frame(tmp.name, params)
            if not ok or not os.path.exists(tmp.name):
                self.send_error(502, "Snapshot failed")
                return
            size = os.path.getsize(tmp.name)
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(size))
            self.send_header("Cache-Control", "no-cache, no-store")
            self.end_headers()
            with open(tmp.name, "rb") as f:
                shutil.copyfileobj(f, self.wfile)
        finally:
            try:
                os.unlink(tmp.name)
            except OSError:
                pass

    def _param(self, params, name, default=""):
        vals = params.get(name)
        if not vals:
            return default
        return vals[0] if vals[0] != "" else default

    def _capture_frame(self, output_path, params=None):
        """根据配置的源抓一帧"""
        params = params or {}
        src = self._param(params, "source", args.source)
        if src not in ("camera", "rtsp", "file"):
            return False

        width = self._param(params, "width", "1920")
        height = self._param(params, "height", "1080")
        cmd = ["ffmpeg", "-y"]
        if src == "camera":
            device = self._param(params, "device", args.device)
            cmd += ["-f", "v4l2", "-video_size", f"{width}x{height}",
                    "-i", device]
        elif src == "rtsp":
            url = self._param(params, "url", args.url)
            if not url:
                return False
            cmd += ["-rtsp_transport", "tcp", "-i", url]
        elif src == "file":
            path = self._param(params, "path", args.path)
            if not path:
                return False
            cmd += ["-i", path]
        else:
            return False

        cmd += ["-vframes", "1", "-q:v", "2", output_path]
        try:
            result = subprocess.run(cmd, capture_output=True, timeout=15)
            if result.returncode != 0:
                return False
            return self._is_valid_jpeg(output_path)
        except Exception:
            return False

    def _is_valid_jpeg(self, path):
        try:
            if not os.path.exists(path) or os.path.getsize(path) < 4:
                return False
            with open(path, "rb") as f:
                return f.read(2) == b"\xff\xd8"
        except OSError:
            return False

    # ── regions.json ────────────────────────────────────────────────

    def _serve_get_regions(self, head_only=False):
        if not os.path.exists(args.regions):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            if not head_only:
                self.wfile.write(b'{"regions":[]}\n')
            return
        try:
            with open(args.regions, "r") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            if not head_only:
                self.wfile.write(data.encode())
        except Exception as e:
            self.send_error(500, f"Read error: {e}")

    def _serve_post_regions(self):
        length = int(self.headers.get("Content-Length", 0))
        if length == 0 or length > 1024 * 1024:
            self.send_error(400, "Empty body")
            return
        body = self.rfile.read(length)
        try:
            data = json.loads(body)
            validation = subprocess.run([sys.executable, VALIDATOR, "-"], input=body.decode("utf-8"),
                                        capture_output=True, text=True)
            if validation.returncode != 0:
                self.send_error(400, validation.stderr.strip() or "Invalid regions schema")
                return
            payload = json.dumps(data, indent=2) + "\n"
            fd, tmp_path = tempfile.mkstemp(prefix=".regions.", dir=REGIONS_DIR or ".")
            try:
                with os.fdopen(fd, "w") as f:
                    f.write(payload)
                    f.flush()
                    os.fsync(f.fileno())
                if os.path.exists(args.regions):
                    shutil.copy2(args.regions, args.regions + ".bak")
                os.replace(tmp_path, args.regions)
            except Exception:
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass
                raise
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok", "count": len(data.get("regions", []))}).encode())
        except json.JSONDecodeError as e:
            self.send_error(400, f"Invalid JSON: {e}")
        except Exception as e:
            self.send_error(500, f"Write error: {e}")

    def _serve_reload(self):
        """保持编辑器兼容：保存后返回成功。"""
        try:
            os.utime(args.regions, None)
            self._json_response({"status": "reload_scheduled"})
        except FileNotFoundError:
            self.send_error(404, "regions.json not found")
        except Exception as e:
            self.send_error(500, f"Reload error: {e}")

    def _json_response(self, data, status=200):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    # 屏蔽多余的日志
    def log_message(self, format, *args):
        pass


# ── 启动 ──────────────────────────────────────────────────────────────
class ReusableHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    handler = functools.partial(RegionHandler, directory=WEB_DIR)
    try:
        server = ReusableHTTPServer((args.host, args.port), handler)
    except OSError as e:
        print(f"[region-server] listen failed on {args.host}:{args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    src_desc = args.source
    if args.source == "camera":
        src_desc += f"({args.device})"
    elif args.source == "rtsp":
        src_desc += f"({args.url})"

    print(f"""
╔═══════════════════════════════════════════════════════════════╗
║  Posevent Region Server — 独立区域编辑 HTTP 服务               ║
╠═══════════════════════════════════════════════════════════════╣
║  源:       {src_desc:<47}║
║  地址:     http://{args.host}:{args.port:<39}║
║  区域配置: {args.regions:<43}║
║                                                               ║
║  端点:                                                        ║
║    GET  /                web/region-editor.html                ║
║    GET  /snapshot.jpg    从摄像头抓一帧 JPEG                    ║
║    GET  /regions.json    获取当前区域配置                       ║
║    POST /regions.json    保存区域配置                           ║
╚═══════════════════════════════════════════════════════════════╝
    """)

    def sig_handler(sig, frame):
        print("\n[region-server] shutting down...")
        sys.exit(0)
    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    server.serve_forever()
