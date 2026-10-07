#!/usr/bin/env python3
"""Uploads a firmware image to a running display over Wi-Fi.

Sends it to the portal's /api/update, the same as System -> Firmware update in
the browser. The display refuses images built for a different board.

    python scripts/ota_upload.py headup.local .pio/build/waveshare_s3_matrix/firmware.bin
    python scripts/ota_upload.py 192.168.71.1 firmware.bin     # via the setup network

PlatformIO runs this for `-t upload` when Wi-Fi upload is chosen (see
scripts/upload_mode.py).
"""
import argparse
import http.client
import json
import os
import socket
import sys
import time
import uuid


def upload(host, path, timeout):
    with open(path, "rb") as f:
        image = f.read()
    boundary = uuid.uuid4().hex
    head = (
        "--%s\r\n"
        'Content-Disposition: form-data; name="firmware"; filename="%s"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n" % (boundary, os.path.basename(path))
    ).encode()
    tail = ("\r\n--%s--\r\n" % boundary).encode()
    hostname, _, port = host.partition(":")

    print("Uploading %s (%d KB) to http://%s/api/update" % (path, len(image) // 1024, host))
    conn = http.client.HTTPConnection(hostname, int(port or 80), timeout=timeout)
    conn.putrequest("POST", "/api/update")
    conn.putheader("Content-Type", "multipart/form-data; boundary=" + boundary)
    conn.putheader("Content-Length", str(len(head) + len(image) + len(tail)))
    conn.endheaders()
    conn.send(head)
    chunk, shown = 8192, -1
    for i in range(0, len(image), chunk):
        conn.send(image[i:i + chunk])
        pct = min(100, (i + chunk) * 100 // len(image))
        if pct // 10 != shown:
            shown = pct // 10
            print("  %3d%%" % pct, flush=True)
    conn.send(tail)

    resp = conn.getresponse()
    body = resp.read().decode(errors="replace")
    conn.close()
    try:
        result = json.loads(body)
    except ValueError:
        result = {}
    if resp.status != 200 or not result.get("ok"):
        raise RuntimeError(result.get("error") or "HTTP %d %s" % (resp.status, body[:200]))


def wait_for_restart(host, timeout):
    """Polls /api/status until the display answers again; returns its version."""
    hostname, _, port = host.partition(":")
    time.sleep(3)  # it reboots about a second after answering
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            conn = http.client.HTTPConnection(hostname, int(port or 80), timeout=3)
            conn.request("GET", "/api/status")
            status = json.loads(conn.getresponse().read())
            conn.close()
            return status.get("fw")
        except (OSError, ValueError, http.client.HTTPException):
            time.sleep(1)
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host", help="display address: headup.local, an IP address, or host:port")
    p.add_argument("firmware", help="firmware.bin to upload")
    p.add_argument("--timeout", type=float, default=60, help="seconds to wait for the display")
    p.add_argument("--no-wait", action="store_true", help="don't wait for the display to restart")
    args = p.parse_args()

    try:
        upload(args.host, args.firmware, args.timeout)
    except (OSError, socket.timeout) as e:
        sys.exit("OTA upload failed: cannot reach %s (%s). Is this computer on the display's network?"
                 % (args.host, e))
    except RuntimeError as e:
        sys.exit("OTA upload failed: the display refused the image: %s" % e)
    print("Installed. The display is restarting.")
    if args.no_wait:
        return
    fw = wait_for_restart(args.host, args.timeout)
    if fw:
        print("Back online, running firmware %s." % fw)
    else:
        print("The display hasn't answered yet; it may have changed address. Check the panel.")


if __name__ == "__main__":
    main()
