#!/usr/bin/env python3
"""Bench stand-in for a Yacht Devices YDWG-02 / YDNR-02 server port.

Streams the same simulated boat as the display's built-in demo mode (beating
to windward, tacking every 3 minutes): speed, wind, heading, depth and
waypoint data as NMEA 0183 or YD RAW, over TCP (like the factory-default
Server #1 on port 1456) or as UDP broadcasts. On the display's portal, set
the Source tab to this computer's IP to test without the boat.

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
import types
from datetime import datetime, timezone

KNOT = 0.514444  # m/s


class Boat:
    """The firmware's demo boat (lib/BoatData/src/DemoBoat.cpp), so the
    simulator and the display's demo mode show the same sailing: beating to
    windward, tacking every 3 minutes to about 45 degrees either side of the
    true wind, slowing through each tack and building speed again. Keep the
    two in step when changing either."""

    LEG = 180.0  # s on each tack
    TACK = 15.0  # s turning through the wind
    CLOSE_HAULED = 45.0  # true wind angle on each tack
    variation = 19.5  # degrees east

    def __init__(self, base_knots):
        self.base = base_knots
        self.t0 = time.time()

    def state(self):
        """Every value at this moment, as one consistent snapshot."""
        return self.at(time.time() - self.t0)

    def at(self, t):
        def side(leg):  # +1 starboard tack (wind from starboard), -1 port
            return 1.0 if leg % 2 == 0 else -1.0

        def smooth(u):
            return u * u * (3 - 2 * u)

        leg = int(t // self.LEG)
        since = t - leg * self.LEG
        tacking = leg > 0 and since < self.TACK
        twa = self.CLOSE_HAULED * side(leg)
        if tacking:
            u = smooth(since / self.TACK)
            twa = self.CLOSE_HAULED * (side(leg - 1) + (side(leg) - side(leg - 1)) * u)
        twa += 2 * math.sin(t / 17) + random.uniform(-1, 1)

        factor = 1.0
        if leg > 0:
            if tacking:
                factor = 1 - 0.5 * smooth(since / self.TACK)
            else:
                factor = 1 - 0.5 * math.exp(-(since - self.TACK) / 20)

        s = types.SimpleNamespace()
        s.twd = (250 + 8 * math.sin(t / 150)) % 360  # magnetic
        s.twa = twa  # -180..180, + = starboard
        s.heading = (s.twd - twa) % 360  # magnetic
        s.tws = max(0.0, 15 + 3 * math.sin(t / 31) + random.uniform(-0.5, 0.5))
        s.stw = max(0.0, factor * (self.base + 0.6 * math.sin(t / 23)) + 0.15 * math.sin(t * 1.7) + random.uniform(-0.05, 0.05))
        s.sog = max(0.0, s.stw * 0.95 + 0.3 * math.sin(t / 41))
        leeway = 4 * max(-1.0, min(1.0, twa / self.CLOSE_HAULED))  # slipping away from the wind
        s.cog_true = (s.heading + self.variation - leeway) % 360
        a = math.radians(twa)
        x, y = s.tws * math.cos(a) + s.stw, s.tws * math.sin(a)
        s.awa, s.aws = math.degrees(math.atan2(y, x)), math.hypot(x, y)
        s.vmg = s.stw * math.cos(a)
        s.depth = max(1.0, 12.4 + 4 * math.sin(t / 60) + random.uniform(-0.1, 0.1))
        s.dtw = 5.0 - math.fmod(t * 4 / 3600, 4.9)  # a waypoint dead upwind
        s.water_temp = 18.4 + 0.3 * math.sin(t / 300)  # Celsius
        return s


def nmea(body):
    cs = 0
    for c in body:
        cs ^= ord(c)
    return "$%s*%02X" % (body, cs)


def nmea_lines(boat, stw=True, sog=True):
    """The firmware demo's sentences, plus time and position in RMC (the
    demo leaves those out so it never sets the display's clock)."""
    s = boat.state()
    var = boat.variation
    now = datetime.now(timezone.utc)
    hhmmss = now.strftime("%H%M%S.") + "%02d" % (now.microsecond // 10000)
    twd_true = (s.twd + var) % 360
    lines = []
    if stw:
        lines.append(nmea("YDVHW,,T,%.1f,M,%.2f,N,%.2f,K" % (s.heading, s.stw, s.stw * 1.852)))
    if sog:
        lines.append(nmea("YDRMC,%s,A,3650.123,S,17446.789,E,%.2f,%.1f,%s,%.1f,E,A" % (hhmmss, s.sog, s.cog_true, now.strftime("%d%m%y"), var)))
        lines.append(nmea("YDVTG,%.1f,T,%.1f,M,%.2f,N,%.2f,K,A" % (s.cog_true, (s.cog_true - var) % 360, s.sog, s.sog * 1.852)))
    lines.append(nmea("YDMWV,%.1f,R,%.1f,N,A" % (s.awa % 360, s.aws)))
    lines.append(nmea("YDMWV,%.1f,T,%.1f,N,A" % (s.twa % 360, s.tws)))
    lines.append(nmea("YDMWD,%.1f,T,%.1f,M,%.1f,N,%.1f,M" % (twd_true, s.twd, s.tws, s.tws * KNOT)))
    lines.append(nmea("YDHDG,%.1f,,,%.1f,E" % (s.heading, var)))
    lines.append(nmea("YDDPT,%.1f,0.0," % s.depth))
    lines.append(nmea("YDRMB,A,0.12,L,,WP1,3655.000,S,17450.000,E,%.2f,%.1f,%.1f,V,A" % (s.dtw, twd_true, s.vmg)))
    lines.append(nmea("YDVPW,%.2f,N,,M" % s.vmg))
    lines.append(nmea("YDMTW,%.1f,C" % s.water_temp))
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
    """The same values as nmea_lines(), as NMEA 2000 PGNs."""
    s = boat.state()
    var = boat.variation
    lines = []
    if stw:
        lines.append(raw_line(128259, 0x23, 2, [sid] + u16(int(round(s.stw * KNOT * 100))) + [0xFF, 0xFF, 0x00, 0xFF, 0xFF]))
    if sog:
        v = int(round(s.sog * KNOT * 100))
        lines.append(raw_line(129026, 0x05, 2, [sid, 0xFC] + u16(rad(s.cog_true)) + u16(v) + [0xFF, 0xFF]))
    now = time.time()
    days = int(now // 86400)
    secs = int((now % 86400) * 10000)
    lines.append(raw_line(126992, 0x05, 3, [sid, 0xF0] + u16(days) + u32(secs)))
    lines.append(raw_line(127258, 0x05, 3, [sid, 0xF0, 0xFF, 0xFF] + i16(int(round(math.radians(var) * 10000))) + [0xFF, 0xFF]))
    lines.append(raw_line(130306, 0x10, 2, [sid] + u16(int(s.aws * KNOT * 100)) + u16(rad(s.awa)) + [0xFA, 0xFF, 0xFF]))
    lines.append(raw_line(130306, 0x10, 2, [sid] + u16(int(s.tws * KNOT * 100)) + u16(rad(s.twa)) + [0xFB, 0xFF, 0xFF]))
    # Wind direction referenced to true north; the display converts it with
    # the variation from PGN 127258.
    lines.append(raw_line(130306, 0x10, 2, [sid] + u16(int(s.tws * KNOT * 100)) + u16(rad(s.twd + var)) + [0xF8, 0xFF, 0xFF]))
    lines.append(raw_line(127250, 0x11, 2, [sid] + u16(rad(s.heading)) + [0xFF, 0x7F, 0xFF, 0x7F, 0xFD]))
    lines.append(raw_line(128267, 0x23, 3, [sid] + u32(int(s.depth * 100)) + i16(0) + [0xFF]))
    # Sea temperature (source 0), instance 0
    lines.append(raw_line(130312, 0x23, 5, [sid, 0x00, 0x00] + u16(int(round((s.water_temp + 273.15) * 100))) + [0xFF, 0xFF, 0xFF]))
    # Navigation data: distance, true bearing position -> waypoint, VMC.
    nav = ([sid] + u32(int(s.dtw * 1852 * 100)) + [0x00] + [0xFF] * 8 + u16(rad(s.twd + var))
           + [0xFF] * 16 + i16(int(s.vmg * KNOT * 100)))
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
    p.add_argument("--knots", type=float, default=6.2, help="average boat speed between tacks")
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
