#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""连接抖动测试：反复建连/断连，检查服务端是否泄漏 fd 与内存。

一半连接正常关闭，一半用 SO_LINGER=0 强制 RST，覆盖两种断链路径。

用法:
    python3 scripts/churn_test.py --rounds 100 --batch 20
"""
import argparse
import asyncio
import json
import socket
import struct
import sys
import time

OP_LOGIN = 0x01
OP_JOIN = 0x02
OP_CHAT = 0x04


async def one(idx, cfg):
    reader, writer = await asyncio.open_connection(cfg.host, cfg.port)

    def frame(op, obj):
        payload = json.dumps(obj).encode()
        return struct.pack(">I", len(payload) + 1) + bytes([op]) + payload

    writer.write(frame(OP_LOGIN, {"name": f"c{idx}"}))
    writer.write(frame(OP_JOIN, {"room": f"r{idx % 7}"}))
    writer.write(frame(OP_CHAT, {"text": "ping"}))
    await writer.drain()

    # 读掉至少一帧，确保服务端已处理
    try:
        await asyncio.wait_for(reader.readexactly(5), 5)
    except Exception:
        pass

    if idx % 2 == 0:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass
    else:
        sock = writer.get_extra_info("socket")
        if sock is not None:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        writer.transport.abort()   # SO_LINGER=0 + abort => 直接发 RST


async def main_async(cfg):
    t0 = time.time()
    done = 0
    for r in range(cfg.rounds):
        await asyncio.gather(*[one(r * cfg.batch + i, cfg) for i in range(cfg.batch)])
        done += cfg.batch
        if (r + 1) % 20 == 0:
            print(f"  {done} 次建连/断连 ... {time.time() - t0:.1f}s")
    print(f"完成 {done} 次连接抖动，用时 {time.time() - t0:.2f}s")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--rounds", type=int, default=100)
    ap.add_argument("--batch", type=int, default=20)
    cfg = ap.parse_args()
    return asyncio.run(main_async(cfg))


if __name__ == "__main__":
    sys.exit(main())
