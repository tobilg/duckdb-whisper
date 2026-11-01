"""Reject runtime audio-library dependencies in distributable extensions."""

import re
import subprocess
import sys


def main():
    binary = sys.argv[1]
    if sys.platform == "darwin":
        command = ["otool", "-L", binary]
    elif sys.platform.startswith("linux"):
        command = ["readelf", "-d", binary]
    else:
        raise SystemExit("Audio linkage inspection supports Linux and macOS")

    output = subprocess.check_output(command, text=True)
    forbidden = re.compile(
        r"(?:libav(?:codec|format|util|device|filter)|libsw(?:resample|scale)|"
        r"libSDL2[^\s/]*|libasound|libpulse[^\s/]*|libpipewire[^\s/]*)"
        r"[^\s/]*\.(?:so|dylib)"
    )
    dependencies = sorted(set(forbidden.findall(output)))
    if dependencies:
        raise SystemExit(
            "Unexpected shared audio dependencies: " + ", ".join(dependencies)
        )
    print("Audio linkage verified: no shared FFmpeg, SDL2, or audio-backend libraries")


if __name__ == "__main__":
    main()
