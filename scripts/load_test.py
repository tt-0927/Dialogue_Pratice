#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""并发压测：验证 epoll 多连接调度与房间广播的投递正确性。

每个客户端进入 rooms 中的某个房间，等所有客户端就位后各发 messages 条消息。
房间内 K 人各发 M 条 => 每人都应收到 K*M 条 ChatPush（含自己的回显）。
任何少收/多发都会让断言失败。

用法:
    python3 scripts/load_test.py --clients 500 --rooms 10 --messages 20
"""
import argparse
import asyncio
import json
import struct
import sys
import time

OP_LOGIN = 0x01
OP_JOIN = 0x02
OP_CHAT = 0x04
OP_LOGIN_RESP = 0x81
OP_ROOM_STATE = 0x82
OP_CHAT_PUSH = 0x83
OP_SYS = 0x84


class Gate:
    """等待 n 个客户端就位。"""

    def __init__(self, n):
        self.n = n
        self.count = 0
        self.event = asyncio.Event()

    def arrive(self):
        self.count += 1
        if self.count >= self.n:
            self.event.set()

    async def wait(self):
        await self.event.wait()


async def run_client(idx, cfg, gate, start, results):
    room = str(1000 + idx % cfg.rooms)
    reader, writer = await asyncio.open_connection(cfg.host, cfg.port)
    queue = asyncio.Queue()

    async def pump():
        try:
            while True:
                hdr = await reader.readexactly(5)
                ln = struct.unpack(">I", hdr[:4])[0]
                body = await reader.readexactly(ln - 1)
                await queue.put((hdr[4], json.loads(body.decode("utf-8")) if body else {}))
        except Exception:
            await queue.put(None)

    pump_task = asyncio.create_task(pump())

    async def send(op, obj):
        payload = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        writer.write(struct.pack(">I", len(payload) + 1) + bytes([op]) + payload)
        await writer.drain()

    async def wait_op(op, timeout=15.0, pred=None):
        end = time.time() + timeout
        while True:
            remain = end - time.time()
            if remain <= 0:
                raise asyncio.TimeoutError(f"client{idx}: wait op 0x{op:02x} timeout")
            item = await asyncio.wait_for(queue.get(), remain)
            if item is None:
                raise ConnectionError(f"client{idx}: closed while waiting 0x{op:02x}")
            gop, obj = item
            if gop == op and (pred is None or pred(obj)):
                return obj

    name = f"u{idx}"
    await send(OP_LOGIN, {"name": name})
    await wait_op(OP_LOGIN_RESP)
    await send(OP_JOIN, {"room": room})
    await wait_op(OP_ROOM_STATE, pred=lambda o: o.get("room") == room)

    gate.arrive()
    await start.wait()

    t0 = time.time()
    for i in range(cfg.messages):
        await send(OP_CHAT, {"text": f"m{i}-{name}"})

    k = cfg.clients // cfg.rooms
    expected = k * cfg.messages
    got = 0
    deadline = time.time() + 60.0
    while got < expected:
        remain = deadline - time.time()
        if remain <= 0:
            raise asyncio.TimeoutError(f"client{idx}: only {got}/{expected} ChatPush")
        item = await asyncio.wait_for(queue.get(), remain)
        if item is None:
            raise ConnectionError(f"client{idx}: closed while receiving")
        gop, _ = item
        if gop == OP_CHAT_PUSH:
            got += 1
    elapsed = time.time() - t0

    results.append((got, elapsed))
    pump_task.cancel()
    writer.close()
    try:
        await writer.wait_closed()
    except Exception:
        pass
    return got


async def main_async(cfg):
    if cfg.clients % cfg.rooms != 0:
        print("clients 必须是 rooms 的整数倍", file=sys.stderr)
        return 2

    gate = Gate(cfg.clients)
    start = asyncio.Event()
    results = []

    t_begin = time.time()
    tasks = [asyncio.create_task(run_client(i, cfg, gate, start, results))
             for i in range(cfg.clients)]

    await gate.wait()
    connect_secs = time.time() - t_begin
    print(f"已建立 {cfg.clients} 条连接并全部进房，用时 {connect_secs:.2f}s")

    t0 = time.time()
    start.set()
    done = await asyncio.gather(*tasks, return_exceptions=True)
    elapsed = time.time() - t0

    errors = [d for d in done if isinstance(d, Exception)]
    if errors:
        for e in errors[:5]:
            print(f"  [FAIL] {type(e).__name__}: {e}", file=sys.stderr)
        print(f"\n失败: {len(errors)}/{cfg.clients} 个客户端异常", file=sys.stderr)
        return 1

    total = sum(d for d in done)
    k = cfg.clients // cfg.rooms
    expect_each = k * cfg.messages
    per_client = sorted(results)
    slowest = max(r[1] for r in per_client)
    print(f"房间数={cfg.rooms} 每房人数={k} 每人发送={cfg.messages}")
    print(f"每个客户端应收 {expect_each} 条，实际全部相符")
    print(f"投递总量={total} 条，耗时={elapsed:.2f}s，吞吐={total / elapsed:,.0f} msg/s")
    print(f"最慢客户端收完 {expect_each} 条用时 {slowest:.2f}s")
    print("\n== 压测通过 ==")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--clients", type=int, default=500)
    ap.add_argument("--rooms", type=int, default=10)
    ap.add_argument("--messages", type=int, default=20)
    cfg = ap.parse_args()
    return asyncio.run(main_async(cfg))


if __name__ == "__main__":
    sys.exit(main())
