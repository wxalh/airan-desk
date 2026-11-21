#!/usr/bin/env python3
"""Optional authenticated fnOS file/terminal smoke test (aiortc, websockets).

Read the device password from AIRAN_RTC_PASSWORD or one line of stdin. This
probe lists only the app home/state and executes a fixed printf/id command.
It never accepts an arbitrary remote command, downloads files, or logs secrets.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import contextlib
import hashlib
import json
import logging
import os
from pathlib import PurePosixPath
import re
import sys
import uuid
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit


class ProbeError(Exception):
    pass


def signaling_url(raw: str, client_id: str) -> str:
    parts = urlsplit(raw)
    if parts.scheme not in {"ws", "wss"} or not parts.hostname or parts.username or parts.fragment:
        raise ProbeError("Use a ws:// or wss:// signaling URL without credentials or fragments")
    query = [(k, v) for k, v in parse_qsl(parts.query) if k not in {"sessionId", "hostname", "installId"}]
    query += [("sessionId", client_id), ("hostname", "fnos-rtc-smoke"), ("installId", str(uuid.uuid4()))]
    return urlunsplit(parts._replace(query=urlencode(query)))


def selected_path(value: str) -> str:
    if value == "home":
        return value
    path = PurePosixPath(value)
    allowed = value == "/var/apps/airan-desk/var" or value.startswith("/var/apps/airan-desk/var/")
    allowed |= bool(re.fullmatch(r"/vol[0-9]+/@appdata/airan-desk(?:/[^/]+)*", value))
    if not path.is_absolute() or ".." in path.parts or not allowed:
        raise argparse.ArgumentTypeError("Select home or an airan-desk fnOS app state path")
    return value


class Probe:
    def __init__(self, args: argparse.Namespace, password: str, rtc: object):
        self.args = args
        self.password_digest = hashlib.md5(password.encode("utf-8")).hexdigest().upper()
        self.client_id = str(uuid.uuid4()).upper()
        self.session_id = str(uuid.uuid4()).upper()
        self.rtc = rtc
        self.peer = None
        self.ws = None
        self.channels = {}
        self.opened = asyncio.Event()
        self.messages = asyncio.Queue(maxsize=1024)
        self.failure = asyncio.get_running_loop().create_future()
        self.remote_description = False
        self.pending_candidates = []
        self.terminal_request = str(uuid.uuid4())
        self.terminal_started = False
        self.closing = False
        self.sequence = 0
        self.stage = "signaling"

    def fail(self, message: str) -> None:
        if not self.closing and not self.failure.done():
            self.failure.set_result(message)

    async def checked(self, awaitable, timeout: float | None = None):
        task = asyncio.ensure_future(awaitable)
        try:
            done, _ = await asyncio.wait(
                [task, self.failure], timeout=timeout or self.args.timeout,
                return_when=asyncio.FIRST_COMPLETED,
            )
            if self.failure in done:
                raise ProbeError(self.failure.result())
            if task not in done:
                raise ProbeError("Timed out during " + self.stage)
            return task.result()
        finally:
            if not task.done():
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task

    async def signal(self, kind: str, **fields) -> None:
        await self.ws.send(json.dumps({
            "role": "ctl", "type": kind, "sender": self.client_id,
            "receiver": self.args.device_id, "sessionId": self.session_id, **fields,
        }, separators=(",", ":")))

    def channel_send(self, label: str, message: dict) -> None:
        channel = self.channels.get(label)
        if channel is None or channel.readyState != "open":
            raise ProbeError("Required data channel is closed")
        channel.send(json.dumps(message, separators=(",", ":")))

    def on_channel(self, channel) -> None:
        self.channels[channel.label] = channel

        @channel.on("open")
        def opened():
            if channel.label == "file_text_airan":
                self.opened.set()

        @channel.on("close")
        def closed():
            if channel.label == "file_text_airan":
                self.fail("Remote file/terminal channel closed")

        @channel.on("message")
        def message(payload):
            if not isinstance(payload, str) or len(payload) > 2 * 1024 * 1024:
                return
            try:
                item = json.loads(payload)
            except (ValueError, TypeError):
                self.fail("Invalid JSON data channel response")
                return
            if not isinstance(item, dict):
                return
            if channel.label == "heartbeat_airan":
                if item.get("msgType") == "session_heartbeat" and item.get("action") == "ping":
                    self.sequence += 1
                    channel.send(json.dumps({"msgType": "session_heartbeat", "action": "pong", "sequence": self.sequence}))
            elif channel.label == "file_text_airan":
                try:
                    self.messages.put_nowait(item)
                except asyncio.QueueFull:
                    self.fail("Remote response queue limit exceeded")

        if channel.readyState == "open":
            opened()

    async def add_candidate(self, item: dict) -> None:
        candidate = item.get("data", "")
        if not isinstance(candidate, str) or not candidate or len(candidate) > 16384:
            raise ProbeError("Invalid remote ICE candidate")
        candidate = re.sub(r"^(?:a=)?candidate:", "", candidate)
        parsed = self.rtc["candidate_from_sdp"](candidate)
        parsed.sdpMid = str(item.get("mid", "0"))
        await self.peer.addIceCandidate(parsed)

    async def signaling(self) -> None:
        try:
            async for payload in self.ws:
                if payload == "@heart":
                    continue
                item = json.loads(payload)
                if not isinstance(item, dict):
                    continue
                kind = item.get("type")
                if item.get("sender") == "server":
                    if kind in {"error", "deviceIdConflict"}:
                        raise ProbeError("Signaling server rejected the session or device is offline")
                    continue
                if item.get("sender") != self.args.device_id or item.get("receiver") != self.client_id:
                    continue
                if item.get("sessionId") not in {None, "", self.session_id}:
                    continue
                if kind in {"error", "session_disconnect", "peer_disconnect"}:
                    raise ProbeError("Remote rejected or disconnected the authenticated session")
                if item.get("sessionId") != self.session_id or item.get("role") != "cli":
                    continue
                if kind == "offer":
                    if self.remote_description:
                        raise ProbeError("Unexpected second SDP offer")
                    sdp = item.get("data")
                    if not isinstance(sdp, str) or len(sdp) > 4 * 1024 * 1024:
                        raise ProbeError("Invalid remote SDP offer")
                    self.stage = "WebRTC negotiation"
                    await self.peer.setRemoteDescription(self.rtc["RTCSessionDescription"](sdp=sdp, type="offer"))
                    self.remote_description = True
                    for pending in self.pending_candidates:
                        await self.add_candidate(pending)
                    self.pending_candidates.clear()
                    await self.peer.setLocalDescription(await self.peer.createAnswer())
                    # aiortc gathers candidates into the answer before returning.
                    await self.signal("answer", data=self.peer.localDescription.sdp)
                elif kind == "candidate":
                    if self.remote_description:
                        await self.add_candidate(item)
                    elif len(self.pending_candidates) < 256:
                        self.pending_candidates.append(item)
                    else:
                        raise ProbeError("Remote ICE candidate queue limit exceeded")
            self.fail("Signaling connection closed")
        except asyncio.CancelledError:
            raise
        except ProbeError as exc:
            self.fail(str(exc))
        except Exception:
            self.fail("Signaling or SDP processing failed")

    async def heartbeat(self) -> None:
        try:
            while True:
                await asyncio.sleep(3)
                await self.ws.send("@heart")
                channel = self.channels.get("heartbeat_airan")
                if channel and channel.readyState == "open":
                    self.sequence += 1
                    channel.send(json.dumps({"msgType": "session_heartbeat", "action": "ping", "sequence": self.sequence}))
        except asyncio.CancelledError:
            raise
        except Exception:
            self.fail("Heartbeat transport failed")

    async def response(self, kinds: set[str], request_id: str) -> dict:
        deadline = asyncio.get_running_loop().time() + self.args.timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise ProbeError("Timed out during " + self.stage)
            item = await self.checked(self.messages.get(), remaining)
            if item.get("requestId") == request_id and item.get("msgType") in kinds:
                return item

    async def operations(self) -> dict:
        await self.checked(self.opened.wait())
        self.stage = "app directory listing"
        request = str(uuid.uuid4())
        self.channel_send("file_text_airan", {"role": "ctl", "msgType": "file_list", "path": self.args.path, "requestId": request})
        listing = await self.response({"file_list"}, request)
        files = listing.get("folderFiles")
        if listing.get("status") is not True or not isinstance(files, list):
            raise ProbeError("Remote app directory listing failed")
        try:
            if selected_path(listing.get("path", "")) == "home":
                raise argparse.ArgumentTypeError("Expected an absolute app home path")
        except (argparse.ArgumentTypeError, TypeError):
            raise ProbeError("Remote directory is outside the selected application state") from None
        if not all(isinstance(entry, dict) and isinstance(entry.get("name"), str) and
                   isinstance(entry.get("is_dir"), bool) and isinstance(entry.get("file_size"), (int, float))
                   for entry in files):
            raise ProbeError("Remote file listing has invalid entries")
        self.stage = "terminal startup"
        self.terminal_started = True
        self.channel_send("file_text_airan", {"role": "ctl", "msgType": "terminal_start", "requestId": self.terminal_request, "cols": 120, "rows": 30})
        info = await self.response({"terminal_info", "terminal_error", "terminal_closed"}, self.terminal_request)
        if info.get("msgType") != "terminal_info" or info.get("os") != "linux":
            raise ProbeError("Remote Linux terminal did not start")
        self.stage = "fixed terminal identity command"
        marker = "FNOS_RTC_" + uuid.uuid4().hex
        # Literal markers are printed with %s, keeping PTY command echo from
        # satisfying the line-anchored response check. No caller command runs.
        command = ("printf '\\n%s\\n' '" + marker + "'; id -u; id -un; "
                   "printf '%s\\n' '" + marker + "_END'\n")
        self.channel_send("file_text_airan", {"role": "ctl", "msgType": "terminal_input", "requestId": self.terminal_request,
                                             "data": base64.b64encode(command.encode()).decode()})
        output = bytearray()
        deadline = asyncio.get_running_loop().time() + self.args.timeout
        matched = None
        while not matched:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise ProbeError("Timed out waiting for fixed terminal command output")
            item = await self.checked(self.messages.get(), remaining)
            if item.get("requestId") != self.terminal_request:
                continue
            if item.get("msgType") in {"terminal_error", "terminal_closed"}:
                raise ProbeError("Remote terminal ended before command verification")
            if item.get("msgType") != "terminal_output":
                continue
            try:
                output.extend(base64.b64decode(item.get("data", ""), validate=True))
            except (ValueError, TypeError):
                raise ProbeError("Invalid terminal output encoding") from None
            if len(output) > 1024 * 1024:
                raise ProbeError("Terminal output limit exceeded")
            cleaned = re.sub(rb"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)", b"", bytes(output))
            cleaned = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", cleaned).replace(b"\r", b"")
            expression = rb"(?m)^" + marker.encode() + rb"\n([0-9]+)\n([^\n]+)\n" + marker.encode() + rb"_END(?:\n|$)"
            matched = re.search(expression, cleaned)
        uid = int(matched.group(1))
        username = matched.group(2).decode("utf-8", "replace")
        if uid == 0 or username.strip() == "root":
            raise ProbeError("Remote terminal unexpectedly runs as root")
        self.stage = "terminal exit confirmation"
        self.channel_send("file_text_airan", {"role": "ctl", "msgType": "terminal_input", "requestId": self.terminal_request,
                                             "data": base64.b64encode(b"exit\n").decode()})
        closed = await self.response({"terminal_closed", "terminal_error"}, self.terminal_request)
        if closed.get("msgType") != "terminal_closed" or closed.get("status") != 0:
            raise ProbeError("Remote terminal did not exit cleanly")
        self.terminal_started = False
        return {"ok": True, "path": listing.get("path"), "files": files,
                "terminal": {"os": "linux", "mode": info.get("terminalMode"), "uid": uid,
                             "user": username, "fixed_command_verified": True, "closed": True}}

    async def run(self) -> dict:
        ice_servers = []
        if self.args.turn_host:
            ice_servers.append(self.rtc["RTCIceServer"](
                urls=[f"turn:{self.args.turn_host}:{self.args.turn_port}?transport=udp"],
                username=self.args.turn_user, credential=os.environ.get("AIRAN_RTC_TURN_PASSWORD", ""),
            ))
        self.peer = self.rtc["RTCPeerConnection"](self.rtc["RTCConfiguration"](iceServers=ice_servers))
        self.peer.on("datachannel", self.on_channel)

        @self.peer.on("connectionstatechange")
        def state():
            if self.peer.connectionState in {"failed", "closed"}:
                self.fail("WebRTC connection failed or closed")

        tasks = []
        try:
            self.ws = await self.rtc["websockets"].connect(
                signaling_url(self.args.signaling_url, self.client_id), open_timeout=15,
                close_timeout=3, max_size=4 * 1024 * 1024, proxy=None,
            )
            tasks = [asyncio.create_task(self.signaling()), asyncio.create_task(self.heartbeat())]
            await self.signal("connect", receiver_pwd=self.password_digest, label_name="terminal-fnos-test",
                              is_only_file=True, networkPath="auto", mediaTopology="p2p", audioMode="off",
                              width=0, height=0, fps=1)
            return await self.operations()
        finally:
            self.closing = True
            if self.terminal_started:
                try:
                    self.channel_send("file_text_airan", {"role": "ctl", "msgType": "terminal_input", "requestId": self.terminal_request,
                                                         "data": base64.b64encode(b"exit\n").decode()})
                    await asyncio.wait_for(self.response({"terminal_closed", "terminal_error"}, self.terminal_request), 3)
                except Exception:
                    with contextlib.suppress(Exception):
                        self.channel_send("file_text_airan", {"role": "ctl", "msgType": "terminal_stop", "requestId": self.terminal_request})
            if self.ws:
                with contextlib.suppress(Exception):
                    await asyncio.wait_for(self.signal("session_disconnect", reason="controller_user"), 2)
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self.peer.close(), 5)
            if self.ws:
                with contextlib.suppress(Exception):
                    await self.ws.close()
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("signaling_url")
    parser.add_argument("device_id", type=lambda value: str(uuid.UUID(value)).upper())
    parser.add_argument("--path", type=selected_path, default="home")
    parser.add_argument("--timeout", type=int, default=35, help="seconds per phase (5-120)")
    parser.add_argument("--turn-host")
    parser.add_argument("--turn-port", type=int, default=3478)
    parser.add_argument("--turn-user", default="")
    args = parser.parse_args()
    if not 5 <= args.timeout <= 120 or not 1 <= args.turn_port <= 65535:
        parser.error("Invalid timeout or TURN port")
    if args.turn_host and (not args.turn_user or not os.environ.get("AIRAN_RTC_TURN_PASSWORD")):
        parser.error("TURN requires --turn-user and AIRAN_RTC_TURN_PASSWORD")
    logging.disable(logging.CRITICAL)
    try:
        from aiortc import RTCConfiguration, RTCIceServer, RTCPeerConnection, RTCSessionDescription
        from aiortc.sdp import candidate_from_sdp
        import websockets
    except ImportError:
        print("Optional probe requires aiortc and websockets (pip install aiortc websockets)", file=sys.stderr)
        return 2
    password = os.environ.get("AIRAN_RTC_PASSWORD")
    if password is None:
        password = sys.stdin.readline().rstrip("\r\n")
    if not password:
        print("Provide AIRAN_RTC_PASSWORD or one password line on stdin", file=sys.stderr)
        return 2
    rtc = locals().copy()

    async def bounded():
        probe = Probe(args, password, rtc)
        return await asyncio.wait_for(probe.run(), args.timeout * 4 + 20)

    try:
        result = asyncio.run(bounded())
    except ProbeError as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 1
    except (Exception, KeyboardInterrupt) as exc:
        # Library exceptions can contain URLs or credentials; print type only.
        print(json.dumps({"ok": False, "error": "Probe failed", "category": type(exc).__name__}))
        return 1
    print(json.dumps(result, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
