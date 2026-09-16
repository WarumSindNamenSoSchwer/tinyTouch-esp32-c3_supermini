#!/usr/bin/env python3
"""JSON backend for the tinyTouch macOS GUI.

The GUI process runs this script per action. Every line this script prints to
stdout is one JSON object, so the GUI never parses human-formatted text. Device
interaction reuses the same serial protocol and Keychain layout as the CLI and
the helper: one shared identity (TT-<mac>), the tinyTouch/tinyTouch-pairing
services, and the protocol-6 console commands.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "macos-helper"))

import tinytouch_keychain as keychain  # noqa: E402
from tinytouch_helper import device_endpoints  # noqa: E402

PASSWORD_SERVICE = "tinyTouch"
PAIRING_SERVICE = "tinyTouch-pairing"
AGENT = "com.tinytouch.helper"
LAUNCH_AGENT = Path.home() / "Library" / "LaunchAgents" / f"{AGENT}.plist"
LOG_FILE = Path.home() / "Library" / "Logs" / "tinyTouch" / "helper.log"

# Slot layout: up to 4 fingers, 8 template slots per finger.
# Finger f (1-4), view v (1-8) -> device slot (f-1)*8 + v. Every enrolled view
# is an independent template the sensor's search matches against, so more
# views per finger directly reduce false rejections.
SLOTS_PER_FINGER = 8
MAX_FINGERS = 4

ENROLLMENT_VIEWS_8 = (
    ("Mitte", "flach mittig auflegen"),
    ("linke Kante", "leicht nach links gekippt"),
    ("rechte Kante", "leicht nach rechts gekippt"),
    ("obere Kante", "Fingerspitze Richtung Sensor-Oberkante"),
    ("untere Kante", "Fingerbeere Richtung Sensor-Unterkante"),
    ("Mitte gedreht", "flach, Finger leicht gedreht"),
    ("links steil", "stärker nach links gekippt"),
    ("rechts steil", "stärker nach rechts gekippt"),
)


def emit(**payload) -> None:
    sys.stdout.write(json.dumps(payload) + "\n")
    sys.stdout.flush()


def fail(message: str, **extra) -> None:
    emit(event="error", message=message, **extra)
    sys.exit(1)


# ---------------------------------------------------------------- helper ctl

def helper_loaded() -> bool:
    return subprocess.run(
        ["launchctl", "print", f"gui/{os.getuid()}/{AGENT}"],
        check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0


def helper_stop() -> bool:
    was = helper_loaded()
    if was:
        subprocess.run(
            ["launchctl", "bootout", f"gui/{os.getuid()}/{AGENT}"],
            check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    return was


def helper_start() -> None:
    if LAUNCH_AGENT.exists() and not helper_loaded():
        subprocess.run(
            ["launchctl", "bootstrap", f"gui/{os.getuid()}", str(LAUNCH_AGENT)],
            check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )


# ---------------------------------------------------------------- device io

class DeviceError(Exception):
    pass


def find_endpoint():
    endpoints = device_endpoints()
    if not endpoints:
        raise DeviceError("Kein tinyTouch gefunden. Ist das Gerät eingesteckt?")
    return endpoints[0]


class Session:
    """One exclusive serial session. Pauses the helper while open."""

    def __init__(self):
        import serial  # deferred so `status` without pyserial still errors clearly
        self.endpoint = find_endpoint()
        self.helper_was_loaded = helper_stop()
        last = None
        for _ in range(20):
            try:
                self.port = serial.Serial(self.endpoint.port, 115200, timeout=0.25,
                                          write_timeout=2)
                break
            except Exception as exc:  # port briefly busy after helper exit
                last = exc
                time.sleep(0.25)
        else:
            if self.helper_was_loaded:
                helper_start()
            raise DeviceError(f"Serieller Port nicht verfügbar: {last}")
        time.sleep(0.2)
        self.port.reset_input_buffer()

    def close(self):
        try:
            self.port.close()
        finally:
            if self.helper_was_loaded:
                helper_start()

    def command(self, text: str, *, timeout: float = 8.0, on_event=None) -> list[str]:
        """Send one command, stream EVENT lines, return payload lines."""
        self.port.reset_input_buffer()
        self.port.write((text + "\n").encode("ascii"))
        self.port.flush()
        deadline = time.monotonic() + timeout
        buffer = b""
        lines: list[str] = []
        while time.monotonic() < deadline:
            chunk = self.port.read(4096)
            if chunk:
                buffer += chunk
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip()
                    if not line:
                        continue
                    if line.startswith("EVENT "):
                        if on_event:
                            on_event(line[6:])
                        # Device prompts extend the caller's patience window.
                        deadline = time.monotonic() + timeout
                        continue
                    lines.append(line)
                    if line.startswith("OK") or line.startswith("ERR"):
                        if line.startswith("ERR"):
                            raise DeviceError(f"Gerät meldet: {line}")
                        return lines
        raise DeviceError(f"Zeitüberschreitung bei: {text.split()[0]}")


def parse_status(lines: list[str]) -> dict:
    for line in lines:
        if line.startswith("OK STATUS "):
            return dict(part.split("=", 1) for part in line[10:].split() if "=" in part)
    raise DeviceError("Gerät lieferte keinen Status.")


def unlock(session: Session, reason_de: str, *, timeout: float = 30.0) -> None:
    """AUTH with a touch prompt surfaced to the GUI."""
    emit(event="touch", message=f"Finger auflegen: {reason_de}")
    def on_event(name):
        if name == "TOUCH":
            emit(event="touch", message=f"Finger auflegen: {reason_de}")
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            session.command("AUTH", timeout=20, on_event=on_event)
            emit(event="unlocked")
            return
        except DeviceError as exc:
            last = exc
            if "no_match" in str(exc):
                emit(event="retry", message="Nicht erkannt. Bitte nochmal auflegen.")
                continue
            raise
    raise DeviceError(f"Autorisierung fehlgeschlagen: {last}")


def account_for(endpoint) -> str:
    return endpoint.device_id


# ------------------------------------------------------------------ actions

def action_status() -> None:
    result = {
        "helper_installed": LAUNCH_AGENT.exists(),
        "helper_loaded": helper_loaded(),
        "port": None, "device": None, "password_stored": False,
        "paired": False, "account": None,
    }
    try:
        endpoint = find_endpoint()
        result["port"] = endpoint.port
        result["account"] = endpoint.device_id
        result["password_stored"] = keychain.get_password_bytes(
            PASSWORD_SERVICE, endpoint.device_id) is not None
        result["paired"] = keychain.get_password_bytes(
            PAIRING_SERVICE, endpoint.device_id) is not None
        session = Session()
        try:
            result["device"] = parse_status(session.command("STATUS", timeout=10))
        finally:
            session.close()
    except DeviceError as exc:
        result["error"] = str(exc)
    emit(event="status", **result)


def action_set_password() -> None:
    password = sys.stdin.readline().rstrip("\n")
    if not password:
        fail("Leeres Passwort.")
    if len(password.encode()) > 160:
        fail("Passwort darf höchstens 160 Bytes lang sein.")
    endpoint = find_endpoint()
    keychain.set_password(PASSWORD_SERVICE, endpoint.device_id, password)
    # The helper caches credentials per connection; restart to pick them up.
    if helper_stop():
        helper_start()
    emit(event="done", message="Passwort gespeichert.")


def action_pair() -> None:
    """Register this Mac on the device and store the pairing key."""
    session = Session()
    try:
        endpoint = session.endpoint
        key = hashlib.sha256(
            f"tinyTouch HID pairing|{endpoint.device_id}|{platform.node()}".encode()
        ).digest()
        identifier = hashlib.sha256(key).hexdigest()[:16]
        unlock(session, "diesen Mac registrieren")
        lines = session.command("HOST LIST", timeout=5)
        registered = []
        for line in lines:
            if line.startswith("OK HOST LIST"):
                match = re.search(r"ids=(\S*)", line)
                if match and match.group(1):
                    registered = match.group(1).split(",")
        if identifier not in registered:
            session.command(f"HOST ADD {identifier} {key.hex()}", timeout=5)
        keychain.set_password(PAIRING_SERVICE, endpoint.device_id, key.hex())
        emit(event="done", message="Mac registriert.")
    finally:
        session.close()


def action_enroll(finger: int, views: int) -> None:
    """Enroll one finger into its slot range with 4 or 8 views."""
    if not 1 <= finger <= MAX_FINGERS:
        fail(f"Finger muss 1..{MAX_FINGERS} sein.")
    if views not in (4, 8):
        fail("Anzahl der Scans muss 4 oder 8 sein.")
    base = (finger - 1) * SLOTS_PER_FINGER
    session = Session()
    try:
        unlock(session, f"Registrierung von Finger {finger} starten")
        # Old templates in this finger's range would otherwise keep matching
        # after a re-enrollment; clear the range first.
        for offset in range(SLOTS_PER_FINGER):
            try:
                session.command(f"FINGER DELETE {base + offset + 1}", timeout=5)
            except DeviceError:
                pass  # empty slot
        for index in range(1, views + 1):
            view_de, hint = ENROLLMENT_VIEWS_8[index - 1]
            slot = base + index

            def on_event(name, view_de=view_de, hint=hint, index=index):
                if name == "TOUCH":
                    emit(event="enroll_touch", slot=index, view=view_de,
                         tap=1, total=views,
                         message=f"Scan {index}/{views}: {view_de} ({hint})")
                elif name == "LIFT":
                    emit(event="enroll_lift", slot=index, view=view_de,
                         message="Finger abheben")
                elif name == "TOUCH_AGAIN":
                    emit(event="enroll_touch", slot=index, view=view_de,
                         tap=2, total=views,
                         message=f"Scan {index}/{views}: {view_de} nochmal auflegen")

            session.command(f"FINGER ENROLL {slot}", timeout=60, on_event=on_event)
            emit(event="enroll_done", slot=index, view=view_de, total=views)
        emit(event="done", message=f"Finger {finger}: {views} Scans registriert.")
    finally:
        session.close()


def action_delete(target: str) -> None:
    session = Session()
    try:
        unlock(session, "Fingerabdruck löschen")
        if target == "all":
            session.command("FINGER CLEAR", timeout=10)
            emit(event="done", message="Alle Fingerabdrücke gelöscht.")
        else:
            finger = int(target)
            if not 1 <= finger <= MAX_FINGERS:
                fail(f"Finger muss 1..{MAX_FINGERS} sein.")
            base = (finger - 1) * SLOTS_PER_FINGER
            removed = 0
            for offset in range(SLOTS_PER_FINGER):
                try:
                    session.command(f"FINGER DELETE {base + offset + 1}", timeout=5)
                    removed += 1
                except DeviceError:
                    pass
            emit(event="done",
                 message=f"Finger {finger} gelöscht ({removed} Scans entfernt).")
    finally:
        session.close()


def action_fingers() -> None:
    session = Session()
    try:
        lines = session.command("FINGER LIST", timeout=5)
        bitmap = 0
        for line in lines:
            match = re.search(r"bitmap=([0-9a-fA-F]+)", line)
            if match:
                bitmap = int(match.group(1), 16)
        fingers = []
        for finger in range(1, MAX_FINGERS + 1):
            base = (finger - 1) * SLOTS_PER_FINGER
            count = sum(1 for offset in range(SLOTS_PER_FINGER)
                        if bitmap & (1 << (base + offset)))
            fingers.append({"finger": finger, "views": count})
        emit(event="fingers", fingers=fingers)
    finally:
        session.close()


def action_computers() -> None:
    session = Session()
    try:
        lines = session.command("HOST LIST", timeout=5)
        for line in lines:
            if line.startswith("OK HOST LIST"):
                match = re.search(r"ids=(\S*)\s+capacity=(\d+)", line)
                ids = match.group(1).split(",") if match and match.group(1) else []
                emit(event="computers", ids=ids, capacity=int(match.group(2)))
                return
        fail("Keine Antwort auf HOST LIST.")
    finally:
        session.close()


def action_remove_computer(identifier: str) -> None:
    session = Session()
    try:
        unlock(session, "Computer entfernen")
        session.command(f"HOST REMOVE {identifier}", timeout=5)
        emit(event="done", message="Computer entfernt.")
    finally:
        session.close()


def action_keyboard_test() -> None:
    session = Session()
    try:
        unlock(session, "Tastaturtest starten")
        emit(event="typing", message="Tippt in 3 Sekunden 'tinytouch' ins fokussierte Feld...")
        time.sleep(3)
        session.command("KEYBOARD TEST", timeout=15)
        emit(event="done", message="Tastaturtest gesendet.")
    finally:
        session.close()


def action_helper(op: str) -> None:
    if op == "stop":
        helper_stop()
    elif op == "start":
        helper_start()
    elif op == "restart":
        helper_stop()
        helper_start()
    emit(event="done", helper_loaded=helper_loaded())


def action_logs() -> None:
    lines = []
    if LOG_FILE.exists():
        lines = LOG_FILE.read_text(errors="replace").splitlines()[-100:]
    emit(event="logs", lines=lines)


def main() -> None:
    if len(sys.argv) < 2:
        fail("Kommando fehlt.")
    command = sys.argv[1]
    try:
        if command == "status":
            action_status()
        elif command == "set-password":
            action_set_password()
        elif command == "pair":
            action_pair()
        elif command == "enroll":
            finger = int(sys.argv[2]) if len(sys.argv) > 2 else 1
            views = int(sys.argv[3]) if len(sys.argv) > 3 else 4
            action_enroll(finger, views)
        elif command == "delete":
            action_delete(sys.argv[2])
        elif command == "fingers":
            action_fingers()
        elif command == "computers":
            action_computers()
        elif command == "remove-computer":
            action_remove_computer(sys.argv[2])
        elif command == "keyboard-test":
            action_keyboard_test()
        elif command == "helper":
            action_helper(sys.argv[2])
        elif command == "logs":
            action_logs()
        else:
            fail(f"Unbekanntes Kommando: {command}")
    except DeviceError as exc:
        fail(str(exc))
    except Exception as exc:  # pragma: no cover - surfaced to the GUI
        fail(f"{type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
