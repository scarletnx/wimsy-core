# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""Discord (its REST API and its gateway) and GitHub as the bots reach them, over TLS on 127.0.0.1:443, for
test_gateway.sh, which runs a bot in a network namespace whose /etc/hosts sends discord.com, gateway.discord.gg and
api.github.com here.

    python3 -I fake_discord.py CERT KEY DROP_AFTER

The gateway sends HELLO, answers IDENTIFY with READY and each heartbeat with its ACK. Half a second after DROP_AFTER
(github: the first GitHub request, which is held open, never answered; ready: READY) it closes with 1001, as Discord
going away, and from then on GET /gateway/bot gets its connection closed with no answer. Each step is a line on stderr.
"""

import asyncio
import base64
import hashlib
import json
import ssl
import struct
import sys

CERT, KEY, DROP_AFTER = sys.argv[1], sys.argv[2], sys.argv[3]
if DROP_AFTER not in ("github", "ready"):
    sys.exit(f"fake_discord.py: DROP_AFTER is {DROP_AFTER!r}, not github or ready")
ME = {"id": "100", "username": "FakeBot", "discriminator": "0", "bot": True}
cue = asyncio.Event()  # what the drop waits for
broken = False


def say(line):
    print(line, file=sys.stderr, flush=True)


async def request_of(reader):
    head = (await reader.readuntil(b"\r\n\r\n")).decode("latin-1").split("\r\n")
    method, target, _ = head[0].split(" ", 2)
    headers = {k.strip().lower(): v.strip() for k, v in (h.split(":", 1) for h in head[1:] if ":" in h)}
    if int(headers.get("content-length", "0")):
        await reader.readexactly(int(headers["content-length"]))
    return method, target, headers


def answer(writer, obj):
    data = json.dumps(obj).encode()
    writer.write(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n" % len(data) + data)


def frame(opcode, payload):
    n = len(payload)
    if n < 126:
        return struct.pack("!BB", 0x80 | opcode, n) + payload
    return struct.pack("!BBH", 0x80 | opcode, 126, n) + payload


async def frame_of(reader):
    b0, b1 = await reader.readexactly(2)
    n = b1 & 0x7F
    if n == 126:
        (n,) = struct.unpack("!H", await reader.readexactly(2))
    elif n == 127:
        (n,) = struct.unpack("!Q", await reader.readexactly(8))
    mask = await reader.readexactly(4) if b1 & 0x80 else b"\0\0\0\0"
    return b0 & 0x0F, bytes(c ^ mask[i % 4] for i, c in enumerate(await reader.readexactly(n)))


async def gateway(reader, writer, headers):
    key = headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    accept = base64.b64encode(hashlib.sha1(key.encode()).digest()).decode()
    writer.write(
        b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        b"Sec-WebSocket-Accept: %s\r\n\r\n" % accept.encode()
    )

    def send(obj):
        writer.write(frame(1, json.dumps(obj).encode()))

    async def drop():
        global broken
        await cue.wait()
        await asyncio.sleep(0.5)
        broken = True
        say("gateway: closed with 1001, GET /gateway/bot answered by a closed connection from now on")
        writer.write(frame(8, struct.pack("!H", 1001)))
        await writer.drain()
        writer.close()

    send({"op": 10, "d": {"heartbeat_interval": 41250}, "s": None, "t": None})
    dropping = asyncio.ensure_future(drop())
    try:
        while True:
            opcode, data = await frame_of(reader)
            if opcode == 8:
                break
            if opcode != 1:
                continue
            op = json.loads(data).get("op")
            if op == 2:
                ready = {"v": 10, "user": ME, "guilds": [], "session_id": "fake", "shard": [0, 1]}
                ready["resume_gateway_url"] = "wss://gateway.discord.gg"
                ready["application"] = {"id": ME["id"], "flags": 0}
                send({"op": 0, "t": "READY", "s": 1, "d": ready})
                say("gateway: READY")
                if DROP_AFTER == "ready":
                    cue.set()
            elif op == 1:
                send({"op": 11, "d": None, "s": None, "t": None})
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    dropping.cancel()


async def serve(reader, writer):
    try:
        while True:
            method, target, headers = await request_of(reader)
            host = headers.get("host", "").split(":")[0]
            if host == "gateway.discord.gg":
                return await gateway(reader, writer, headers)
            if host == "api.github.com":
                say(f"github: {method} {target} held")
                if DROP_AFTER == "github":
                    cue.set()
                await reader.read()  # until the client goes
                return
            if target.startswith("/api/v10/gateway/bot"):
                if broken:
                    say("discord: GET /gateway/bot, its connection closed")
                    return
                answer(
                    writer,
                    {
                        "url": "wss://gateway.discord.gg",
                        "shards": 1,
                        "session_start_limit": {
                            "total": 1000,
                            "remaining": 999,
                            "reset_after": 14400000,
                            "max_concurrency": 1,
                        },
                    },
                )
            elif target.startswith("/api/v10/users/@me"):
                answer(writer, ME)
            elif "/emojis" in target:
                answer(writer, {"items": []})
            else:
                answer(writer, [] if method == "GET" else {"id": "1"})
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError, ssl.SSLError):
        pass
    finally:
        writer.close()


async def main():
    tls = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
    tls.load_cert_chain(CERT, KEY)
    tls.set_alpn_protocols(["http/1.1"])
    server = await asyncio.start_server(serve, "127.0.0.1", 443, ssl=tls)
    say("listening on 127.0.0.1:443")
    async with server:
        await server.serve_forever()


asyncio.run(main())
