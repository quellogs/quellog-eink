#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import textwrap
import zipfile
from datetime import datetime, timezone
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_DIR / "build"
DIST_DIR = PROJECT_DIR / "dist" / "release"
BOARD_NAME = "zectrix-s3-epaper-4.2"
TARGET = "esp32s3"
ZIP_BASENAME_TEMPLATE = "quellog-firmware_v{version}"


def run_command(args: list[str]) -> None:
    subprocess.run(args, cwd=PROJECT_DIR, check=True)


def find_idf_export_script() -> Path | None:
    if shutil.which("idf.py") is not None:
        return None

    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        export_script = Path(idf_path) / "export.sh"
        if export_script.is_file():
            return export_script

    for candidate in (Path.home() / "esp/esp-idf/export.sh", Path("/root/esp/esp-idf/export.sh")):
        if candidate.is_file():
            return candidate

    raise RuntimeError("ESP-IDF environment not found. Run source export.sh first.")


def run_idf_command(args: list[str]) -> None:
    export_script = find_idf_export_script()
    if export_script is None:
        run_command(["idf.py", *args])
        return

    command = shlex.join(["idf.py", *args])
    subprocess.run(
        ["bash", "-lc", f"source {export_script} >/dev/null && {command}"],
        cwd=PROJECT_DIR,
        check=True,
    )


def run_python_module_with_idf_env(args: list[str]) -> None:
    export_script = find_idf_export_script()
    if export_script is None:
        run_command([sys.executable, *args])
        return

    command = shlex.join(["python", *args])
    subprocess.run(
        ["bash", "-lc", f"source {export_script} >/dev/null && {command}"],
        cwd=PROJECT_DIR,
        check=True,
    )


def build_web_ui() -> None:
    run_command([sys.executable, "scripts/build_web_ui.py"])


def read_project_version() -> str:
    cmake_path = PROJECT_DIR / "CMakeLists.txt"
    for line in cmake_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("set(PROJECT_VER"):
            return line.split('"')[1]
    raise RuntimeError("Unable to read PROJECT_VER from firmware/CMakeLists.txt")


def validate_tag(project_version: str, tag: str | None) -> None:
    if tag is None:
        return
    expected_tag = f"v{project_version}"
    if tag != expected_tag:
        raise RuntimeError(
            f"Git tag {tag} does not match firmware PROJECT_VER {expected_tag}"
        )


def load_flasher_args() -> dict:
    flasher_args_path = BUILD_DIR / "flasher_args.json"
    with flasher_args_path.open(encoding="utf-8") as file:
        return json.load(file)


def package_filenames() -> dict[str, str]:
    return {
        "merged_bin": "merged-binary.bin",
        "flash_args": "flash_args",
        "flasher_args": "flasher_args.json",
        "bootloader": "bootloader.bin",
        "partition_table": "partition-table.bin",
        "app": "quellog_firmware.bin",
        "readme": "FLASHING.md",
        "manifest": "manifest.json",
    }


def build_package_name_map(flasher_args: dict) -> dict[str, str]:
    package_files = package_filenames()
    return {
        flasher_args["bootloader"]["file"]: package_files["bootloader"],
        flasher_args["partition-table"]["file"]: package_files["partition_table"],
        flasher_args["app"]["file"]: package_files["app"],
    }


def create_package_flash_args(flasher_args: dict) -> str:
    lines = [
        f"--flash_mode {flasher_args['flash_settings']['flash_mode']}"
        f" --flash_freq {flasher_args['flash_settings']['flash_freq']}"
        f" --flash_size {flasher_args['flash_settings']['flash_size']}"
    ]

    file_map = build_package_name_map(flasher_args)

    for offset, file_name in flasher_args["flash_files"].items():
        package_name = file_map.get(file_name, Path(file_name).name)
        lines.append(f"{offset} {package_name}")

    return "\n".join(lines) + "\n"


def create_package_flasher_args(flasher_args: dict) -> dict:
    mapping = build_package_name_map(flasher_args)

    package_args = json.loads(json.dumps(flasher_args))
    package_args["flash_files"] = {
        offset: mapping.get(file_name, Path(file_name).name)
        for offset, file_name in flasher_args["flash_files"].items()
    }

    for key in ("bootloader", "app", "partition-table"):
        if key in package_args and "file" in package_args[key]:
            package_args[key]["file"] = mapping.get(
                package_args[key]["file"], Path(package_args[key]["file"]).name
            )

    return package_args


def create_manifest(version: str, flasher_args: dict) -> dict:
    package_files = package_filenames()
    return {
        "version": version,
        "board": BOARD_NAME,
        "target": TARGET,
        "chip": flasher_args["extra_esptool_args"]["chip"],
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "artifacts": [
            package_files["merged_bin"],
            package_files["flash_args"],
            package_files["flasher_args"],
            package_files["bootloader"],
            package_files["partition_table"],
            package_files["app"],
            package_files["readme"],
            package_files["manifest"],
        ],
        "flashMode": flasher_args["flash_settings"]["flash_mode"],
        "flashFreq": flasher_args["flash_settings"]["flash_freq"],
        "flashSize": flasher_args["flash_settings"]["flash_size"],
    }


def create_flashing_readme(version: str) -> str:
    return textwrap.dedent(
        f"""\
        # 泉流迹 固件 v{version}

        板型：{BOARD_NAME}
        目标芯片：{TARGET}

        本发布包支持以下两种烧录方式。

        ## 多文件烧录

        适用于按分区地址分别烧录各个镜像文件。`flash_args` 已包含
        `bootloader.bin`、`partition-table.bin` 和 `quellog_firmware.bin`
        的烧录地址。

        ```bash
        python -m esptool --chip {TARGET} -p <PORT> -b 460800 --before default_reset --after hard_reset write_flash @flash_args
        ```

        ## 单文件烧录

        适用于直接烧录合并后的整包镜像 `merged-binary.bin`。

        ```bash
        python -m esptool --chip {TARGET} -p <PORT> -b 460800 --before default_reset --after hard_reset write_flash 0x0 merged-binary.bin
        ```

        请将 `<PORT>` 替换为实际串口设备，例如 `/dev/ttyUSB0`。
        """
    )


def merge_bin() -> None:
    flasher_args = load_flasher_args()
    args = [
        "-m",
        "esptool",
        "--chip",
        flasher_args["extra_esptool_args"]["chip"],
        "merge_bin",
        "-o",
        str(BUILD_DIR / "merged-binary.bin"),
        *flasher_args["write_flash_args"],
    ]

    for offset, file_name in flasher_args["flash_files"].items():
        args.extend([offset, str(BUILD_DIR / file_name)])

    run_python_module_with_idf_env(args)


def build_firmware() -> None:
    build_web_ui()
    run_idf_command(["set-target", TARGET])
    run_idf_command(["build"])
    merge_bin()


def ensure_required_build_outputs() -> None:
    required_paths = [
        BUILD_DIR / "merged-binary.bin",
        BUILD_DIR / "flasher_args.json",
    ]
    missing = [str(path) for path in required_paths if not path.exists()]
    if missing:
        raise RuntimeError(
            "Missing build outputs for packaging: " + ", ".join(missing)
        )


def write_package(version: str) -> Path:
    flasher_args = load_flasher_args()
    package_dir = DIST_DIR / f"v{version}"
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True, exist_ok=True)

    package_files = package_filenames()
    app_source = BUILD_DIR / flasher_args["app"]["file"]
    bootloader_source = BUILD_DIR / flasher_args["bootloader"]["file"]
    partition_source = BUILD_DIR / flasher_args["partition-table"]["file"]

    shutil.copy2(BUILD_DIR / "merged-binary.bin", package_dir / package_files["merged_bin"])
    shutil.copy2(bootloader_source, package_dir / package_files["bootloader"])
    shutil.copy2(partition_source, package_dir / package_files["partition_table"])
    shutil.copy2(app_source, package_dir / package_files["app"])

    (package_dir / package_files["flash_args"]).write_text(
        create_package_flash_args(flasher_args),
        encoding="utf-8",
    )
    (package_dir / package_files["flasher_args"]).write_text(
        json.dumps(create_package_flasher_args(flasher_args), indent=2) + "\n",
        encoding="utf-8",
    )
    (package_dir / package_files["manifest"]).write_text(
        json.dumps(create_manifest(version, flasher_args), indent=2) + "\n",
        encoding="utf-8",
    )
    (package_dir / package_files["readme"]).write_text(
        create_flashing_readme(version),
        encoding="utf-8",
    )

    zip_base = ZIP_BASENAME_TEMPLATE.format(version=version)
    zip_path = package_dir / f"{zip_base}.zip"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for file_name in [
            package_files["merged_bin"],
            package_files["flash_args"],
            package_files["flasher_args"],
            package_files["bootloader"],
            package_files["partition_table"],
            package_files["app"],
            package_files["readme"],
            package_files["manifest"],
        ]:
            archive.write(package_dir / file_name, arcname=file_name)

    return zip_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build and package Quellog firmware release")
    parser.add_argument(
        "--tag",
        help="Validate that the provided tag matches the firmware version, e.g. v0.1.0",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Package from the existing build/ directory without rebuilding",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    os.chdir(PROJECT_DIR)

    version = read_project_version()
    validate_tag(version, args.tag)
    if args.skip_build:
        ensure_required_build_outputs()
    else:
        build_firmware()
    zip_path = write_package(version)
    print(zip_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        raise SystemExit(1)
