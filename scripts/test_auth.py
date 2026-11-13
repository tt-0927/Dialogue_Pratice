#!/usr/bin/env python3
"""登录认证的四种场景自检。

用法（服务端工程目录下）：
    python3 scripts/test_auth.py [--host 127.0.0.1] [--port 9000]

场景：
    1. 正确账号密码        -> 登录成功，accountId > 0
    2. 错误密码            -> auth_failed
    3. 不存在的账号 + 密码 -> auth_failed（提示与场景 2 一致，避免账号枚举）
    4. 不带密码            -> 兼容模式：按昵称匹配或匿名登录

若服务端以 requireAuth=true 启动，场景 4 会得到 auth_required——脚本会识别并说明。
"""

import argparse
import json
import socket
import struct
import sys

OP_LOGIN = 0x01
OP_LOGIN_RESP = 0x81
OP_ERROR = 0x85


def send_frame(sock, op, obj):
    payload = json.dumps(obj).encode("utf-8")
    sock.sendall(struct.pack(">IB", len(payload) + 1, op) + payload)


def recv_frame(sock):
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


def try_login(host, port, payload):
    """返回 (opcode, body)；连接失败返回 (None, None)。"""
    sock = socket.create_connection((host, port), timeout=5)
    try:
        send_frame(sock, OP_LOGIN, payload)
        return recv_frame(sock)
    finally:
        sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--user", default="alice")
    ap.add_argument("--password", default="123456")
    args = ap.parse_args()

    passed = 0
    failed = 0

    def check(label, condition, detail=""):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  [PASS] {label}{('  ' + detail) if detail else ''}")
        else:
            failed += 1
            print(f"  [FAIL] {label}{('  ' + detail) if detail else ''}")

    print("== 登录认证自检 ==")

    # 1. 正确密码
    op, body = try_login(args.host, args.port,
                         {"name": args.user, "password": args.password})
    check("正确账号密码登录成功",
          op == OP_LOGIN_RESP and body.get("accountId", 0) > 0,
          f"accountId={body.get('accountId') if body else None}")

    # 2. 错误密码
    op, body = try_login(args.host, args.port,
                         {"name": args.user, "password": "wrong-password"})
    check("错误密码被拒绝",
          op == OP_ERROR and body.get("code") == "auth_failed",
          f"code={body.get('code') if body else None}")

    # 3. 不存在的账号（提示应与场景 2 完全一致，避免账号枚举）
    op, body = try_login(args.host, args.port,
                         {"name": "no-such-user-xyz", "password": "123456"})
    check("不存在的账号被拒绝且提示一致",
          op == OP_ERROR and body.get("code") == "auth_failed",
          f"code={body.get('code') if body else None}")

    # 4. 不带密码
    op, body = try_login(args.host, args.port, {"name": args.user})
    if op == OP_LOGIN_RESP:
        check("不带密码走兼容模式成功", True,
              f"accountId={body.get('accountId')}")
    elif op == OP_ERROR and body.get("code") == "auth_required":
        check("服务端要求强制认证（requireAuth=true），无密码被拒", True)
    else:
        check("不带密码的处理", False, f"op={op} body={body}")

    print(f"\n== 结果：{passed} 通过 / {failed} 失败 ==")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
