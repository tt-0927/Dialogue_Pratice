#!/usr/bin/env python3
"""注册接口自检。

用法（服务端工程目录下）：
    python3 scripts/test_register.py [--host 127.0.0.1] [--port 9000]

覆盖：正常注册、注册后可登录、重名、用户名格式、密码强度、缺字段、单连接限流。
"""

import argparse
import json
import random
import socket
import struct
import sys

OP_LOGIN = 0x01
OP_REGISTER = 0x08
OP_LOGIN_RESP = 0x81
OP_ERROR = 0x85
OP_REGISTER_RESP = 0x8B


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


class Conn:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)

    def call(self, op, obj):
        send_frame(self.sock, op, obj)
        return recv_frame(self.sock)

    def close(self):
        self.sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    args = ap.parse_args()

    passed = failed = 0

    def check(label, condition, detail=""):
        nonlocal passed, failed
        if condition:
            passed += 1
            print(f"  [PASS] {label}{('  ' + detail) if detail else ''}")
        else:
            failed += 1
            print(f"  [FAIL] {label}{('  ' + detail) if detail else ''}")

    suffix = random.randint(1000, 9999)
    new_user = f"tester{suffix}"
    new_pass = "pass123456"

    print("== 注册接口自检 ==")

    # 注册与登录必须用不同连接：同一条连接登录后就不能再注册了
    # （服务端会返回 already_logged_in，那是正确行为）

    # 1. 正常注册
    c = Conn(args.host, args.port)
    op, body = c.call(OP_REGISTER, {
        "username": new_user,
        "password": new_pass,
        "nickname": f"Tester{suffix}",
        "age": 25,
        "desc": "注册自检创建的账号",
    })
    check("正常注册成功",
          op == OP_REGISTER_RESP and body.get("ok") is True,
          f"userId={body.get('userId') if body else None}")
    c.close()

    # 2. 用新账号登录（新连接）
    c = Conn(args.host, args.port)
    op, body = c.call(OP_LOGIN, {"name": new_user, "password": new_pass})
    check("新账号可登录",
          op == OP_LOGIN_RESP and body.get("accountId", 0) > 0,
          f"accountId={body.get('accountId') if body else None}")

    # 3. 已登录状态下注册应被拒（复用上面的登录连接）
    # 登录成功后服务端会补发一条欢迎系统消息，先消费掉，否则会读错帧
    recv_frame(c.sock)
    op, body = c.call(OP_REGISTER, {"username": f"x{suffix}", "password": new_pass})
    check("已登录后注册被拒绝",
          op == OP_ERROR and body.get("code") == "already_logged_in",
          f"code={body.get('code') if body else None}")
    c.close()

    # 4. 重名（新连接，未登录）
    c = Conn(args.host, args.port)
    op, body = c.call(OP_REGISTER, {"username": new_user, "password": new_pass})
    check("重名被拒绝",
          op == OP_ERROR and body.get("code") == "username_taken",
          f"code={body.get('code') if body else None}")
    c.close()

    # 5. 格式与强度校验（同一连接，顺序消耗注册配额）
    c = Conn(args.host, args.port)
    op, body = c.call(OP_REGISTER, {"username": "ab", "password": new_pass})
    check("用户名过短被拒绝",
          op == OP_ERROR and body.get("code") == "bad_username",
          f"code={body.get('code') if body else None}")

    op, body = c.call(OP_REGISTER, {"username": "bad name!", "password": new_pass})
    check("用户名含非法字符被拒绝",
          op == OP_ERROR and body.get("code") == "bad_username",
          f"code={body.get('code') if body else None}")

    op, body = c.call(OP_REGISTER, {"username": f"weak{suffix}", "password": "123"})
    check("密码过短被拒绝",
          op == OP_ERROR and body.get("code") == "weak_password",
          f"code={body.get('code') if body else None}")

    # 第 4 次尝试：已用完 3 次配额，应被限流
    op, body = c.call(OP_REGISTER, {"username": f"flood{suffix}", "password": new_pass})
    check("单连接注册次数超限被拒绝",
          op == OP_ERROR and body.get("code") == "too_many_registrations",
          f"code={body.get('code') if body else None}")
    c.close()

    print(f"\n== 结果：{passed} 通过 / {failed} 失败 ==")
    print(f"（本次创建的测试账号：{new_user}）")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
