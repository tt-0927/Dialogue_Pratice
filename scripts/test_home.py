#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""主页功能自检：资料/头像持久化 + 个人历史房间。

覆盖：
1. 注册新账号（带密码），登录后更新资料（含头像）。
2. 断线重连后头像从数据库恢复（持久化生效）。
3. 头像清除（avatar=null）与超限（> 32 KiB）校验。
4. 历史房间：仅进房未发言也会记录；发言后带出 lastSender/lastMessage/lastTs。
5. 账号隔离：别人的房间不出现在自己的历史里。
6. 匿名登录：历史房间返回空列表。

用法:
    python3 scripts/test_home.py [--host 127.0.0.1] [--port 9000]
"""
import argparse
import base64
import json
import random
import socket
import struct
import sys
import time

OP_LOGIN = 0x01
OP_JOIN = 0x02
OP_CHAT = 0x04
OP_UPDATE = 0x07
OP_REGISTER = 0x08
OP_LIST_HISTORY = 0x0A

OP_LOGIN_RESP = 0x81
OP_ERROR = 0x85
OP_PROFILE = 0x88
OP_HISTORY_ROOMS = 0x8C

PASSED = 0


def ok(msg):
    global PASSED
    PASSED += 1
    print(f"  [PASS] {msg}")


class Client:
    """最小协议客户端：组帧/解帧 + expect 断言。"""

    def __init__(self, host, port, label):
        self.label = label
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""

    def send(self, op, obj=None):
        payload = json.dumps(obj or {}, ensure_ascii=False).encode("utf-8")
        self.sock.sendall(struct.pack(">I", len(payload) + 1) + bytes([op]) + payload)

    def _pump(self, timeout):
        self.sock.settimeout(timeout)
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            return None
        except OSError:
            return None
        if not chunk:
            raise ConnectionError(f"{self.label}: 服务端已关闭连接")
        self.buf += chunk
        return True

    def recv(self, timeout=3.0):
        deadline = time.time() + timeout
        while True:
            while len(self.buf) >= 5:
                ln = struct.unpack(">I", self.buf[:4])[0]
                if len(self.buf) < 4 + ln:
                    break
                op = self.buf[4]
                payload = self.buf[5:4 + ln]
                self.buf = self.buf[4 + ln:]
                obj = json.loads(payload.decode("utf-8")) if payload else {}
                return op, obj
            remain = deadline - time.time()
            if remain <= 0:
                return None
            if self._pump(remain) is None:
                return None

    def expect(self, op, desc, pred=None, timeout=3.0):
        r = self.recv(timeout)
        assert r is not None, f"{self.label}: 超时，未收到「{desc}」"
        got_op, obj = r
        assert got_op == op, (
            f"{self.label}: 期望「{desc}」op=0x{op:02x}，实际 op=0x{got_op:02x} body={obj}"
        )
        if pred is not None:
            assert pred(obj), f"{self.label}: 「{desc}」内容断言失败: {obj}"
        return obj

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def login(c, name, password):
    """登录并消费 LoginResp 与 welcome 系统消息。"""
    c.send(OP_LOGIN, {"name": name, "password": password})
    resp = c.expect(OP_LOGIN_RESP, "登录成功")
    c.expect(0x84, "welcome")  # 系统欢迎消息
    return resp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    args = ap.parse_args()

    print(f"== 主页功能自检 target={args.host}:{args.port} ==")

    suffix = random.randint(1000, 9999)
    user_a = f"homea{suffix}"
    user_b = f"homeb{suffix}"
    password = "123456"
    room_silent = f"hs{suffix}"
    room_chat = f"hc{suffix}"
    avatar_bytes = b"\xff\xd8\xff\xe0FAKEJPEGDATA" + bytes(range(64))
    avatar_b64 = base64.b64encode(avatar_bytes).decode()

    # ---- 1. 注册两个新账号 ----
    ca = Client(args.host, args.port, user_a)
    ca.send(OP_REGISTER, {"username": user_a, "password": password, "nickname": "甲"})
    ca.expect(0x8B, "注册 A")
    ca.close()

    cb = Client(args.host, args.port, user_b)
    cb.send(OP_REGISTER, {"username": user_b, "password": password, "nickname": "乙"})
    cb.expect(0x8B, "注册 B")
    cb.close()
    ok("注册两个测试账号")

    # ---- 2. A 登录：初始无头像、历史房间为空 ----
    ca = Client(args.host, args.port, user_a)
    resp = login(ca, user_a, password)
    assert resp.get("avatar", "") == "", f"新账号不应有头像: {resp}"
    ca.send(OP_LIST_HISTORY)
    hr = ca.expect(OP_HISTORY_ROOMS, "历史房间（初始）", lambda o: o.get("rooms") == [])
    assert hr["rooms"] == []
    ok("新账号头像为空、历史房间为空")

    # ---- 3. 更新资料带头像 ----
    ca.send(OP_UPDATE, {"name": "甲甲", "age": 20, "desc": "练口语", "avatar": avatar_b64})
    pr = ca.expect(OP_PROFILE, "资料更新", lambda o: o.get("avatar") == avatar_b64)
    assert pr["name"] == "甲甲" and pr["age"] == 20 and pr["desc"] == "练口语"
    ok("资料更新成功且响应带回头像")

    # ---- 4. 超大头像被拒绝 ----
    big = base64.b64encode(b"x" * (32 * 1024 + 1)).decode()
    ca.send(OP_UPDATE, {"name": "甲甲", "avatar": big})
    ca.expect(OP_ERROR, "头像超限", lambda o: o.get("code") == "bad_avatar")
    ok("超过 32 KiB 的头像被拒绝")

    # ---- 5. A 进房（不发言）后断线 ----
    ca.send(OP_JOIN, {"room": room_silent})
    ca.expect(0x82, "进静默房")
    ca.close()

    # ---- 6. A 重新登录：头像恢复 + 历史房间含静默房 ----
    ca = Client(args.host, args.port, user_a)
    resp = login(ca, user_a, password)
    assert resp.get("avatar") == avatar_b64, f"头像应从数据库恢复: {resp.get('avatar', '')[:20]}"
    assert resp["name"] == "甲甲" and resp["age"] == 20 and resp["desc"] == "练口语"
    ok("重连后头像与资料从数据库恢复")

    ca.send(OP_LIST_HISTORY)
    hr = ca.expect(OP_HISTORY_ROOMS, "历史房间（含静默房）")
    rooms = {r["room"]: r for r in hr["rooms"]}
    assert room_silent in rooms, f"仅进房未发言的房间应保留: {hr}"
    assert rooms[room_silent]["lastMessage"] == "", f"静默房 lastMessage 应为空: {rooms[room_silent]}"
    ok("仅进房未发言的房间进入历史列表")

    # ---- 7. A 进聊天房并发一条消息，再查历史 ----
    ca.send(OP_JOIN, {"room": room_chat})
    ca.expect(0x82, "进聊天房")
    ca.recv(2.0)  # 消化 join 广播回显
    ca.send(OP_CHAT, {"text": "hello history"})
    ca.expect(0x83, "消息回显")
    ca.close()

    ca = Client(args.host, args.port, user_a)
    login(ca, user_a, password)
    ca.send(OP_LIST_HISTORY)
    hr = ca.expect(OP_HISTORY_ROOMS, "历史房间（含消息房）")
    rooms = {r["room"]: r for r in hr["rooms"]}
    assert room_chat in rooms, f"聊天房应在历史里: {hr}"
    r = rooms[room_chat]
    assert r["lastSender"] == "甲甲" and r["lastMessage"] == "hello history" and r["lastTs"] > 0, (
        f"最后一条消息字段错误: {r}"
    )
    ok("发言房带出 lastSender/lastMessage/lastTs")

    # ---- 8. B 登录：看不到 A 的房间 ----
    cb = Client(args.host, args.port, user_b)
    login(cb, user_b, password)
    cb.send(OP_LIST_HISTORY)
    hb = cb.expect(OP_HISTORY_ROOMS, "B 的历史房间")
    brooms = [r["room"] for r in hb["rooms"]]
    assert room_silent not in brooms and room_chat not in brooms, f"账号隔离失败: {brooms}"
    ok("历史房间账号隔离正确")
    cb.close()

    # ---- 9. 匿名登录：历史房间为空 ----
    cn = Client(args.host, args.port, "anon")
    login(cn, f"路人{suffix}", "")
    cn.send(OP_LIST_HISTORY)
    hn = cn.expect(OP_HISTORY_ROOMS, "匿名历史房间", lambda o: o.get("rooms") == [])
    assert hn["rooms"] == []
    ok("匿名登录历史房间为空")

    # ---- 10. 清除头像 ----
    ca.send(OP_UPDATE, {"name": "甲甲", "age": 20, "desc": "练口语", "avatar": None})
    ca.expect(OP_PROFILE, "清除头像", lambda o: o.get("avatar") == "")
    ca.close()

    ca = Client(args.host, args.port, user_a)
    resp = login(ca, user_a, password)
    assert resp.get("avatar", "") == "", f"清除应持久化: {resp.get('avatar', '')[:20]}"
    ok("清除头像后持久化生效")
    ca.close()

    print(f"\n== 结果：{PASSED} 通过 / 0 失败 ==")


if __name__ == "__main__":
    sys.exit(main())
