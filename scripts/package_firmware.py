#!/usr/bin/env python3
"""After `pio run`: copy the browser-installable full image and OTA image into site/firmware/.

pioarduino already writes firmware.factory.bin (bootloader + partitions + boot_app0 + app,
flashed at offset 0). It becomes merged.bin for ESP Web Tools; firmware.bin is the OTA image.
"""
import hashlib
import json
import re
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
build = root / ".pio/build/esp32dev"
out = root / "site/firmware"
out.mkdir(parents=True, exist_ok=True)

ini = (root / "platformio.ini").read_text()
version = re.search(r'FIRMWARE_VERSION=\\"([^"\\]+)\\"', ini).group(1)

app = (build / "firmware.bin").read_bytes()
assert app[0] == 0xE9 and app[12:14] == b"\0\0" and app[32:36] == bytes.fromhex("3254cdab"), "Not a classic ESP32 application image"
assert len(app) <= 0x1E0000, "Application exceeds OTA partition"

factory = (build / "firmware.factory.bin").read_bytes()
assert factory[0x1000] == 0xE9 and factory[0x10000:0x10000 + len(app)] == app, "Unexpected factory image layout"

shutil.copyfile(build / "firmware.factory.bin", out / "merged.bin")
shutil.copyfile(build / "firmware.bin", out / "firmware.bin")

manifest = {
    "name": "Prusa Shutter",
    "version": version,
    "new_install_prompt_erase": True,
    "builds": [{"chipFamily": "ESP32", "parts": [{"path": "merged.bin", "offset": 0}]}],
}
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
(out / "SHA256SUMS.txt").write_text(
    "".join(f"{hashlib.sha256((out / n).read_bytes()).hexdigest()}  {n}\n" for n in ["merged.bin", "firmware.bin"])
)
print("Packaged firmware " + version)
