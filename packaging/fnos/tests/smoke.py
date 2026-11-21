#!/usr/bin/env python3
"""Test a finished FPK using its real Airan Desk wrapper and private runtime."""

import base64
import configparser
import hashlib
from html.parser import HTMLParser
import http.client
import os
from pathlib import Path, PurePosixPath
import re
import socket
import socketserver
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import uuid
from urllib.parse import urlencode, urlsplit, parse_qs

PREFIX = "/app/airan-desk/"
ADMIN = {"X-Trim-Isadmin": "true", "X-Trim-Userid": "1001"}


class UnixHTTP(http.client.HTTPConnection):
    def __init__(self, path):
        super().__init__("localhost", timeout=30)
        self.path = str(path)

    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(self.timeout)
        self.sock.connect(self.path)


def request(sock, headers=None, form=None, endpoint=PREFIX):
    conn = UnixHTTP(sock)
    try:
        headers = dict(headers or {})
        if form is None:
            conn.request("GET", endpoint, headers=headers)
        else:
            headers["Content-Type"] = "application/x-www-form-urlencoded"
            conn.request("POST", endpoint, urlencode(form), headers)
        res = conn.getresponse()
        return res.status, res.read().decode()
    finally:
        conn.close()


def csrf(page):
    return re.search(r'name="csrf"\s+value="([^"]+)"', page).group(1)


def read_identity(state):
    conf = configparser.ConfigParser(interpolation=None)
    assert conf.read(state / "conf/id.ini"), "identity file missing"
    values = conf["local"]
    result = values["local_id"], values["local_pwd"]
    for value in result:
        parsed = uuid.UUID(value)
        assert parsed.int != 0, "identity contains a null UUID"
        assert value == str(parsed).upper(), "identity UUID is not canonical"
    return result


class IdentityView(HTMLParser):
    def __init__(self, page):
        super().__init__()
        self.fields = {}
        self.identity_inputs = []
        self.active = None
        self.in_identity = False
        self.feed(page)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "section" and attrs.get("aria-labelledby") == "identity-heading":
            self.in_identity = True
        if self.in_identity and tag == "input":
            self.identity_inputs.append(attrs)
        if attrs.get("id") in ("device-id", "device-password", "share-content"):
            self.active = attrs["id"]
            self.fields[self.active] = ""
            if self.active == "share-content":
                assert "readonly" in attrs, "share text must be read-only"

    def handle_endtag(self, tag):
        if tag in ("code", "textarea"):
            self.active = None
        if tag == "section":
            self.in_identity = False

    def handle_data(self, data):
        if self.active:
            self.fields[self.active] += data


def check_identity_page(page, identity):
    rendered = IdentityView(page)
    device, password = identity
    assert rendered.fields.get("device-id") == device, "page shows an unexpected device ID"
    assert rendered.fields.get("device-password") == password, "page shows a stale connection password"
    assert rendered.fields.get("share-content") == (
        "欢迎使用 airan 远程工具。你的 ID：" + device + "\n验证码：" + password
    ), "sharing does not use the current identity"
    assert {field.get("name") for field in rendered.identity_inputs} == {"action", "csrf"}, (
        "identity form accepts a manually entered device ID or password"
    )
    assert all(field.get("type") == "hidden" for field in rendered.identity_inputs), (
        "local identity should not have editable input fields"
    )


class Signaling(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self):
        self.seen = []
        self.clients = []
        super().__init__(("127.0.0.1", 0), SignalHandler)


class SignalHandler(socketserver.StreamRequestHandler):
    def handle(self):
        self.request.settimeout(60)
        line = self.rfile.readline().decode().split()
        if len(line) != 3:
            return
        headers = {}
        while True:
            value = self.rfile.readline()
            if value in (b"\r\n", b"\n", b""):
                break
            key, value = value.decode().split(":", 1)
            headers[key.lower()] = value.strip()
        if "sec-websocket-key" not in headers:
            return
        key = headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
        accept = base64.b64encode(hashlib.sha1(key.encode()).digest())
        self.wfile.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                         b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        self.wfile.flush()
        self.server.seen.append(parse_qs(urlsplit(line[1]).query))
        self.server.clients.append(self.request)
        try:
            while self.request.recv(4096):
                pass
        except OSError:
            pass


def until(check, proc, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline and proc.poll() is None:
        try:
            if check():
                return
        except (OSError, http.client.HTTPException):
            pass
        time.sleep(0.15)
    raise AssertionError("packaged application did not reach the expected state")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def unpack(fpk, root):
    with tarfile.open(fpk) as package:
        archive = next(m for m in package if PurePosixPath(m.name).name == "app.tgz")
        with tarfile.open(fileobj=package.extractfile(archive)) as app:
            for member in app.getmembers():
                target = root / member.name
                assert target.resolve().is_relative_to(root)
                assert member.isfile() or member.isdir() or member.issym() or member.islnk()
                if member.issym():
                    assert (target.parent / member.linkname).resolve().is_relative_to(root)
                if member.islnk():
                    assert (root / member.linkname).resolve().is_relative_to(root)
            app.extractall(root)


def exercise(fpk):
    with tempfile.TemporaryDirectory(prefix="ad-fnos-") as tmp, Signaling() as signaling:
        root = Path(tmp)
        app, state, sock = root / "app", root / "state", root / "settings.sock"
        app.mkdir()
        unpack(fpk, app)
        full = app / "airan"
        assert (full / "lib/pipewire-config/client.conf").is_file()
        assert (full / "plugins/platforms").is_dir()
        version = subprocess.check_output([str(full / "airan-desk"), "--version"],
                                          env={**os.environ, "AIRAN_DESK_SKIP_UINPUT_SETUP": "1"})
        assert version.startswith(b"airan-desk ")
        thread = threading.Thread(target=signaling.serve_forever, daemon=True)
        thread.start()
        command = [str(app / "bin/fnos-manager"), "--app-dir", str(app),
                   "--state-dir", str(state), "--socket", str(sock)]
        with (root / "manager.log").open("wb+") as log:
            proc = subprocess.Popen(command, stdout=log, stderr=log,
                                    env={**os.environ, "DISPLAY": ":59999", "WAYLAND_DISPLAY": "missing"})
            try:
                until(lambda: request(sock, endpoint="/_health")[0] == 200, proc)
                assert request(sock)[0] == 403
                assert request(sock, {**ADMIN, "X-Trim-Isadmin": "false"})[0] == 403
                status, page = request(sock, ADMIN)
                assert status == 200
                initial_identity = read_identity(state)
                check_identity_page(page, initial_identity)
                form = dict(action="save", csrf=csrf(page), ws_url="ws://127.0.0.1:" +
                            str(signaling.server_address[1]), ice_host="", ice_port="3478",
                            ice_username="", ice_password="", allow_remote="on")
                assert request(sock, ADMIN, {**form, "csrf": "invalid"})[0] == 403
                assert request(sock, ADMIN, form)[0] == 400, "owner acceptance must be explicit"
                assert not (state / "home/.wxalh/airan-desk/state/owner-agreement-v1").exists()
                assert request(sock, ADMIN, {**form, "owner_accept": "on"})[0] in (200, 303)
                until(lambda: signaling.seen, proc)
                peer = signaling.seen[-1]
                assert len(peer["sessionId"][0]) == 36
                original_id = peer["sessionId"][0]
                assert original_id == initial_identity[0], "native registration changed the device ID"
                assert read_identity(state) == initial_identity, "native startup changed the identity"
                assert (state / "home/.wxalh/airan-desk/audit").is_dir()
                helper_env = {**os.environ, "AIRAN_DESK_STATE_DIR": str(state)}
                subprocess.run([str(app / "bin/notify"), "test", "fnos-test", "smoke-test-event"],
                               env=helper_env, check=True)
                status, page = request(sock, ADMIN)
                assert status == 200 and "smoke-test-event" in page
                assert request(sock, ADMIN, dict(action="reset_password", csrf=csrf(page)))[0] in (200, 303)
                until(lambda: len(signaling.seen) >= 2, proc)
                updated_identity = read_identity(state)
                assert updated_identity[0] == initial_identity[0], "password rotation changed the device ID"
                assert updated_identity[1] != initial_identity[1], "password rotation retained the old password"
                assert uuid.UUID(updated_identity[1]).version == 4, "replacement password is not a random UUID"
                assert signaling.seen[-1]["sessionId"][0] == original_id
                status, page = request(sock, ADMIN)
                assert status == 200
                check_identity_page(page, updated_identity)
                assert initial_identity[1] not in page, "page still exposes the replaced password"
                stop(proc)
                assert proc.returncode == 0
                count = len(signaling.seen)
                proc = subprocess.Popen(command, stdout=log, stderr=log)
                until(lambda: len(signaling.seen) > count, proc)
                assert signaling.seen[-1]["sessionId"][0] == original_id
                status, page = request(sock, ADMIN)
                assert status == 200
                assert read_identity(state) == updated_identity, "restart lost the rotated password or device ID"
                check_identity_page(page, updated_identity)
                assert initial_identity[1] not in page, "restart restored the replaced password"
                disabled = {**form, "csrf": csrf(page)}
                disabled.pop("allow_remote")
                assert request(sock, ADMIN, disabled)[0] in (200, 303)
                assert request(sock, endpoint="/_health")[0] == 200
            except Exception:
                log.seek(0)
                print(re.sub(r"(?i)[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}",
                             "[UUID redacted]", log.read().decode(errors="replace")))
                raise
            finally:
                stop(proc)
                for client in signaling.clients:
                    try:
                        client.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                signaling.shutdown()
    print("FPK passed: full private runtime, admin/CSRF, explicit setup, native signal registration, "
          "UUID password rotation with stable ID and updated sharing, notification delivery, persistence and disable")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: smoke.py package.fpk")
    exercise(Path(sys.argv[1]))
