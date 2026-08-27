#!/usr/bin/env python3
"""
FOCUS AIoT - MiMo HTTPS 中继 (tools/mimo_relay.py)
====================================================
设备 (ESP32-S3-EYE) 没有 TLS 栈, 无法直连 https 的 MiMo endpoint。
本中继部署在 PC / 租用服务器上:
  设备 --(http)--> 中继 --(https)--> MiMo

设备把 OpenAI 兼容的 chat/completions 请求体 (含 base64 图片) POST 到中继,
中继原样转发给 MiMo, 再把响应回传。

部署:
  MIMO_API_KEY=<mimo的key> RELAY_TOKEN=<中继访问令牌> python3 mimo_relay.py
  或填在下面的默认值里。

设备侧配置 (perception.c):
  #define MIMO_ENDPOINT_DEFAULT "http://<服务器IP>:8600/v1/chat/completions"
设备需在 Authorization 头带 RELAY_TOKEN (wifi_set_http_auth 设置)。

安全:
  RELAY_TOKEN 用于防止公网其他人蹭用 MiMo 额度 (未设置则不校验, 仅限内网测试)。
"""

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

    def _reply(self, code, payload):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):  # 静音
        sys.stderr.write("[relay] %s\n" % (fmt % args))


def main():
    print("[relay] MiMo 中继监听 0.0.0.0:%d -> %s" % (PORT, MIMO_URL))
    if RELAY_TOKEN:
        print("[relay] 已启用访问令牌校验 (设备需带 Authorization)")
    else:
        print("[relay] 警告: 未设置 RELAY_TOKEN, 不校验来源")
    # 必须用多线程: 单线程 HTTPServer 在 MiMo 慢请求时无法 accept 新连接,
    # 内核接收队列积压满后直接丢弃 SYN (真机表现为连接超时 / 网络不通)。
    http.server.ThreadingHTTPServer(("0.0.0.0", PORT), RelayHandler).serve_forever()


if __name__ == "__main__":
    main()
