# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""Discord (its REST API, its gateway and its CDN) and GitHub as the bots reach them, over TLS on 127.0.0.1:443, for
test_gateway.sh, which runs a bot in a network namespace whose /etc/hosts sends discord.com, gateway.discord.gg,
cdn.discordapp.com and api.github.com here.

    python3 -I fake_discord.py CERT KEY DROP_AFTER|cards

The gateway sends HELLO, answers IDENTIFY with READY and each heartbeat with its ACK. Half a second after DROP_AFTER
(github: the first GitHub request, which is held open, never answered; ready: READY; replies: Tracer's answers to two
replies, below; zombie: below) it closes with 1001, as Discord going away, and from then on GET /gateway/bot gets its
connection closed with no answer.

zombie: a heartbeat every second. The first connection sends READY and the first kilobyte of a 60000-byte
MESSAGE_CREATE, then nothing, its heartbeats unanswered: a connection that stalls in the middle of a frame, which the
bot has to take for a zombie and leave. Each later one ACKs each heartbeat 0.3 s after it, as Discord's round trip, and
its second ACK is DROP_AFTER. Each connection, its READY, heartbeats and ACKs are lines, numbered by connection.

unready: a heartbeat every second, the first connection's unanswered; each later one is closed, with no CLOSE frame,
once the bot sends IDENTIFY. Nothing is dropped: the bot has to end on its own, its session never ready again.

replies: once READY is sent, a mention asking for help in channel 7 (which Tracer answers only with
TRACER_CHANNELS=all), then someone's reply that mentions the bot and brings no file, to message 50 of channel 6, which
has a ScarletNX log and no mention (the REST API gives it; the CDN, its log); once the bot has answered it, another
person's reply to the same message.

cards, for reiwimsy --fix-cards: the nightly channel 5 holds two of the bot's cards saying "Building" from five hours
ago, a commit inside a push (B) and that push's head (C), whose build failed; one of its cards saying something else,
and someone else's. GitHub answers what drawing C asks; Discord's channel listing, PATCH and DELETE work on that
channel. Each step is a line on stderr, each message a bot posts too.
"""

import asyncio
import base64
import hashlib
import json
import ssl
import struct
import sys
import time

CERT, KEY, DROP_AFTER = sys.argv[1], sys.argv[2], sys.argv[3]
if DROP_AFTER not in ("github", "ready", "replies", "cards", "zombie", "unready"):
    sys.exit(f"fake_discord.py: DROP_AFTER is {DROP_AFTER!r}, not github, ready, replies, cards, zombie or unready")
ME = {"id": "100", "username": "FakeBot", "discriminator": "0", "bot": True}
cue = asyncio.Event()  # what the drop waits for
posted = asyncio.Queue()  # each message a bot posts, its JSON
broken = False
connections = 0  # the gateway connections made so far
# the message the replies reply to: a person's, with a log, mentioning no one; the log, as the CDN serves it
LOG = (
    "[   0.00] ScarletNX - Fake Game\n"
    "[   0.00] build: ScarletNX main 1234abc (clang 23.1.3, Linux x86_64)\n"
    "[   0.30] game: 0100aaaabbbb0000, version 1.0.0\n"
    "[   1.00] [game] loading /home/someone/save.bin\n"
)
LOG_PATH = "/attachments/6/51/game.log"
LOGGED = {
    "id": "50",
    "channel_id": "6",
    "author": {"id": "7", "username": "someone"},
    "content": "it crashes at the title screen",
    "mentions": [],
    "attachments": [
        {
            "id": "51",
            "filename": "game.log",
            "size": len(LOG),
            "url": "https://cdn.discordapp.com" + LOG_PATH,
            "content_type": "text/plain; charset=utf-8",
        }
    ],
}


def reply(message, author):
    """a reply to message 50 by author, mentioning the bot and nothing else, as MESSAGE_CREATE's data"""
    return {
        "id": message,
        "channel_id": "6",
        "guild_id": "5",
        "type": 19,
        "author": {"id": author, "username": "helper" + author},
        "member": {"roles": []},
        "content": "<@" + ME["id"] + ">",
        "mentions": [ME],
        "attachments": [],
        "message_reference": {"message_id": "50", "channel_id": "6", "guild_id": "5"},
    }


def say(line):
    print(line, file=sys.stderr, flush=True)


async def request_of(reader):
    head = (await reader.readuntil(b"\r\n\r\n")).decode("latin-1").split("\r\n")
    method, target, _ = head[0].split(" ", 2)
    headers = {k.strip().lower(): v.strip() for k, v in (h.split(":", 1) for h in head[1:] if ":" in h)}
    body = b""
    if int(headers.get("content-length", "0")):
        body = await reader.readexactly(int(headers["content-length"]))
    return method, target, headers, body


def answer(writer, obj, status="200 OK", kind="application/json"):
    data = obj.encode() if isinstance(obj, str) else json.dumps(obj).encode()
    writer.write(
        b"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n" % (status.encode(), kind.encode(), len(data))
        + data
    )


# ---- the cards scenario ----
NOW = time.time()
B, C, A = "b" * 40, "c" * 40, "a" * 40


def iso(when):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(when))


def posted_id(when, n):
    return str(((int(when * 1000) - 1420070400000) << 22) + n)


def building(sha):
    return {
        "title": f"Nightly 2026-11-04 ({sha[:7]})",
        "description": f"Building main at [`{sha[:7]}`]"
        f"(https://github.com/owner/repo/commit/{sha}), committed 2026-11-04.",
    }


# the channel, newest first
channel = [
    {"id": posted_id(NOW - 5 * 3600 + 80, 0), "author": {"id": "999"}, "embeds": [building("2" * 40)]},
    {"id": posted_id(NOW - 5 * 3600 + 70, 0), "author": ME, "embeds": [{"description": "Built from main at x."}]},
    {"id": posted_id(NOW - 5 * 3600 + 62, 0), "author": ME, "embeds": [building(C)]},
    {"id": posted_id(NOW - 5 * 3600 + 60, 0), "author": ME, "embeds": [building(B)]},
]
GITHUB = {
    "/repos/owner/repo/commits/main": {"sha": A},
    "/repos/owner/repo/actions/workflows/build.yml/runs?branch=main&event=push&per_page=100&page=1": {
        "workflow_runs": [
            {"head_sha": A, "created_at": iso(NOW - 3600)},
            {"head_sha": C, "created_at": iso(NOW - 5 * 3600)},
        ]
    },
    f"/repos/owner/repo/commits/{C}": {"commit": {"committer": {"date": "2026-11-04T22:00:00Z"}}},
    f"/repos/owner/repo/actions/workflows/build.yml/runs?head_sha={C}&per_page=100&page=1": {
        "workflow_runs": [
            {
                "id": 71,
                "event": "push",
                "status": "completed",
                "conclusion": "failure",
                "created_at": iso(NOW - 5 * 3600),
                "html_url": "https://r/71",
            }
        ]
    },
    "/repos/owner/repo/actions/runs/71/jobs?per_page=100&page=1": {"jobs": []},
    "/repos/owner/nightly/releases?per_page=100&page=1": [],
}


def cards_discord(writer, method, target, body):
    """the nightly channel's listing, its PATCH and DELETE; True when target was one of them"""
    path, _, query = target.partition("?")
    if path == "/api/v10/channels/5/messages" and method == "GET":
        args = dict(kv.split("=", 1) for kv in query.split("&") if "=" in kv)
        before = int(args.get("before", "0")) or 1 << 64
        answer(writer, [m for m in channel if int(m["id"]) < before][: int(args.get("limit", "50"))])
        return True
    if path.startswith("/api/v10/channels/5/messages/"):
        mid = path.rsplit("/", 1)[1]
        held = [m for m in channel if m["id"] == mid]
        if not held:
            answer(writer, {"message": "Unknown Message", "code": 10008}, "404 Not Found")
        elif method == "DELETE":
            channel.remove(held[0])
            writer.write(b"HTTP/1.1 204 No Content\r\n\r\n")
        elif method == "PATCH":
            held[0]["embeds"] = json.loads(body)["embeds"]
            answer(writer, held[0])
        else:
            return False
        say(f"discord: {method} message {mid[-4:]}, of {held[0]['embeds'][0]['description'][:30] if held else 'none'}")
        return True
    return False


def frame(opcode, payload, size=None):
    """a frame of payload; size, when given, is the length its header announces, of which payload is the start"""
    n = len(payload) if size is None else size
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
    global connections
    connections += 1
    n = connections
    acks = 0
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

    async def replies():
        elsewhere = dict(reply("70", "10"), channel_id="7", content="<@" + ME["id"] + "> help")
        del elsewhere["message_reference"]
        send({"op": 0, "t": "MESSAGE_CREATE", "s": 2, "d": elsewhere})
        say("gateway: MESSAGE_CREATE 70, a mention asking for help in channel 7")
        for seq, (message, author) in enumerate((("60", "8"), ("61", "9")), start=3):
            send({"op": 0, "t": "MESSAGE_CREATE", "s": seq, "d": reply(message, author)})
            await writer.drain()
            say(f"gateway: MESSAGE_CREATE {message}, a reply by {author} to message 50")
            try:  # the bot answers in order: an answer to 70, when it gives one, comes first
                while True:
                    said = await asyncio.wait_for(posted.get(), 30)
                    say(f"discord: answered {json.dumps(said)}")
                    if said.get("message_reference", {}).get("message_id") == message:
                        break
            except TimeoutError:
                say(f"discord: no answer to {message} in 30 s")
                break
        cue.set()

    async def ack():
        nonlocal acks
        await asyncio.sleep(0.3)
        if writer.is_closing():  # the bot left this connection before its ACK
            return
        send({"op": 11, "d": None, "s": None, "t": None})
        try:
            await writer.drain()
        except ConnectionError:
            return
        acks += 1
        say(f"gateway: connection {n}: ACK {acks}")
        if acks == 2:
            cue.set()

    beating = DROP_AFTER in ("zombie", "unready")  # a heartbeat a second, each connection numbered
    if beating:
        say(f"gateway: connection {n}")
    send({"op": 10, "d": {"heartbeat_interval": 1000 if beating else 41250}, "s": None, "t": None})
    dropping = asyncio.ensure_future(drop())
    replying = None
    try:
        while True:
            opcode, data = await frame_of(reader)
            if opcode == 8:
                break
            if opcode != 1:
                continue
            op = json.loads(data).get("op")
            if op == 2 and DROP_AFTER == "unready" and n > 1:
                say(f"gateway: connection {n}: IDENTIFY, closed with no CLOSE frame")
                break
            if op == 2:
                ready = {"v": 10, "user": ME, "guilds": [], "session_id": "fake", "shard": [0, 1]}
                ready["resume_gateway_url"] = "wss://gateway.discord.gg"
                ready["application"] = {"id": ME["id"], "flags": 0}
                send({"op": 0, "t": "READY", "s": 1, "d": ready})
                say(f"gateway: connection {n}: READY" if beating else "gateway: READY")
                if DROP_AFTER == "zombie" and n == 1:
                    start = json.dumps({"op": 0, "t": "MESSAGE_CREATE", "s": 2, "d": {"content": "x" * 60000}})
                    writer.write(frame(1, start[:1000].encode(), 60000))
                    say("gateway: connection 1: 1000 bytes of a 60000-byte frame, and nothing more")
                elif DROP_AFTER == "ready":
                    cue.set()
                elif DROP_AFTER == "replies" and not replying:
                    replying = asyncio.ensure_future(replies())
            elif op == 1 and beating:
                say(f"gateway: connection {n}: heartbeat" + (", unanswered" if n == 1 else ""))
                if n > 1:
                    asyncio.ensure_future(ack())
            elif op == 1:
                send({"op": 11, "d": None, "s": None, "t": None})
            await writer.drain()
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    dropping.cancel()
    if replying:
        replying.cancel()


async def serve(reader, writer):
    try:
        while True:
            method, target, headers, body = await request_of(reader)
            host = headers.get("host", "").split(":")[0]
            if host == "gateway.discord.gg":
                return await gateway(reader, writer, headers)
            if DROP_AFTER == "cards" and host == "api.github.com":
                if target in GITHUB:
                    answer(writer, GITHUB[target])
                else:
                    say(f"github: {method} {target}: no answer here, 404")
                    answer(writer, {"message": "Not Found"}, "404 Not Found")
            elif DROP_AFTER == "cards" and cards_discord(writer, method, target, body):
                pass
            elif host == "api.github.com":
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
            elif host == "cdn.discordapp.com":
                say(f"cdn: {method} {target}")
                answer(writer, LOG if target == LOG_PATH else "", kind="text/plain")
            elif target.startswith("/api/v10/users/@me"):
                answer(writer, ME)
            elif target.startswith("/api/v10/applications/@me"):
                answer(writer, {"id": ME["id"], "flags": 1 << 19})  # the Message Content intent, limited
            elif method == "GET" and target == "/api/v10/channels/6/messages/50":
                say(f"discord: {method} {target}")
                answer(writer, LOGGED)
            elif method == "POST" and target in ("/api/v10/channels/6/messages", "/api/v10/channels/7/messages"):
                posted.put_nowait(json.loads(body))
                answer(writer, {"id": str(900 + posted.qsize())})
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
