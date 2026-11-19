# PlatformIO post-build step: after firmware.bin is built, merges the
# bootloader, partition table, boot_app0 and the app into one image,
# firmware_combined.bin, in the build directory.
#
# firmware_combined.bin is for a board's first install: write it at offset 0
# with esptool or a browser flasher such as ESP Launchpad. firmware.bin stays
# the image for updates from the portal's System tab.
#
# The chip, flash mode, speed and size come from the environment's board
# settings, so each board gets an image with its own flash layout.
from os.path import join

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

board = env.BoardConfig()  # noqa: F821


def merge_bin(source, target, env):
    app = target[0].get_abspath()
    out = join(env.subst("$BUILD_DIR"), env.subst("${PROGNAME}_combined.bin"))
    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, '"%s"' % env.subst(path)]
    cmd = " ".join([
        '"$PYTHONEXE"', '"$OBJCOPY"',
        "--chip", board.get("build.mcu", "esp32"),
        "merge-bin",
        "--output", '"%s"' % out,
        "--flash-mode", "${__get_board_flash_mode(__env__)}",
        "--flash-freq", "${__get_board_f_image(__env__)}",
        "--flash-size", board.get("upload.flash_size", "4MB"),
    ] + images + ["$ESP32_APP_OFFSET", '"%s"' % app])
    if env.Execute(env.VerboseAction(cmd, "Merging %s" % out)):
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821
