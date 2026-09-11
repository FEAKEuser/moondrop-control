#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""GAIA v3/v4 over RFCOMM toolkit for MOONDROP headphones (reverse-engineered).

Frame format (both directions):

    FF | ver | flags | payload_len | vendor_hi vendor_lo | cmd_hi cmd_lo | payload...
     0    1      2          3            4        5          6       7         8..

    * ver          -- 0x04 on MOONDROP EDGE (mirror what the device sends)
    * flags        -- 0x00 (no checksum), bit0 would mean a trailing checksum byte
    * payload_len  -- number of *payload* bytes, i.e. total_frame_len - 8
    * vendor       -- 0x001D = MOONDROP / QTiL, 0x000A = generic GAIA
    * cmd (16 bit) -- (feature << 9) | (type << 7) | command
                      type: 0=COMMAND 1=NOTIFICATION 2=RESPONSE 3=ERROR

Example: query firmware version -> ff 04 00 00 00 1d 01 05
         reply                  -> ff 04 00 05 00 1d 01 05 "1.4.0"

Usage:
    gaia.py scan                       # list paired/connected BT devices
    gaia.py query F C [hexpayload]     # send one command, print frames received
    gaia.py enum                       # walk the whole read-only command space
    gaia.py listen SECONDS             # connect and dump notifications
"""
from __future__ import annotations

import socket
import select
import struct
import sys
import time

SOF = 0xFF
VERSION = 0x04
DEFAULT_ADDR = "AA:BB:CC:DD:EE:FF"  # replace with your headphone's address
DEFAULT_CHANNEL = 1
VENDOR_MOONDROP = 0x001D
VENDOR_GAIA = 0x000A

TYPE_COMMAND = 0
TYPE_NOTIFICATION = 1
TYPE_RESPONSE = 2
TYPE_ERROR = 3
TYPE_NAMES = {0: "CMD", 1: "NOTIF", 2: "RESP", 3: "ERR"}

# feature ids (QTiL / Moondrop gaiaclient)
F_BASIC = 0
F_EARBUD = 1
F_ANC = 2
F_VOICE_UI = 3
F_DEBUG = 4
F_MUSIC_PROCESSING = 5
F_UPGRADE = 6
F_HANDSET_SERVICE = 7
F_AUDIO_CURATION = 8
F_EARBUD_FIT = 9
F_VOICE_PROCESSING = 10
F_GESTURE_CONFIGURATION = 11
F_STATISTICS = 12
F_BATTERY = 13
F_VOICE = 14
F_DAC_GAIN = 15
F_CODEC_TYPE = 16
F_LIGHT_SENSOR = 17
F_SPATIAL_AUDIO = 18
F_LED = 19
F_ONEBRINGTWO = 20
F_BT_ADDRESS = 21
F_TOUCHV2 = 22
F_AUDIO_RESOURCE = 23
F_POWER_CONTROL = 24
F_POWER_TIMEOUT = 25
F_TOUCHV3 = 26
F_DYBASS = 27
F_AUDIO_FILE_STORAGE = 29
F_LR_CHANNEL = 30
F_ANC_V2 = 32

FEATURE_NAMES = {
    0: "BASIC", 1: "EARBUD", 2: "ANC_V1", 3: "VOICE_UI", 4: "DEBUG",
    5: "MUSIC_PROCESSING(EQ)", 6: "UPGRADE", 7: "HANDSET_SERVICE", 8: "AUDIO_CURATION",
    9: "EARBUD_FIT", 10: "VOICE_PROCESSING", 11: "GESTURE", 12: "STATISTICS",
    13: "BATTERY", 14: "VOICE", 15: "DAC_GAIN", 16: "CODEC_TYPE", 17: "LIGHT_SENSOR",
    18: "SPATIAL_AUDIO", 19: "LED", 20: "ONEBRINGTWO", 21: "BT_ADDRESS",
    22: "TOUCH_V2", 23: "AUDIO_RESOURCE", 24: "POWER_CONTROL", 25: "POWER_TIMEOUT",
    26: "TOUCH_V3", 27: "DYBASS", 29: "AUDIO_FILE_STORAGE", 30: "LR_CHANNEL", 32: "ANC_V2",
}


def cmd_value(feature: int, command: int, type_: int = TYPE_COMMAND) -> int:
    return ((feature & 0x7FF) << 9) | ((type_ & 0x3) << 7) | (command & 0x7F)


def build(vendor: int, feature: int, command: int, payload: bytes = b"",
          type_: int = TYPE_COMMAND, version: int = VERSION, flags: int = 0) -> bytes:
    cv = cmd_value(feature, command, type_)
    return bytes([SOF, version, flags, len(payload)]) + struct.pack(">HH", vendor, cv) + payload


class Frame:
    __slots__ = ("version", "flags", "vendor", "feature", "type", "command", "payload", "raw")

    def __init__(self, version, flags, vendor, cv, payload, raw):
        self.version = version
        self.flags = flags
        self.vendor = vendor
        self.feature = (cv >> 9) & 0x7FF
        self.type = (cv >> 7) & 0x3
        self.command = cv & 0x7F
        self.payload = payload
        self.raw = raw

    @property
    def type_name(self):
        return TYPE_NAMES.get(self.type, "?")

    def feature_name(self):
        return FEATURE_NAMES.get(self.feature, f"F{self.feature}")

    def __str__(self):
        p = self.payload
        pretty = ""
        if p:
            printable = all(32 <= b < 127 for b in p)
            pretty = f' "{p.decode()}"' if printable else f" [{p.hex(' ')}]"
        return (f"F{self.feature:<3}({self.feature_name()}) {self.type_name:<5} "
                f"C{self.command:<3} len={len(p)}{pretty}")


def parse(buf: bytes):
    """Yield (Frame, consumed) pairs from a stream buffer; returns leftover."""
    out = []
    i = 0
    while True:
        while i < len(buf) and buf[i] != SOF:
            i += 1
        if len(buf) - i < 8:
            break
        length = buf[i + 3]
        total = 8 + length + (1 if buf[i + 2] & 1 else 0)
        if len(buf) - i < total:
            break
        raw = buf[i:i + total]
        out.append(Frame(raw[1], raw[2], struct.unpack(">H", raw[4:6])[0],
                         struct.unpack(">H", raw[6:8])[0], raw[8:8 + length], raw))
        i += total
    return out, buf[i:]


class GaiaClient:
    def __init__(self, addr: str = DEFAULT_ADDR, channel: int = DEFAULT_CHANNEL):
        self.addr = addr
        self.channel = channel
        self.sock = None
        self.buf = b""

    def connect(self, timeout: float = 6.0):
        s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
        s.settimeout(timeout)
        s.connect((self.addr, self.channel))
        s.setblocking(False)
        self.sock = s
        return self

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def __enter__(self):
        return self.connect()

    def __exit__(self, *a):
        self.close()

    def send(self, feature, command, payload=b"", vendor=VENDOR_MOONDROP, type_=TYPE_COMMAND):
        self.sock.sendall(build(vendor, feature, command, payload, type_))

    def read(self, seconds=1.5, quiet_after=0.35):
        """Collect frames until `seconds` elapsed or `quiet_after` of silence."""
        frames = []
        deadline = time.time() + seconds
        last = time.time()
        while time.time() < deadline:
            r, _, _ = select.select([self.sock], [], [], 0.1)
            if r:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buf += data
                got, self.buf = parse(self.buf)
                if got:
                    frames += got
                    last = time.time()
            elif time.time() - last > quiet_after and frames:
                break
        return frames

    def request(self, feature, command, payload=b"", seconds=1.2):
        self.send(feature, command, payload)
        return self.read(seconds)


def _main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    action = argv[1]
    if action == "scan":
        import subprocess
        print(subprocess.run(["bluetoothctl", "devices"], capture_output=True, text=True).stdout)
        return 0
    if action == "query":
        feature, command = int(argv[2], 0), int(argv[3], 0)
        payload = bytes.fromhex(argv[4]) if len(argv) > 4 else b""
        with GaiaClient() as c:
            c.read(0.4)
            print("TX:", build(VENDOR_MOONDROP, feature, command, payload).hex(" "))
            for f in c.request(feature, command, payload, seconds=2.0):
                print("RX:", f, "|", f.raw.hex(" "))
        return 0
    if action == "listen":
        secs = float(argv[2]) if len(argv) > 2 else 10.0
        with GaiaClient() as c:
            for f in c.read(secs, quiet_after=secs):
                print("RX:", f, "|", f.raw.hex(" "))
        return 0
    if action == "enum":
        return enum(argv)
    print("unknown action", action)
    return 2


def enum(argv):
    """Read-only sweep: ask the device for its supported features, then probe
    known GET commands of every supported feature."""
    probes = {
        F_BASIC: [0, 1, 2, 3, 4, 5, 18, 19, 22],
        F_EARBUD: [0, 1, 2],
        F_ANC: [1, 3, 4, 6],
        F_MUSIC_PROCESSING: [0, 1, 2, 4],
        F_AUDIO_CURATION: [0, 2, 3, 5, 7, 8, 10, 12, 13, 15, 17, 18, 20, 22, 23, 25, 26, 28, 30, 31, 34, 35, 37, 38, 39, 41],
        F_GESTURE_CONFIGURATION: [0, 1, 2, 3, 4],
        F_BATTERY: [0, 1],
        F_DAC_GAIN: [1],
        F_CODEC_TYPE: [1, 2, 5],
        F_SPATIAL_AUDIO: [1, 3],
        F_LED: [1],
        F_DYBASS: [1],
        F_LR_CHANNEL: [1],
        F_ANC_V2: [3, 41],
        F_BT_ADDRESS: [1],
    }
    with GaiaClient() as c:
        c.read(0.5)
        supported = set()
        print("== BASIC: get supported features ==")
        for cmd in (1, 2):
            for f in c.request(F_BASIC, cmd, seconds=1.5):
                print("  ", f, "|", f.raw.hex(" "))
                if f.feature == F_BASIC and f.command == cmd:
                    supported.update(f.payload)
        if supported:
            print("   -> supported feature ids:", sorted(supported))
            print("   -> " + ", ".join(f"{i}={FEATURE_NAMES.get(i, '?')}" for i in sorted(supported)))
        features = sorted(supported) if supported else sorted(probes)
        for feat in features:
            cmds = probes.get(feat)
            if not cmds:
                continue
            print(f"\n== feature {feat} ({FEATURE_NAMES.get(feat, '?')}) ==")
            for cmd in cmds:
                for f in c.request(feat, cmd, seconds=1.0):
                    print("  ", f, "|", f.raw.hex(" "))
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
