#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""协议级冒烟测试。

不依赖 Qt，直接按线上帧格式（4B 大端长度 + 1B opcode + JSON）验证服务端：
登录、进房、跨房隔离、消息广播、离开/掉线通知、错误码。

用法:
    python3 scripts/smoke_test.py [--host 127.0.0.1] [--port 9000]
"""
import argparse
import base64
import random
import json
import socket
import struct
import sys
import time

# 每次运行用不同房间号：聊天记录已持久化，固定房间号会撞上上次的历史
ROOM_SUFFIX = f"-{random.randint(1000, 9999)}"

OP_LOGIN = 0x01
OP_JOIN = 0x02
OP_LEAVE = 0x03
OP_CHAT = 0x04
OP_ROOMS = 0x05
OP_PING = 0x06
OP_UPDATE = 0x07

OP_LOGIN_RESP = 0x81
OP_ROOM_STATE = 0x82
OP_CHAT_PUSH = 0x83
OP_SYS = 0x84
OP_ERROR = 0x85
OP_PONG = 0x86
OP_ROOM_LIST = 0x87
OP_PROFILE = 0x88
OP_HISTORY = 0x89
OP_VOICE = 0x09
OP_VOICE_PUSH = 0x8A

PASSED = 0


def ok(msg):
    global PASSED
    PASSED += 1
    print(f"  [PASS] {msg}")


class Client:
    def __init__(self, host, port, label):
        self.label = label
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""

    # ---- 底层收发 ----
    def send(self, op, obj=None):
        payload = json.dumps(obj or {}, ensure_ascii=False).encode("utf-8")
        self.sock.sendall(struct.pack(">I", len(payload) + 1) + bytes([op]) + payload)

    def send_raw(self, data):
        self.sock.sendall(data)

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

    def expect_nothing(self, desc, timeout=0.5):
        r = self.recv(timeout)
        assert r is None, f"{self.label}: 不应收到消息「{desc}」，实际收到 {r}"

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    args = ap.parse_args()

    print(f"== 冒烟测试 target={args.host}:{args.port} ==")

    alice = Client(args.host, args.port, "alice")
    bob = Client(args.host, args.port, "bob")
    carol = Client(args.host, args.port, "carol")

    # 1. 登录 -------------------------------------------------------------
    for c in (alice, bob, carol):
        c.send(OP_LOGIN, {"name": c.label})
        resp = c.expect(OP_LOGIN_RESP, "登录响应", lambda o: o.get("ok") is True)
        c.expect(OP_SYS, "欢迎消息", lambda o: o.get("event") == "welcome")
        c.uid = resp["userId"]
    ok(f"三个客户端登录成功，userId={alice.uid},{bob.uid},{carol.uid}")

    # 2. 进房 -------------------------------------------------------------
    alice.send(OP_JOIN, {"room": f"1001{ROOM_SUFFIX}"})
    st = alice.expect(OP_ROOM_STATE, "alice 房间快照",
                      lambda o: o.get("room") == f"1001{ROOM_SUFFIX}" and len(o.get("members", [])) == 1)
    assert st["members"][0]["name"] == "alice", st
    ok("alice 进入 1001，成员列表 = [alice]")

    bob.send(OP_JOIN, {"room": f"1001{ROOM_SUFFIX}"})
    st = bob.expect(OP_ROOM_STATE, "bob 房间快照",
                    lambda o: o.get("room") == f"1001{ROOM_SUFFIX}" and len(o.get("members", [])) == 2)
    names = [m["name"] for m in st["members"]]
    assert names == ["alice", "bob"], names
    alice.expect(OP_SYS, "alice 收到 bob 进房通知",
                 lambda o: o.get("event") == "join" and o.get("name") == "bob")
    ok("bob 进入 1001，双方状态一致（成员按 id 有序: alice, bob）")

    carol.send(OP_JOIN, {"room": f"2002{ROOM_SUFFIX}"})
    carol.expect(OP_ROOM_STATE, "carol 房间快照",
                 lambda o: o.get("room") == f"2002{ROOM_SUFFIX}" and len(o.get("members", [])) == 1)
    alice.expect_nothing("2002 房间的进房通知（跨房隔离）", timeout=0.4)
    ok("carol 进入 2002，1001 房间收不到其进房通知")

    # 3. 同房间广播 -------------------------------------------------------
    text = "hello 世界"
    alice.send(OP_CHAT, {"text": text})
    a = alice.expect(OP_CHAT_PUSH, "alice 回显", lambda o: o.get("text") == text)
    b = bob.expect(OP_CHAT_PUSH, "bob 收到 alice 消息", lambda o: o.get("text") == text)
    assert a["from"] == alice.uid and b["from"] == alice.uid
    assert a["room"] == f"1001{ROOM_SUFFIX}" and b["room"] == f"1001{ROOM_SUFFIX}"
    carol.expect_nothing("2002 房间收到 1001 的消息（跨房隔离）", timeout=0.4)
    ok("1001 内广播正确：发送者回显 + 房友收到，跨房不可见（含 UTF-8 中文）")

    bob.send(OP_CHAT, {"text": "hi alice"})
    alice.expect(OP_CHAT_PUSH, "alice 收到 bob 消息",
                 lambda o: o.get("text") == "hi alice" and o.get("from") == bob.uid)
    bob.expect(OP_CHAT_PUSH, "bob 回显", lambda o: o.get("text") == "hi alice")
    ok("双向广播正确")

    # 4. 房间列表 / 心跳 ---------------------------------------------------
    alice.send(OP_ROOMS)
    rooms = alice.expect(OP_ROOM_LIST, "房间列表")
    mapping = {r["id"]: r["count"] for r in rooms["rooms"]}
    assert mapping.get(f"1001{ROOM_SUFFIX}") == 2 and mapping.get(f"2002{ROOM_SUFFIX}") == 1, mapping
    ok(f"房间列表正确: {mapping}")

    alice.send(OP_PING)
    alice.expect(OP_PONG, "Pong")
    ok("心跳 Ping/Pong 正常")

    # 5. 主动离开 ---------------------------------------------------------
    bob.send(OP_LEAVE)
    bob.expect(OP_SYS, "bob 离开确认", lambda o: o.get("event") == "left" and o.get("room") == f"1001{ROOM_SUFFIX}")
    alice.expect(OP_SYS, "alice 收到 bob 离开通知",
                 lambda o: o.get("event") == "leave" and o.get("name") == "bob")
    ok("主动离开：本人收到确认，房友收到 leave 通知")

    # 6. 切换房间（旧房间自动清空销毁）--------------------------------------
    alice.send(OP_JOIN, {"room": f"2002{ROOM_SUFFIX}"})
    st = alice.expect(OP_ROOM_STATE, "alice 切房快照",
                      lambda o: o.get("room") == f"2002{ROOM_SUFFIX}" and len(o.get("members", [])) == 2)
    carol.expect(OP_SYS, "carol 收到 alice 进房通知",
                 lambda o: o.get("event") == "join" and o.get("name") == "alice")
    ok("切房成功：旧房间 1001 无成员后自动销毁，2002 现有 alice+carol")

    alice.send(OP_ROOMS)
    rooms = alice.expect(OP_ROOM_LIST, "切房后房间列表")
    mapping = {r["id"]: r["count"] for r in rooms["rooms"]}
    assert f"1001{ROOM_SUFFIX}" not in mapping and mapping.get(f"2002{ROOM_SUFFIX}") == 2, mapping
    ok(f"空房间不再出现在列表中（滞留期内保留历史）: {mapping}")

    # 7. 异常断线 ---------------------------------------------------------
    alice.close()
    carol.expect(OP_SYS, "carol 收到 alice 断线通知",
                 lambda o: o.get("event") == "leave" and o.get("name") == "alice")
    ok("TCP 断链后，房友收到离线通知（服务端未崩溃）")

    # 8. 错误处理 ---------------------------------------------------------
    dave = Client(args.host, args.port, "dave")
    dave.send(OP_CHAT, {"text": "no login"})
    dave.expect(OP_ERROR, "未登录发言报错", lambda o: o.get("code") == "not_logged_in")
    dave.send(OP_LOGIN, {"name": "dave"})
    dave.expect(OP_LOGIN_RESP, "dave 登录")
    dave.expect(OP_SYS, "dave 欢迎消息")
    dave.send(OP_LOGIN, {"name": "dave2"})
    dave.expect(OP_ERROR, "重复登录报错", lambda o: o.get("code") == "already_logged_in")
    dave.send(OP_JOIN, {"room": "bad room!"})
    dave.expect(OP_ERROR, "非法房间号报错", lambda o: o.get("code") == "bad_room")
    dave.send(OP_CHAT, {"text": "x"})
    dave.expect(OP_ERROR, "未进房发言报错", lambda o: o.get("code") == "not_in_room")
    dave.send(OP_JOIN, {"room": f"3003{ROOM_SUFFIX}"})
    dave.expect(OP_ROOM_STATE, "dave 进房")
    dave.send(OP_CHAT, {"text": ""})
    dave.expect(OP_ERROR, "空消息报错", lambda o: o.get("code") == "bad_text")
    ok("错误码齐全：not_logged_in / already_logged_in / bad_room / not_in_room / bad_text")

    # 9. 非法帧：长度字段越界 -> protocol_error 并断开 ------------------------
    evil = Client(args.host, args.port, "evil")
    evil.send_raw(struct.pack(">I", 0xFFFFFFFF) + b"\x01")
    evil.expect(OP_ERROR, "非法长度报错", lambda o: o.get("code") == "protocol_error")
    try:
        r = evil.recv(timeout=2.0)
        assert r is None, f"非法帧后应断开，却收到 {r}"
    except ConnectionError:
        pass
    ok("非法帧长被拒绝并断开连接（不会 OOM/崩溃）")

    # 10. 历史消息（含空房滞留）---------------------------------------------
    h1 = Client(args.host, args.port, "h1")
    h1.send(OP_LOGIN, {"name": "h1"})
    h1.expect(OP_LOGIN_RESP, "h1 登录")
    h1.expect(OP_SYS, "h1 欢迎消息")
    h1.send(OP_JOIN, {"room": f"5005{ROOM_SUFFIX}"})
    h1.expect(OP_ROOM_STATE, "h1 创建 5005", lambda o: o.get("created") is True)
    h1.send(OP_CHAT, {"text": "first"})
    first = h1.expect(OP_CHAT_PUSH, "h1 第一条消息", lambda o: o.get("text") == "first")
    h1.send(OP_CHAT, {"text": "second"})
    second = h1.expect(OP_CHAT_PUSH, "h1 第二条消息", lambda o: o.get("text") == "second")
    assert second["seq"] > first["seq"] > 0, (first, second)
    ok(f"消息带全局递增序号: {first['seq']} -> {second['seq']}")

    h2 = Client(args.host, args.port, "h2")
    h2.send(OP_LOGIN, {"name": "h2"})
    h2.expect(OP_LOGIN_RESP, "h2 登录")
    h2.expect(OP_SYS, "h2 欢迎消息")
    h2.send(OP_JOIN, {"room": f"5005{ROOM_SUFFIX}"})
    h2.expect(OP_ROOM_STATE, "h2 进入 5005", lambda o: o.get("created") is False)
    h1.expect(OP_SYS, "h1 收到 h2 进房通知",
              lambda o: o.get("event") == "join" and o.get("name") == "h2")
    hist = h2.expect(OP_HISTORY, "h2 收到历史消息")
    assert [m["text"] for m in hist["messages"]] == ["first", "second"], hist
    assert all(m["seq"] > 0 for m in hist["messages"]), hist
    ok("后进房者收到完整历史（时间正序、带 seq）")

    h1.send(OP_LEAVE)
    h1.expect(OP_SYS, "h1 离开确认", lambda o: o.get("event") == "left")
    h2.expect(OP_SYS, "h2 收到 h1 离开", lambda o: o.get("event") == "leave")
    h2.send(OP_LEAVE)
    h2.expect(OP_SYS, "h2 离开确认", lambda o: o.get("event") == "left")

    # 房间已空，但处于滞留期：历史应当仍在
    h3 = Client(args.host, args.port, "h3")
    h3.send(OP_LOGIN, {"name": "h3"})
    h3.expect(OP_LOGIN_RESP, "h3 登录")
    h3.expect(OP_SYS, "h3 欢迎消息")
    h3.send(OP_JOIN, {"room": f"5005{ROOM_SUFFIX}"})
    h3.expect(OP_ROOM_STATE, "h3 重新进入空房", lambda o: o.get("created") is True)
    hist = h3.expect(OP_HISTORY, "空房滞留期内仍有历史")
    assert [m["text"] for m in hist["messages"]] == ["first", "second"], hist
    ok("空房间滞留期内保留历史，重新进入仍可看到上下文")

    # 11. 资料更新（含改名广播）---------------------------------------------
    frank = Client(args.host, args.port, "frank")
    frank.send(OP_LOGIN, {"name": "frank", "age": 20, "desc": "hi from frank"})
    frank.expect(OP_LOGIN_RESP, "带资料登录",
                 lambda o: o.get("age") == 20 and o.get("desc") == "hi from frank")
    frank.expect(OP_SYS, "frank 欢迎消息", lambda o: o.get("event") == "welcome")

    dave.send(OP_JOIN, {"room": f"4004{ROOM_SUFFIX}"})
    dave.expect(OP_ROOM_STATE, "dave 进入 4004", lambda o: o.get("room") == f"4004{ROOM_SUFFIX}")
    frank.send(OP_JOIN, {"room": f"4004{ROOM_SUFFIX}"})
    frank.expect(OP_ROOM_STATE, "frank 进入 4004",
                 lambda o: o.get("room") == f"4004{ROOM_SUFFIX}" and len(o.get("members", [])) == 2)
    dave.expect(OP_SYS, "dave 收到 frank 进房通知",
                lambda o: o.get("event") == "join" and o.get("name") == "frank")

    dave.send(OP_UPDATE, {"name": "david", "age": 31, "desc": "updated"})
    dave.expect(OP_PROFILE, "资料更新响应",
                lambda o: o.get("ok") is True and o.get("name") == "david"
                          and o.get("age") == 31 and o.get("desc") == "updated")
    frank.expect(OP_SYS, "房友收到改名通知",
                 lambda o: o.get("event") == "rename" and o.get("name") == "david")
    # 改名广播是给房间所有人的（含自己），这里消费掉，避免污染后面的断言
    dave.expect(OP_SYS, "自己也能收到改名广播",
                lambda o: o.get("event") == "rename" and o.get("name") == "david")
    ok("资料更新生效，改名会广播给同房间成员")

    frank.send(OP_UPDATE, {"age": 200})
    frank.expect(OP_ERROR, "非法年龄被拒", lambda o: o.get("code") == "bad_age")
    frank.send(OP_UPDATE, {"name": ""})
    frank.expect(OP_ERROR, "空姓名被拒", lambda o: o.get("code") == "bad_name")
    ok("资料字段校验：age 0..150、姓名非空")

    # 12. 语音消息 ---------------------------------------------------------
    # 服务端不解析音频内容，只校验大小，所以用假容器即可
    fake_voice = b"DPV1" + struct.pack("<I", 3000) + b"\x00" * 200
    voice_b64 = base64.b64encode(fake_voice).decode()

    dave.send(OP_VOICE, {"durationMs": 3000, "opus": voice_b64})
    push = frank.expect(OP_VOICE_PUSH, "房友收到语音推送",
                        lambda o: o.get("durationMs") == 3000 and o.get("name") == "david"
                                  and len(o.get("opus", "")) > 0)
    assert push.get("seq", 0) > 0, "语音消息应带 seq"
    # 语音和文本一样会回显给发送者（客户端靠它渲染自己的气泡），这里消费掉
    dave.expect(OP_VOICE_PUSH, "自己也能收到语音回显",
                lambda o: o.get("durationMs") == 3000 and o.get("seq") == push["seq"])
    ok(f"语音消息广播成功（{push['durationMs']}ms, {len(push['opus'])} base64 字符）")

    dave.send(OP_VOICE, {"durationMs": 0, "opus": voice_b64})
    dave.expect(OP_ERROR, "时长为 0 的语音被拒", lambda o: o.get("code") == "bad_voice")
    dave.send(OP_VOICE, {"durationMs": 3000, "opus": "not-base64!!"})
    dave.expect(OP_ERROR, "非法 base64 被拒", lambda o: o.get("code") == "bad_voice")
    dave.send(OP_VOICE, {"durationMs": 999999, "opus": voice_b64})
    dave.expect(OP_ERROR, "超长语音被拒", lambda o: o.get("code") == "bad_voice")
    ok("语音校验：时长上限、Base64 合法性、容器大小")

    # 语音也要进历史：换个人进房应该能收到
    grace = Client(args.host, args.port, "grace")
    grace.send(OP_LOGIN, {"name": "grace"})
    grace.expect(OP_LOGIN_RESP, "grace 登录")
    grace.expect(OP_SYS, "grace 欢迎消息", lambda o: o.get("event") == "welcome")
    grace.send(OP_JOIN, {"room": f"4004{ROOM_SUFFIX}"})
    grace.expect(OP_ROOM_STATE, "grace 进入 4004", lambda o: o.get("room") == f"4004{ROOM_SUFFIX}")

    got_voice = False
    for _ in range(6):          # 历史可能分多帧（文本一帧、每条语音一帧）
        hist = grace.expect(OP_HISTORY, "grace 收到历史")
        for m in hist.get("messages", []):
            if m.get("kind") == "voice":
                assert m.get("durationMs") == 3000, "历史里的语音时长应保留"
                assert len(m.get("opus", "")) > 0, "历史里的语音应带音频"
                got_voice = True
        if got_voice:
            break
    assert got_voice, "历史里应包含语音条目"
    ok("语音消息进入历史，后进房的人能看到并播放")

    # 13. 半包 / 粘包 ------------------------------------------------------
    erin = Client(args.host, args.port, "erin")
    frame = struct.pack(">I", 1 + len(b'{"name":"erin"}')) + bytes([OP_LOGIN]) + b'{"name":"erin"}'
    for i in range(len(frame)):          # 逐字节发送，模拟极端半包
        erin.send_raw(frame[i:i + 1])
        time.sleep(0.002)
    erin.expect(OP_LOGIN_RESP, "半包登录成功")
    erin.expect(OP_SYS, "半包欢迎消息")
    ok("逐字节半包重组正确（解帧器按长度前缀累积）")

    # 两个帧粘在一起发送
    f1 = struct.pack(">I", 1 + len(b"{}")) + bytes([OP_PING]) + b"{}"
    f2 = struct.pack(">I", 1 + len(b"{}")) + bytes([OP_ROOMS]) + b"{}"
    erin.send_raw(f1 + f2)
    erin.expect(OP_PONG, "粘包中的 Pong")
    erin.expect(OP_ROOM_LIST, "粘包中的房间列表")
    ok("两帧粘连发送可被正确拆分为两帧")

    for c in (bob, carol, dave, erin, frank, h1, h2, h3):
        c.close()

    print(f"\n== 全部通过：{PASSED} 项检查 ==")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as e:
        print(f"\n[FAIL] {e}")
        sys.exit(1)
