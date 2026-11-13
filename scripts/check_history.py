#!/usr/bin/env python3
"""验证「服务端重启后，历史消息能从数据库补回」。

用法（在服务端工程目录下）：
    python3 scripts/check_history.py [--host 127.0.0.1] [--port 9000] [--room 1001]

原理：登录 + 进房，收集所有 HistoryResp，统计条数与首尾消息。
内存环形缓冲在服务端重启后是空的，若仍能收到历史，说明走的是数据库。
"""

import argparse
import json
import socket
import struct
import sys

OP_LOGIN = 0x01
OP_JOIN = 0x02
OP_ROOM_STATE = 0x82
OP_HISTORY = 0x89
OP_ERROR = 0x85


def send_frame(sock, op, obj):
    payload = json.dumps(obj).encode("utf-8")
    sock.sendall(struct.pack(">IB", len(payload) + 1, op) + payload)


def recv_frame(sock):
    """返回 (opcode, dict) 或 None（连接关闭）。"""
    header = b""
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            return None
        header += chunk
    total = struct.unpack(">I", header)[0]

    body = b""
    while len(body) < total:
        chunk = sock.recv(total - len(body))
        if not chunk:
            return None
        body += chunk

    op = body[0]
    try:
        payload = json.loads(body[1:].decode("utf-8")) if len(body) > 1 else {}
    except json.JSONDecodeError:
        payload = {}
    return op, payload


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--room", default="1001")
    ap.add_argument("--name", default="alice")
    args = ap.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=5)
    send_frame(sock, OP_LOGIN, {"name": args.name})

    op, resp = recv_frame(sock)
    if op != 0x81:
        print(f"登录失败：op=0x{op:02X} {resp}", file=sys.stderr)
        return 1
    print(f"登录成功：userId={resp.get('userId')} name={resp.get('name')} "
          f"accountId={resp.get('accountId')}")

    send_frame(sock, OP_JOIN, {"room": args.room})

    texts, voices = [], []
    sock.settimeout(1.5)   # 收完历史后服务端不再主动发，靠超时退出
    while True:
        try:
            frame = recv_frame(sock)
        except (socket.timeout, TimeoutError):
            break
        if frame is None:
            break
        op, body = frame
        if op == OP_ERROR:
            print(f"错误：{body}", file=sys.stderr)
            break
        if op != OP_HISTORY:
            continue
        for msg in body.get("messages", []):
            if msg.get("kind") == "voice":
                voices.append(msg)
            else:
                texts.append(msg.get("text", ""))

    sock.close()

    print(f"房间 {args.room}：文本 {len(texts)} 条，语音 {len(voices)} 条")
    for t in texts[:10]:
        print("  -", t)
    if len(texts) > 10:
        print(f"  ... 还有 {len(texts) - 10} 条")
    return 0


if __name__ == "__main__":
    sys.exit(main())
