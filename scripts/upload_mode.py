# PlatformIO pre-build step: lets `-t upload` send the firmware over Wi-Fi
# through the portal (scripts/ota_upload.py) instead of USB.
#
# Wi-Fi upload is used when either:
#   - `custom_upload = ota` is set for the environment in platformio.ini, or
#   - the upload port is a network address: `--upload-port headup.local`
#     or `--upload-port 192.168.71.1`.
# The display's address is the upload port, else `custom_ota_host`.
#
# The platform would otherwise treat a network upload port as Espressif's
# "espota" protocol, which this firmware doesn't run.
#
# It also adds an "ota" target (VS Code: Project Tasks -> Custom -> Upload
# over Wi-Fi) that always uploads over Wi-Fi to `custom_ota_host`:
#   pio run -e waveshare_s3_matrix -t ota
import os
import re

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: F401  pylint: disable=import-error

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

NETWORK_PORT = re.compile(r"^\"?((\d{1,3}\.){3}\d{1,3}|[^\\/:]+\.local|[^\\/]+:\d+)\"?$")

mode = env.GetProjectOption("custom_upload", "usb").strip().lower()  # noqa: F821
if mode not in ("usb", "ota"):
    raise SystemExit("custom_upload must be usb or ota, not '%s'" % mode)

default_host = env.GetProjectOption("custom_ota_host", "headup.local").strip()  # noqa: F821
port = env.subst("$UPLOAD_PORT")  # noqa: F821
if port and NETWORK_PORT.match(port):
    mode = "ota"
    host = port.strip('"')
else:
    host = default_host

uploader = os.path.join(env["PROJECT_DIR"], "scripts", "ota_upload.py")  # noqa: F821


def ota_command(address):
    return '"$PYTHONEXE" "%s" "%s" "$SOURCE"' % (uploader, address)


if mode == "ota":
    env.Replace(UPLOAD_PROTOCOL="custom", UPLOAD_PORT="", UPLOADCMD=ota_command(host))  # noqa: F821
    if "upload" in COMMAND_LINE_TARGETS:
        print("Upload: over Wi-Fi to %s" % host)

# The platform names the image "firmware" only after pre: scripts run, so
# ${PROGNAME} would still be "program" here.
env.AddCustomTarget(  # noqa: F821
    name="ota",
    dependencies="$BUILD_DIR/firmware.bin",
    actions=ota_command(host if mode == "ota" else default_host),
    title="Upload over Wi-Fi",
    description="Build and upload the firmware through the display's portal (custom_ota_host)",
)
