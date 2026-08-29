#!/usr/bin/env python3
"""
FOCUS AIoT - MiMo HTTPS 中继 (tools/mimo_relay.py)
====================================================
设备 (ESP32-S3-EYE) 没有 TLS 栈, 无法直连 https 的 MiMo endpoint。
本中继部署在 PC / 租用服务器上:
  设备 --(http)--> 中继 --(https)--> MiMo

设备把 OpenAI 兼容的 chat/completions 请求体 (含 base64 图片) POST 到中继,
中继原样转发给 MiMo, 再把响应回传。
顺带把最近一帧存为 PREVIEW_JPEG_PATH, 浏览器 GET /preview 实时查看画面
(设备侧零改动, 只是搭 MiMo 识图的便车)。

部署:
  MIMO_API_KEY=<mimo的key> RELAY_TOKEN=<中继访问令牌> python3 mimo_relay.py
  或填在下面的默认值里。

设备侧配置 (perception.c):
  #define MIMO_ENDPOINT_DEFAULT "http://<服务器IP>:8600/v1/chat/completions"
设备需在 Authorization 头带 RELAY_TOKEN (wifi_set_http_auth 设置)。

安全:
  RELAY_TOKEN 用于防止公网其他人蹭用 MiMo 额度 (未设置则不校验, 仅限内网测试)。
"""

import base64
import http.server
import os
import sys
import urllib.error
import urllib.request

# MiMo 专属 endpoint (tokenplan)
MIMO_URL = os.environ.get(
    "MIMO_URL",
    "https://token-plan-cn.xiaomimimo.com/v1/chat/completions",
)

# MiMo 的 API key (放在服务器, 不必烧进设备)
MIMO_API_KEY = os.environ.get(
    "MIMO_API_KEY",
    "tp-cqhvcy96byytd23azumypy2925bpmilukor7fp3k2tu8h5ew",
)

# 中继访问令牌: 设备 Authorization: Bearer <RELAY_TOKEN>
# 留空 = 不校验 (仅建议内网联调用)
RELAY_TOKEN = os.environ.get("RELAY_TOKEN", "")

PORT = int(os.environ.get("PORT", "8600"))

# 网页预览: 把设备 POST 来的最近一帧解码存盘, 浏览器 /preview 轮询查看。
PREVIEW_JPEG_PATH = os.environ.get("PREVIEW_JPEG_PATH", "/tmp/preview.jpg")

# /preview 页面: 单文件 HTML, JS 每 2s 用时间戳绕过缓存拉取最新帧。
PREVIEW_PAGE = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FOCUS AIoT 摄像头预览</title>
<style>
  body{margin:0;background:#111;color:#eee;font-family:monospace;
       display:flex;flex-direction:column;align-items:center}
  h1{font-size:16px;margin:12px 0 4px}
  img{max-width:100%;max-height:88vh;border:1px solid #333;
      image-rendering:pixelated;background:#000}
</style>
</head>
<body>
<h1>FOCUS AIoT 实时预览 (约每 5s 刷新一帧)</h1>
<img id="pv" src="/preview.jpg" alt="等待设备上传画面...">
<script>
var img = document.getElementById('pv');
setInterval(function(){ img.src = '/preview.jpg?t=' + Date.now(); }, 2000);
</script>
</body>
</html>
"""


class RelayHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            # 1. 校验中继令牌 (设备侧 wifi_set_http_auth 设置)
            if RELAY_TOKEN:
                auth = self.headers.get("Authorization", "")
                if auth != "Bearer " + RELAY_TOKEN:
                    self._reply(403, b'{"error":"forbidden: bad relay token"}')
                    return

            # 2. 读设备发来的请求体 (OpenAI 兼容, 含 base64 图, 可能上百 KB)
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)

            # 2.5 顺带存最新一帧供网页预览 (失败不影响识别转发)
            self._save_preview_frame(body)

            # 3. 转发给 MiMo (https)
            req = urllib.request.Request(
                MIMO_URL,
                data=body,
                headers={
                    "Content-Type": "application/json",
                    "Authorization": "Bearer " + MIMO_API_KEY,
                },
            )
            try:
                with urllib.request.urlopen(req, timeout=90) as resp:
                    payload = resp.read()
                    self._reply(resp.status, payload)
            except urllib.error.HTTPError as e:
                self._reply(e.code, e.read())

        except Exception as e:  # noqa: BLE001
            self._reply(502, ("{\"error\":\"relay: %s\"}" % e).encode())

    def do_GET(self):
        # 网页预览路由 (POST 转发不受影响)
        if self.path == "/preview":
            self._reply(200, PREVIEW_PAGE.encode(),
                        content_type="text/html; charset=utf-8")
        elif self.path == "/preview.jpg":
            self._serve_preview_jpeg()
        else:
            self._reply(404, b'{"error":"not found"}')

    def _serve_preview_jpeg(self):
        try:
            with open(PREVIEW_JPEG_PATH, "rb") as f:
                payload = f.read()
            self._reply(200, payload, content_type="image/jpeg",
                        extra_headers={"Cache-Control": "no-store"})
        except FileNotFoundError:
            self._reply(404, b'{"error":"no preview frame yet"}')
        except OSError as e:
            self._reply(500, ("{\"error\":\"preview io: %s\"}" % e).encode())

    def _save_preview_frame(self, body):
        try:
            b64 = self._extract_jpeg_b64(body)
            if b64 is None:
                return
            data = base64.b64decode(b64)
            if len(data) < 4:  # 空/损坏帧直接丢弃
                return
            tmp = PREVIEW_JPEG_PATH + ".tmp"
            with open(tmp, "wb") as f:
                f.write(data)
            os.replace(tmp, PREVIEW_JPEG_PATH)  # 原子替换, 网页不会读到半张图
            print("[relay] preview saved %d bytes" % len(data))
        except Exception as e:  # noqa: BLE001
            print("[relay] preview save failed: %s" % e)

    @staticmethod
    def _extract_jpeg_b64(body):
        """从 OpenAI 兼容请求体提取 data:image/jpeg;base64,<b64>。
        base64 只含 A-Za-z0-9+/= (不含引号), 读到下一个引号即可, 无需 JSON 库。"""
        marker = b"data:image/jpeg;base64,"
        idx = body.find(marker)
        if idx < 0:
            return None
        start = idx + len(marker)
        end = body.find(b'"', start)
        if end < 0:
            end = len(body)
        return body[start:end]

    def _reply(self, code, payload, content_type="application/json",
               extra_headers=None):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):  # 静音
        sys.stderr.write("[relay] %s\n" % (fmt % args))


def main():
    print("[relay] MiMo 中继监听 0.0.0.0:%d -> %s" % (PORT, MIMO_URL))
    print("[relay] 网页预览: http://<本机IP>:%d/preview" % PORT)
    if RELAY_TOKEN:
        print("[relay] 已启用访问令牌校验 (设备需带 Authorization)")
    else:
        print("[relay] 警告: 未设置 RELAY_TOKEN, 不校验来源")
    # 必须用多线程: 单线程 HTTPServer 在 MiMo 慢请求时无法 accept 新连接,
    # 内核接收队列积压满后直接丢弃 SYN (真机表现为连接超时 / 网络不通)。
    http.server.ThreadingHTTPServer(("0.0.0.0", PORT), RelayHandler).serve_forever()


if __name__ == "__main__":
    main()
