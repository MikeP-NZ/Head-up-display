#!/usr/bin/env python3
"""Bench stand-in for a Yacht Devices YDWG-02 / YDNR-02 server port.

Streams a sailing boat's speed, wind, heading, depth and waypoint data as
NMEA 0183 or YD RAW, over TCP (like the
factory-default Server #1 on port 1456) or as UDP broadcasts. Point the
display's Data settings at this computer's IP to test without the boat.

    python3 tools/yd_simulator.py                      # TCP 1456, NMEA 0183
    python3 tools/yd_simulator.py --protocol raw       # YD RAW (NMEA 2000)
    python3 tools/yd_simulator.py --transport udp --port 1457
    python3 tools/yd_simulator.py --no-stw             # GPS speed only

On Windows use `python` (or `py`) instead of `python3`, and allow Python
through Windows Firewall when asked, so the display can connect.
"""
import argparse
import math
import random
import socket
import threading
import time
from datetime import datetime, timezone

KNOT = 0.514444  # m/s


class Boat:
    """Instrument values that wander like a boat sailing upwind in waves."""

    def __init__(self, base_knots):
        self.base = base_knots
        self.t0 = time.time()
        self.variation = 19.5  # degrees east

    def t(self):
        return time.time() - self.t0

    def stw_knots(self):
        t = self.t()
        return max(0.0, self.base + 1.2 * math.sin(t / 23) + 0.25 * math.sin(t * 1.7) + random.uniform(-0.05, 0.05))

    def sog_knots(self):
        return max(0.0, self.stw_knots() * 0.93 + 0.4 * math.sin(self.t() / 41))

    def heading(self):  # magnetic
        return (205 + 6 * math.sin(self.t() / 17) + random.uniform(-1, 1)) % 360

    def twd(self):  # magnetic
        return (250 + 10 * math.sin(self.t() / 90)) % 360

    def tws_knots(self):
        return max(0.0, 15 + 3 * math.sin(self.t() / 31) + random.uniform(-0.5, 0.5))

    def twa(self):  # -180..180, + = starboard
        return (self.twd() - self.heading() + 540) % 360 - 180

    def apparent(self):
        """(AWA, AWS knots) from true wind and boat speed."""
        twa, tws, stw = math.radians(self.twa()), self.tws_knots(), self.stw_knots()
        x, y = tws * math.cos(twa) + stw, tws * math.sin(twa)
        return math.degrees(math.atan2(y, x)), math.hypot(x, y)

    def vmg_knots(self):
        return self.stw_knots() * math.cos(math.radians(self.twa()))

    def depth(self):
        return max(1.0, 12.4 + 4 * math.sin(self.t() / 60) + random.uniform(-0.1, 0.1))

    def dtw_nm(self):
        return max(0.0, 5.0 - self.t() * self.base / 3600 * 0.9) % 5.0 or 5.0

    def vmc_knots(self):
        return self.sog_knots() * 0.9

    def water_temp(self):  # Celsius
        return 18.4 + 0.3 * math.sin(self.t() / 300)


def nmea(body):
    cs = 0
    for c in body:
        cs ^= ord(c)
    return "$%s*%02X" % (body, cs)


def nmea_lines(boat, stw=True, sog=True):
    now = datetime.now(timezone.utc)
    hhmmss = now.strftime("%H%M%S.") + "%02d" % (now.microsecond // 10000)
    hdg = boat.heading()
    lines = []
    if stw:
        k = boat.stw_knots()
        lines.append(nmea("YDVHW,,T,%.1f,M,%.2f,N,%.2f,K" % (hdg, k, k * 1.852)))
    if sog:
        k = boat.sog_knots()
        lines.append(nmea("YDRMC,%s,A,3650.123,S,17446.789,E,%.2f,212.4,%s,%.1f,E,A" % (hhmmss, k, now.strftime("%d%m%y"), boat.variation)))
        lines.append(nmea("YDVTG,212.4,T,192.9,M,%.2f,N,%.2f,K,A" % (k, k * 1.852)))
    awa, aws = boat.apparent()
    lines.append(nmea("YDMWV,%.1f,R,%.1f,N,A" % (awa % 360, aws)))
    lines.append(nmea("YDMWV,%.1f,T,%.1f,N,A" % (boat.twa() % 360, boat.tws_knots())))
    twd_m = boat.twd()
    lines.append(nmea("YDMWD,%.1f,T,%.1f,M,%.1f,N,%.1f,M" % ((twd_m + boat.variation) % 360, twd_m, boat.tws_knots(), boat.tws_knots() * KNOT)))
    lines.append(nmea("YDHDG,%.1f,,,%.1f,E" % (hdg, boat.variation)))
    lines.append(nmea("YDDPT,%.1f,0.0," % boat.depth()))
    lines.append(nmea("YDRMB,A,0.12,L,,WP1,3655.000,S,17450.000,E,%.2f,045.0,%.1f,V,A" % (boat.dtw_nm(), boat.vmc_knots())))
    lines.append(nmea("YDVPW,%.2f,N,,M" % boat.vmg_knots()))
    lines.append(nmea("YDMTW,%.1f,C" % boat.water_temp()))
    return lines


def can_id(pgn, src, prio):
    return (prio << 26) | (pgn << 8) | src


def raw_line(pgn, src, prio, data):
    ts = datetime.now(timezone.utc).strftime("%H:%M:%S.%f")[:-3]
    return "%s R %08X %s" % (ts, can_id(pgn, src, prio), " ".join("%02X" % b for b in data))


def u16(v):
    return [v & 0xFF, (v >> 8) & 0xFF]


def i16(v):
    return u16(v & 0xFFFF)


def u32(v):
    return [(v >> (8 * i)) & 0xFF for i in range(4)]


def rad(deg):
    return int(round(math.radians(deg % 360) * 10000))


def fast_packet(pgn, src, prio, payload, seq):
    """Split a payload into NMEA 2000 fast-packet frames."""
    frames = [[(seq << 5) | 0, len(payload)] + payload[:6]]
    rest, n = payload[6:], 1
    while rest:
        chunk = rest[:7]
        rest = rest[7:]
        frames.append([(seq << 5) | n] + chunk + [0xFF] * (7 - len(chunk)))
        n += 1
    return [raw_line(pgn, src, prio, f) for f in frames]


def raw_lines(boat, sid, stw=True, sog=True):
    lines = []
    if stw:
        v = int(round(boat.stw_knots() * KNOT * 100))
        lines.append(raw_line(128259, 0x23, 2, [sid] + u16(v) + [0xFF, 0xFF, 0x00, 0xFF, 0xFF]))
    if sog:
        v = int(round(boat.sog_knots() * KNOT * 100))
        cog = int(3.707 * 10000)
        lines.append(raw_line(129026, 0x05, 2, [sid, 0xFC] + u16(cog) + u16(v) + [0xFF, 0xFF]))
    now = time.time()
    days = int(now // 86400)
    secs = int((now % 86400) * 10000)
    lines.append(raw_line(126992, 0x05, 3, [sid, 0xF0] + u16(days) + u32(secs)))
    lines.append(raw_line(127258, 0x05, 3, [sid, 0xF0, 0xFF, 0xFF] + i16(int(round(math.radians(boat.variation) * 10000))) + [0xFF, 0xFF]))
    awa, aws = boat.apparent()
    lines.append(raw_line(130306, 0x10, 2, [sid] + u16(int(aws * KNOT * 100)) + u16(rad(awa)) + [0xFA, 0xFF, 0xFF]))
    lines.append(raw_line(130306, 0x10, 2, [sid] + u16(int(boat.tws_knots() * KNOT * 100)) + u16(rad(boat.twa())) + [0xFB, 0xFF, 0xFF]))
    # Wind direction referenced to true north; the display converts it with
    # the variation from PGN 127258.
    lines.append(raw_line(130306, 0x10, 2, [sid] + u16(int(boat.tws_knots() * KNOT * 100)) + u16(rad(boat.twd() + boat.variation)) + [0xF8, 0xFF, 0xFF]))
    lines.append(raw_line(127250, 0x11, 2, [sid] + u16(rad(boat.heading())) + [0xFF, 0x7F, 0xFF, 0x7F, 0xFD]))
    lines.append(raw_line(128267, 0x23, 3, [sid] + u32(int(boat.depth() * 100)) + i16(0) + [0xFF]))
    # Sea temperature (source 0), instance 0
    lines.append(raw_line(130312, 0x23, 5, [sid, 0x00, 0x00] + u16(int(round((boat.water_temp() + 273.15) * 100))) + [0xFF, 0xFF, 0xFF]))
    nav = [sid] + u32(int(boat.dtw_nm() * 1852 * 100)) + [0x00] + [0xFF] * 26 + i16(int(boat.vmc_knots() * KNOT * 100))
    lines += fast_packet(129284, 0x05, 3, nav, sid % 8)
    return lines


class Feed:
    def __init__(self, args):
        self.args = args
        self.boat = Boat(args.knots)
        self.sid = 0

    def next_batch(self):
        self.sid = (self.sid + 1) % 253
        a = self.args
        if a.protocol == "raw":
            return raw_lines(self.boat, self.sid, not a.no_stw, not a.no_sog)
        return nmea_lines(self.boat, not a.no_stw, not a.no_sog)


def serve_tcp(args, feed):
    clients = []
    lock = threading.Lock()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(5)

    def accept():
        while True:
            c, addr = srv.accept()
            with lock:
                if len(clients) >= 3:  # YD servers allow 3 TCP clients
                    print("refusing %s:%d (3 clients already)" % addr)
                    c.close()
                    continue
                clients.append(c)
            print("client connected: %s:%d" % addr)

    threading.Thread(target=accept, daemon=True).start()
    print("TCP %s server on port %d - Ctrl+C to stop" % (args.protocol, args.port))
    while True:
        payload = "".join(l + "\r\n" for l in feed.next_batch()).encode()
        if args.verbose:
            print(payload.decode().rstrip())
        with lock:
            for c in list(clients):
                try:
                    c.sendall(payload)
                except OSError:
                    clients.remove(c)
                    print("client disconnected")
        time.sleep(1.0 / args.rate)


def serve_udp(args, feed):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    print("UDP %s broadcast to %s:%d - Ctrl+C to stop" % (args.protocol, args.broadcast, args.port))
    while True:
        for line in feed.next_batch():
            s.sendto((line + "\r\n").encode(), (args.broadcast, args.port))
            if args.verbose:
                print(line)
        time.sleep(1.0 / args.rate)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--protocol", choices=["nmea0183", "raw"], default="nmea0183")
    p.add_argument("--transport", choices=["tcp", "udp"], default="tcp")
    p.add_argument("--port", type=int, default=1456)
    p.add_argument("--broadcast", default="255.255.255.255", help="UDP destination (use your subnet broadcast, e.g. 192.168.1.255)")
    p.add_argument("--rate", type=float, default=2.0, help="batches per second")
    p.add_argument("--knots", type=float, default=6.5, help="average boat speed")
    p.add_argument("--no-stw", action="store_true", help="omit speed through water")
    p.add_argument("--no-sog", action="store_true", help="omit speed over ground")
    p.add_argument("-v", "--verbose", action="store_true", help="print every line sent")
    args = p.parse_args()
    feed = Feed(args)
    try:
        (serve_udp if args.transport == "udp" else serve_tcp)(args, feed)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
