#!/usr/bin/env python3
"""Fake instrument display for working on the web portal without hardware.

Serves web/index.html and the same JSON API as the firmware, with simulated
boat data. LED preview frames come from the firmware's own Renderer
(tools/preview/preview), so build that first:

    tools/preview/build.sh                  (Windows: tools\\preview\\build.ps1)
    python3 tools/mock_device.py [--port 8765] [--fresh] [--board wf4|matrixportal]

--fresh starts unconfigured, like a new device; --board picks which
controller board to pretend to be (waveshare, wf4 or matrixportal).
"""
import argparse
import copy
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import yd_simulator  # noqa: E402

PREVIEW = os.path.join(HERE, "preview", "preview.exe")
KNOT = 0.514444
UNITS = {"kn": ("KN", 1.943844), "kmh": ("KM/H", 3.6), "mph": ("MPH", 2.236936), "mps": ("M/S", 1.0)}

DEFAULTS = {
    "wifi": {"ssid": "", "open": True},
    "portal": {"apMode": "always", "ssid": "", "defaultSsid": "HeadUp-1A2B", "open": False, "hostname": "headup"},
    "data": {"transport": "tcp", "protocol": "auto", "host": "", "port": 1456},
    "speed": {"source": "prefer_stw", "unit": "kn", "decimals": -1, "smoothing": 1.0, "staleSec": 5, "stwFilter": 0, "sogFilter": 0, "depthUnit": "m", "tempUnit": "c",
              "distanceUnit": "nm", "shortDistanceUnit": "off", "bearingRef": "mag"},
    "display": {
        "brightness": 160, "speedColor": "#ffffff", "accentColor": "#00a0ff", "showSource": True, "showUnit": True,
        "showStatus": True, "rotation": 0, "sections": 1, "items": ["speed", "aws", "awa", "dpt"],
        "night": {"mode": "off", "brightness": 24, "color": "#ff0000", "start": 1200, "end": 360, "utcOffset": 0},
    },
    "panel": {
        "width": 64, "height": 64, "chain": 1, "scan": "standard", "driver": "FM6126A", "clkPhase": False, "i2sSpeed": 8, "latchBlanking": 1,
        "colorOrder": "RGB",
        "pins": {"r1": 4, "g1": 5, "b1": 6, "r2": 7, "g2": 15, "b2": 16, "a": 18, "b": 8, "c": 3, "d": 42, "e": 9, "lat": 40, "oe": 2, "clk": 41},
    },
}


WF4_PINS = {"r1": 2, "g1": 6, "b1": 10, "r2": 3, "g2": 7, "b2": 11, "a": 39, "b": 38, "c": 37, "d": 36, "e": 21, "lat": 33, "oe": 35, "clk": 34}
MATRIXPORTAL_PINS = {"r1": 42, "g1": 41, "b1": 40, "r2": 38, "g2": 39, "b2": 37, "a": 45, "b": 36, "c": 48, "d": 35,
                     "e": 21, "lat": 47, "oe": 14, "clk": 2}
BOARDS = {
    "waveshare": {"id": "waveshare-s3-matrix", "name": "Waveshare ESP32-S3-RGB-Matrix", "buttonName": "BOOT",
                  "defaultClkPhase": False, "ports": [], "defaultPins": DEFAULTS["panel"]["pins"]},
    "wf4": {"id": "huidu-hd-wf4", "name": "Huidu HD-WF4", "buttonName": "S1", "defaultClkPhase": True,
            "ports": [{"id": p, "label": l} for p, l in [("X1", "X1 (75EX1)"), ("X2", "X2 (75EX2)"), ("X3", "X3 (75EX3)"),
                                                           ("X4", "X4 (75EX4)"), ("X1+X3", "X1 + X3, same image on both"),
                                                           ("X2+X4", "X2 + X4, same image on both")]],
            "defaultPins": WF4_PINS},
    "matrixportal": {"id": "adafruit-matrixportal-s3", "name": "Adafruit Matrix Portal S3", "buttonName": "UP",
                     "defaultClkPhase": True, "ports": [], "defaultPins": MATRIXPORTAL_PINS},
}


def defaults_for(board):
    cfg = copy.deepcopy(DEFAULTS)
    if board["ports"]:
        cfg["panel"].update({"driver": "SHIFTREG", "clkPhase": True, "port": "X1", "pins": dict(WF4_PINS)})
    return cfg


class Device:
    def __init__(self, fresh, board):
        self.lock = threading.Lock()
        self.board = BOARDS[board]
        self.cfg = defaults_for(self.board)
        if not fresh:
            self.cfg["wifi"] = {"ssid": "YDWG", "open": True}
        self.reboot_required = False
        self.boot = time.time()
        self.wifi_since = time.time()
        self.boat = yd_simulator.Boat(6.5)
        self.sid = 0
        self.lines = []  # (seq, t, speed, text)
        self.seq = 0
        self.stats = {"lines": 0, "speed": 0}
        self.test_until = 0
        self.test_start = 0
        self.info_until = 0
        self.scan_done_at = 0
        self.discover_start = 0
        self.epoch_offset = None
        threading.Thread(target=self.feed, daemon=True).start()

    # -- simulated data feed --
    def connected(self):
        return bool(self.cfg["wifi"]["ssid"]) and time.time() - self.wifi_since > 2

    def feed(self):
        while True:
            time.sleep(0.5)
            with self.lock:
                if not self.connected():
                    continue
                proto = self.cfg["data"]["protocol"]
                self.sid = (self.sid + 1) % 253
                batch = yd_simulator.raw_lines(self.boat, self.sid) if proto == "raw" else yd_simulator.nmea_lines(self.boat)
                for line in batch:
                    self.seq += 1
                    speed = " 0DF01005 " not in line and "ZDA" not in line
                    self.stats["lines"] += 1
                    self.stats["speed"] += int(speed)
                    self.lines.append((self.seq, time.time(), speed, line))
                self.lines = self.lines[-48:]

    def speed_state(self):
        mode = self.cfg["speed"]["source"]
        stw, sog = self.boat.stw_knots() * 0.514444, self.boat.sog_knots() * 0.514444
        ok = self.connected()
        kind = "SOG" if mode in ("sog", "prefer_sog") else "STW"
        return ok, kind, stw if kind == "STW" else sog, stw, sog

    def values(self):
        """Every value in display units (wind angles: negative = port)."""
        b, c = self.boat, self.cfg["speed"]
        _, f = UNITS[c["unit"]]
        depth_f = 1 / 0.3048 if c["depthUnit"] == "ft" else 1.0
        awa, aws = b.apparent()
        unit = UNITS[c["unit"]][0]
        dunit = "FT" if c["depthUnit"] == "ft" else "M"
        var = b.variation if c["bearingRef"] == "true" else 0.0
        bref = "oT" if c["bearingRef"] == "true" else "oM"
        dtw_m = b.dtw_nm() * 1852
        short = {"m": (1.0, "M"), "yd": (0.9144, "YD"), "ft": (0.3048, "FT")}.get(c["shortDistanceUnit"])
        if short and dtw_m / short[0] < 999.5:
            dtw = (round(dtw_m / short[0]), short[1])
        else:
            m, u = {"km": (1000.0, "KM"), "mi": (1609.344, "MI")}.get(c["distanceUnit"], (1852.0, "NM"))
            dtw = (dtw_m / m, u)
        return {
            "stw": (b.stw_knots() * KNOT * f, unit), "sog": (b.sog_knots() * KNOT * f, unit),
            "awa": (awa, ""), "aws": (aws * KNOT * f, unit), "twa": (b.twa(), ""), "tws": (b.tws_knots() * KNOT * f, unit),
            "twd": ((b.twd() + var) % 360, bref), "hdg": ((b.heading() + var) % 360, bref), "dtw": dtw,
            "cog": ((b.heading() + 4 + var) % 360, bref), "btw": ((212 + var) % 360, bref),
            "mtw": (b.water_temp() * 1.8 + 32, "oF") if c["tempUnit"] == "f" else (b.water_temp(), "oC"),
            "vmg": (b.vmg_knots() * KNOT * f, unit), "vmc": (b.vmc_knots() * KNOT * f, unit), "dpt": (b.depth() * depth_f, dunit),
        }

    # -- API --
    def status(self):
        unit_label, factor = UNITS[self.cfg["speed"]["unit"]]
        dec = max(0, self.cfg["speed"]["decimals"]) if self.cfg["speed"]["decimals"] >= 0 else 1
        ok, kind, cur, stw, sog = self.speed_state()
        conn = self.connected()
        rnd = lambda v: round(v * factor, dec)
        now = time.time()
        data_age = int((now - self.lines[-1][1]) * 1000) if self.lines and conn else -1
        proto = self.cfg["data"]["protocol"]
        return {
            "fw": "1.1.0-mock", "board": self.board["name"], "deviceId": "1A2B", "uptime": int(now - self.boot), "heap": 182000, "minHeap": 150000,
            "psram": 16 * 1048576, "rebootRequired": self.reboot_required,
            "wifi": {
                "configured": bool(self.cfg["wifi"]["ssid"]), "ssid": self.cfg["wifi"]["ssid"],
                "state": "connected" if conn else ("not_configured" if not self.cfg["wifi"]["ssid"] else "connecting"),
                "connected": conn, "hostname": self.cfg["portal"]["hostname"],
                **({"ip": "192.168.4.2", "gateway": "192.168.4.1", "rssi": -58, "channel": 6} if conn else {}),
            },
            "ap": {"active": True, "ssid": self.cfg["portal"]["ssid"] or "HeadUp-1A2B", "ip": "192.168.71.1", "clients": 1},
            "data": {
                "transport": self.cfg["data"]["transport"], "host": self.cfg["data"]["host"], "port": self.cfg["data"]["port"],
                "remote": "192.168.4.1" if conn else "", "state": "connected" if conn else "no_network", "lastError": "",
                "connects": 1 if conn else 0, "bytes": self.stats["lines"] * 40, "lineRate": 12.0 if conn else 0,
                "lastLineAgo": data_age, "protocol": proto, "detected": ("raw" if proto == "raw" else "nmea0183") if conn else "",
                "lines": self.stats["lines"], "nmeaLines": self.stats["lines"], "rawLines": 0, "badChecksum": 0, "ignored": 0,
                "samples": self.stats["speed"],
            },
            "values": {k: {"valid": conn, "unit": u, **({"value": round(v, 2), "age": 300} if conn else {})}
                       for k, (v, u) in self.values().items()},
            "speed": {
                "valid": ok, "kind": kind, "unit": unit_label, **({"value": rnd(cur)} if ok else {}),
                "stw": {"valid": ok, "value": rnd(stw), "age": 300}, "sog": {"valid": ok, "value": rnd(sog), "age": 300},
            },
            "sources": ([
                {"id": 0x10023, "label": "N2K #35", "kind": "STW", "via": "128259", "value": rnd(stw), "age": 240, "count": self.stats["speed"]},
                {"id": 0x10005, "label": "N2K #5", "kind": "SOG", "via": "129026", "value": rnd(sog), "age": 260, "count": self.stats["speed"]},
                {"id": 0x10016, "label": "N2K #22", "kind": "SOG", "via": "129026", "value": rnd(sog * 1.01), "age": 900, "count": 40},
            ] if proto == "raw" else [
                {"id": 0x25944, "label": "0183 YD", "kind": "STW", "via": "VHW", "value": rnd(stw), "age": 240, "count": self.stats["speed"]},
                {"id": 0x25944, "label": "0183 YD", "kind": "SOG", "via": "RMC", "value": rnd(sog), "age": 250, "count": self.stats["speed"]},
                *[{"id": 0x25944, "label": "0183 YD", "kind": k.upper(), "via": via, "value": round(self.values()[k][0], 2),
                   "unit": self.values()[k][1], "age": 260, "count": self.stats["speed"]}
                  for k, via in (("awa", "MWV"), ("aws", "MWV"), ("twa", "MWV"), ("tws", "MWV"), ("twd", "MWD"), ("hdg", "HDG"),
                                 ("dpt", "DPT"), ("dtw", "RMB"), ("vmc", "RMB"), ("vmg", "VPW"))],
            ]) if conn else [],
            "time": {"valid": True, "epoch": int(now), "night": self.cfg["display"]["night"]["mode"] == "on"},
            "display": {"panelOk": True, "width": self.cfg["panel"]["width"] * self.cfg["panel"]["chain"], "height": self.cfg["panel"]["height"]},
        }

    def frame(self):
        c = self.cfg
        d = c["display"]
        p = c["panel"]
        args = [
            "width=%d" % (p["width"] * p["chain"]), "height=%d" % p["height"], "depthunit=" + c["speed"]["depthUnit"], "tempunit=" + c["speed"]["tempUnit"],
            "distunit=" + c["speed"]["distanceUnit"], "shortdistunit=" + c["speed"]["shortDistanceUnit"],
            "bearingref=" + c["speed"]["bearingRef"],
            "color=" + d["speedColor"][1:], "accent=" + d["accentColor"][1:], "nightcolor=" + d["night"]["color"][1:],
            "showsource=%d" % d["showSource"], "showunit=%d" % d["showUnit"], "showstatus=%d" % d["showStatus"],
            "unit=" + c["speed"]["unit"], "decimals=%d" % c["speed"]["decimals"], "rotation=%d" % d["rotation"],
            "night=%d" % (d["night"]["mode"] == "on"), "now=%d" % int((time.time() - self.boot) * 1000),
        ]
        now = time.time()
        if now < self.test_until:
            cmd = ["test", "t=%d" % int((now - self.test_start) * 1000)]
        elif not c["wifi"]["ssid"] or now < self.info_until:
            cmd = ["info", "setup=%d" % (not c["wifi"]["ssid"]), "page=%d" % int(now / 4), "ssid=" + c["wifi"]["ssid"],
                   "apssid=" + (c["portal"]["ssid"] or "HeadUp-1A2B"), "hostname=" + c["portal"]["hostname"]]
        else:
            ok, kind, cur, _, _ = self.speed_state()
            _, factor = UNITS[c["speed"]["unit"]]
            vals = self.values()
            n = d.get("sections", 1)
            items = d.get("items", ["speed"])[:n]
            values = ["%.3f" % (cur * factor if it == "speed" else vals[it][0] if it in vals else 0) for it in items]
            cmd = ["dash", "sections=%d" % n, "items=" + ",".join(items), "values=" + ",".join(values),
                   "valid=" + ",".join("1" if ok else "0" for _ in items), "kind=" + kind.lower(),
                   "wifi=%d" % self.connected(), "link=%d" % self.connected()]
            if not self.connected():
                cmd += ["err=1", "msg=NO WIFI " + c["wifi"]["ssid"] + "  SETUP: HeadUp-1A2B 192.168.71.1"]
        return subprocess.run([PREVIEW] + cmd + args, check=True, capture_output=True).stdout

    def merge(self, body):
        old_panel = json.dumps(self.cfg["panel"], sort_keys=True)
        old_wifi = json.dumps(self.cfg["wifi"], sort_keys=True)
        for group, values in body.items():
            if group not in self.cfg or not isinstance(values, dict):
                continue
            for k, v in values.items():
                if k in ("password",):
                    if group == "portal" and v and len(v) < 8:
                        raise ValueError("portal password must be 8-63 characters")
                    self.cfg[group]["open"] = not v
                elif k == "open":
                    if v:
                        self.cfg[group]["open"] = True
                elif isinstance(v, dict) and isinstance(self.cfg[group].get(k), dict):
                    self.cfg[group][k].update(v)
                elif k in self.cfg[group]:
                    self.cfg[group][k] = v
        if json.dumps(self.cfg["panel"], sort_keys=True) != old_panel:
            self.reboot_required = True
        if json.dumps(self.cfg["wifi"], sort_keys=True) != old_wifi:
            self.wifi_since = time.time()
        return {"panel": json.dumps(self.cfg["panel"], sort_keys=True) != old_panel}


def make_handler(dev):
    class H(BaseHTTPRequestHandler):
        def log_message(self, fmt, *a):
            if "/api/status" not in self.path and "/api/frame" not in self.path and "/api/sniff" not in self.path:
                sys.stderr.write("%s %s\n" % (self.command, self.path))

        def send(self, code, body, ctype="application/json", headers=None):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n) if n else b""
            return raw

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            with dev.lock:
                if u.path in ("/", "/index.html"):
                    with open(os.path.join(ROOT, "web", "index.html"), "rb") as f:
                        return self.send(200, f.read(), "text/html; charset=utf-8")
                if u.path == "/api/config":
                    return self.send(200, {**dev.cfg, "rebootRequired": dev.reboot_required, "board": dev.board})
                if u.path == "/api/export":
                    return self.send(200, {**dev.cfg, "board": dev.board["id"]}, headers={"Content-Disposition": 'attachment; filename="head-up-display-config.json"'})
                if u.path == "/api/status":
                    return self.send(200, dev.status())
                if u.path == "/api/frame":
                    p = dev.cfg["panel"]
                    return self.send(200, dev.frame(), "application/octet-stream",
                                     {"X-Width": str(p["width"] * p["chain"]), "X-Height": str(p["height"]),
                                      "X-Rotation": str(dev.cfg["display"]["rotation"])})
                if u.path == "/api/sniff":
                    since = int(q.get("since", ["0"])[0])
                    only = q.get("speed", ["0"])[0] == "1"
                    now = time.time()
                    lines = [{"seq": s, "ago": int((now - t) * 1000), "speed": sp, "text": tx}
                             for (s, t, sp, tx) in dev.lines if s > since and (sp or not only)]
                    return self.send(200, {"seq": dev.seq, "lines": lines})
                if u.path == "/api/scan":
                    if time.time() < dev.scan_done_at:
                        return self.send(200, {"scanning": True})
                    return self.send(200, {"scanning": False, "error": False, "networks": [
                        {"ssid": "YDWG", "rssi": -48, "channel": 6, "security": "open", "yachtDevices": True},
                        {"ssid": "Marina Guest", "rssi": -71, "channel": 1, "security": "WPA2", "yachtDevices": False},
                        {"ssid": "YDNR-Galatea", "rssi": -80, "channel": 11, "security": "WPA2", "yachtDevices": True},
                        {"ssid": "Sailfish_5G", "rssi": -84, "channel": 36, "security": "WPA2/WPA3", "yachtDevices": False},
                    ]})
                if u.path == "/api/discover":
                    p = min(100, int((time.time() - dev.discover_start) * 40))
                    return self.send(200, {"running": p < 100, "ran": True, "progress": p, "port": dev.cfg["data"]["port"],
                                           "found": [{"ip": "192.168.4.1", "port": 1456}, {"ip": "192.168.4.1", "port": 1457}] if p >= 100 else []})
            self.send(404, {"ok": False, "error": "not found"})

        def do_POST(self):
            u = urlparse(self.path)
            raw = self.body()
            with dev.lock:
                if u.path == "/api/config":
                    try:
                        changed = dev.merge(json.loads(raw or b"{}"))
                    except ValueError as e:
                        return self.send(400, {"ok": False, "error": str(e)})
                    return self.send(200, {"ok": True, "rebootRequired": dev.reboot_required, "changed": changed})
                if u.path == "/api/scan":
                    dev.scan_done_at = time.time() + 2.5
                    return self.send(200, {"ok": True})
                if u.path == "/api/discover":
                    if not dev.connected():
                        return self.send(409, {"ok": False, "error": "join the boat Wi-Fi first"})
                    dev.discover_start = time.time()
                    return self.send(200, {"ok": True})
                if u.path == "/api/test-pattern":
                    dev.test_start = time.time()
                    dev.test_until = dev.test_start + 10.5
                    return self.send(200, {"ok": True})
                if u.path == "/api/info":
                    dev.info_until = time.time() + 20
                    return self.send(200, {"ok": True})
                if u.path in ("/api/time", "/api/reboot", "/api/factory-reset", "/api/update"):
                    if u.path == "/api/reboot":
                        dev.reboot_required = False
                    if u.path == "/api/factory-reset":
                        dev.cfg = defaults_for(dev.board)
                    return self.send(200, {"ok": True})
            self.send(404, {"ok": False, "error": "not found"})

    return H


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--fresh", action="store_true", help="start unconfigured")
    p.add_argument("--board", choices=sorted(BOARDS), default="waveshare")
    a = p.parse_args()
    if not os.path.exists(PREVIEW):
        sys.exit("build the renderer first: tools/preview/build.sh (Windows: tools\\preview\\build.ps1)")
    dev = Device(a.fresh, a.board)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(dev))
    print("mock instrument display on http://127.0.0.1:%d" % a.port)
    srv.serve_forever()


if __name__ == "__main__":
    main()
