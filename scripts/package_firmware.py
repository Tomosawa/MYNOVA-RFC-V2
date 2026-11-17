#!/usr/bin/env python3
"""Produce verified ESP32-S3 factory and application-only Web OTA images."""

import argparse
import csv
import hashlib
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 0x800000
FS_OFFSET = 0x610000
FS_SIZE = 0x1D0000
APP_OFFSET = 0x10000
APP_SIZE = 0x300000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def write_checksum(path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_name(f"{path.name}.sha256").write_text(f"{digest}  {path.name}\n")
    return digest


def find_tool(data_dir, name, executable=None):
    # The pinned core installs one version of each tool in a fresh CI data dir.
    matches = list((data_dir / "packages/esp32/tools" / name).glob(f"*/{executable or name}"))
    require(len(matches) == 1, f"Expected exactly one {name} executable: {matches}")
    return matches[0]


def read_partitions(path):
    partitions = {}
    with path.open() as source:
        rows = csv.reader(line for line in source if line.strip() and not line.lstrip().startswith("#"))
        for row in rows:
            name, kind, subtype, offset, size = [value.strip() for value in row[:5]]
            require(name not in partitions, f"Duplicate partition: {name}")
            partitions[name] = (kind, subtype, int(offset, 0), int(size, 0))
    require(partitions.get("app0") == ("app", "ota_0", APP_OFFSET, APP_SIZE), "Unexpected app0 partition")
    require(partitions.get("app1") == ("app", "ota_1", 0x310000, APP_SIZE), "Unexpected app1 partition")
    # Arduino LittleFS defaults to the historical 'spiffs' label and subtype.
    require(partitions.get("spiffs") == ("data", "spiffs", FS_OFFSET, FS_SIZE), "Unexpected LittleFS partition")
    require(partitions.get("otadata") == ("data", "ota", 0xE000, 0x2000), "Unexpected OTA data partition")
    end = 0x9000  # Bootloader and partition table precede data/application partitions.
    for name, (_, _, offset, size) in sorted(partitions.items(), key=lambda item: item[1][2]):
        require(size > 0 and offset >= end, f"Invalid or overlapping partition: {name}")
        end = offset + size
        require(end <= FLASH_SIZE, f"Partition exceeds 8 MiB flash: {name}")
    return partitions


def package(args):
    build = args.build_dir.resolve()
    web = args.web_dir.resolve()
    output = args.output_dir.resolve()
    core = args.arduino_data.resolve() / "packages/esp32/hardware/esp32" / args.core_version
    source_table = ROOT / "MYNOVA_RFC_V2/partitions.csv"
    read_partitions(source_table)
    require((build / "partitions.csv").read_bytes() == source_table.read_bytes(), "Compiler did not use the sketch partitions.csv")
    require((web / "index.html").is_file(), "Web build is missing index.html")
    require(sum(p.stat().st_size for p in web.rglob("*") if p.is_file()) < FS_SIZE, "Web files exceed the LittleFS partition")

    # Regenerate the table to verify that the compiled binary matches the CSV.
    expected_table = build / "release.partitions.bin"
    run(sys.executable, core / "tools/gen_esp32part.py", "-q", source_table, expected_table)
    compiled_table = build / "MYNOVA_RFC_V2.ino.partitions.bin"
    require(compiled_table.read_bytes() == expected_table.read_bytes(), "Compiled partition binary does not match partitions.csv")

    littlefs = build / "littlefs.bin"
    run(find_tool(args.arduino_data, "mklittlefs"), "-c", web, "-b", "4096", "-p", "256", "-s", str(FS_SIZE), littlefs)
    require(littlefs.stat().st_size == FS_SIZE, "Incorrect LittleFS image size")

    segments = [
        (0x0, build / "MYNOVA_RFC_V2.ino.bootloader.bin", 0x8000),
        (0x8000, compiled_table, 0x1000),
        (0xE000, core / "tools/partitions/boot_app0.bin", 0x2000),
        (APP_OFFSET, build / "MYNOVA_RFC_V2.ino.bin", APP_SIZE),
        (FS_OFFSET, littlefs, FS_SIZE),
    ]
    for _, path, limit in segments:
        require(0 < path.stat().st_size <= limit, f"Image exceeds its allocated region: {path}")

    output.mkdir(parents=True, exist_ok=True)
    version = re.sub(r"[^A-Za-z0-9._-]+", "_", args.version)
    require(bool(version), "Version cannot be empty")
    filename = f"MYNOVA_RFC_V2-{version}-ESP32S3-N8R2-full.bin"
    merged = output / filename
    # Preserve the core-generated flash header: QIO uses a DIO boot header on S3.
    command = [find_tool(args.arduino_data, "esptool_py", "esptool")]
    command += ["--chip", "esp32s3", "merge-bin", "--output", merged, "--pad-to-size", "8MB",
                "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep"]
    for offset, path, _ in segments:
        command += [hex(offset), path]
    run(*command)

    image = merged.read_bytes()
    require(len(image) == FLASH_SIZE, "Merged firmware must be exactly 8 MiB")
    for offset, path, _ in segments:
        content = path.read_bytes()
        require(image[offset:offset + len(content)] == content, f"Merged image differs at {offset:#x}: {path}")

    # WebService uses Update.begin() to write an application to the OTA slot.
    # Publish the compiler's application binary without merging or flash padding.
    ota_filename = f"MYNOVA_RFC_V2-{version}-ESP32S3-N8R2-ota.bin"
    ota = output / ota_filename
    application = (build / "MYNOVA_RFC_V2.ino.bin").read_bytes()
    ota.write_bytes(application)
    digest = write_checksum(merged)
    ota_digest = write_checksum(ota)
    (output / "RELEASE_NOTES.md").write_text(f"""ESP32-S3-WROOM-1-N8R2 · 8 MiB Flash · 2 MiB QSPI PSRAM

| 文件 / File | 用途 / Use |
| --- | --- |
| `{filename}` (8 MiB) | 完整固件，从 `0x000000` 烧录 / Factory image for serial flashing |
| `{ota_filename}` ({len(application)} bytes) | Web OTA 应用固件 / Application-only Web OTA update |

完整镜像包含 Bootloader、分区表、OTA 初始化数据、应用固件，以及 Web UI LittleFS 镜像。
The factory image includes the bootloader, partition table, OTA initialization data, application and Web UI.

| Content | Offset | Partition size |
| --- | --- | --- |
| Bootloader | `0x000000` | — |
| Partition table | `0x008000` | — |
| OTA initialization | `0x00E000` | `0x2000` |
| Application (app0) | `0x010000` | `0x300000` |
| Web UI (LittleFS) | `0x610000` | `0x1D0000` |

使用 ESP 烧录工具，将完整 bin 文件烧录到 **`0x000000`**。命令行示例（esptool 5.x）：
Flash the complete bin at **`0x000000`**. Example (esptool 5.x; replace PORT):

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash 0x000000 {filename}
```

这是完整 Flash 镜像，烧录会覆盖原有配置和已保存的数据；请先备份。此文件不用于 Web OTA。
Flashing replaces existing settings and saved data; back up first. This file is for serial flashing, not Web OTA.

Web OTA：已安装 V2 固件且使用相同分区布局的设备，在 **设置 → 维护** 上传 `{ota_filename}`。
OTA 文件仅更新应用，不包含 Bootloader、分区表或 LittleFS，因此不会更新 Web 页面资源。
For a device running V2 with the same partition layout, upload `{ota_filename}` under **Settings → Maintenance**.
The OTA image updates only the application; it does not contain the bootloader, partition table or LittleFS Web assets.

SHA-256:

- `{filename}`: `{digest}`
- `{ota_filename}`: `{ota_digest}`
""", encoding="utf-8")
    print(f"Verified {merged} ({len(image)} bytes), SHA-256: {digest}", flush=True)
    print(f"Published OTA application {ota} ({len(application)} bytes), SHA-256: {ota_digest}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-data", type=Path, required=True)
    parser.add_argument("--core-version", default="3.3.5")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/firmware")
    parser.add_argument("--web-dir", type=Path, default=ROOT / "RFCWeb_V2/dist")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/release")
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
