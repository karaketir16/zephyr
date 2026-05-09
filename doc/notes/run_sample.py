#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import select
import subprocess
import sys
import termios
import threading
import time
import tty

SCRIPT_DIR = Path(__file__).resolve().parent
WORKSPACE_ROOT = SCRIPT_DIR.parents[2]

BAUD = 115200
BOARD = "rpi_zero_w"
SERIAL_PORT = "/dev/tty.usbserial-0001"


def start_openocd(openocd_cfg):
    return subprocess.Popen(
        ["openocd", "-f", str(openocd_cfg)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def start_gdb(gdb_cmds, elf_file):
    return subprocess.Popen(
        ["gdb", "-ex", f"file {elf_file}", "-x", str(gdb_cmds)],
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build and run a Zephyr sample, streaming UART output and forwarding console input."
    )
    parser.add_argument("sample_path", type=Path, help="Path to the sample to build and run.")
    parser.add_argument("--workspace-root", type=Path, default=WORKSPACE_ROOT)
    parser.add_argument("--board", default=BOARD)
    parser.add_argument("--build-dir", type=Path, default=WORKSPACE_ROOT / "zephyr" / "build")
    parser.add_argument("--elf-file", type=Path, default=None)
    parser.add_argument("--openocd-cfg", type=Path, default=SCRIPT_DIR / "rpi-zero-jlink.cfg")
    parser.add_argument("--gdb-cmds", type=Path, default=SCRIPT_DIR / "cmds.gdb")
    parser.add_argument("--serial-port", default=SERIAL_PORT)
    parser.add_argument("--baud", type=int, default=BAUD)
    args = parser.parse_args()

    args.workspace_root = args.workspace_root.resolve()
    args.sample_path = resolve_sample_path(args.sample_path, args.workspace_root)
    args.build_dir = args.build_dir.resolve()
    args.elf_file = (args.elf_file or args.build_dir / "zephyr" / "zephyr.elf").resolve()
    args.openocd_cfg = args.openocd_cfg.resolve()
    args.gdb_cmds = args.gdb_cmds.resolve()
    return args


def resolve_sample_path(sample_path, workspace_root):
    if not sample_path.is_absolute():
        sample_path = workspace_root / sample_path
    return sample_path.resolve()


def as_build_arg(path, workspace_root):
    try:
        return path.relative_to(workspace_root).as_posix()
    except ValueError:
        return path.as_posix()


def validate_sample_path(sample_path):
    if not sample_path.exists():
        raise SystemExit(f"Sample path does not exist: {sample_path}")
    if not sample_path.is_dir():
        raise SystemExit(f"Sample path is not a directory: {sample_path}")
    if not (sample_path / "CMakeLists.txt").is_file():
        raise SystemExit(f"Sample directory has no CMakeLists.txt: {sample_path}")


def build_sample(args):
    sample_arg = as_build_arg(args.sample_path, args.workspace_root)
    print(f"[*] Building {sample_arg} for {args.board}")
    build = subprocess.run(
        [
            "west",
            "build",
            "-b",
            args.board,
            sample_arg,
            "-d",
            str(args.build_dir),
            "-p",
            "always",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=args.workspace_root,
        text=True,
    )
    if build.returncode != 0:
        print(build.stdout, end="")
        raise SystemExit(build.returncode)


class TerminalMode:
    def __enter__(self):
        self.enabled = sys.stdin.isatty()
        self.fd = None
        self.original = None
        if self.enabled:
            self.fd = sys.stdin.fileno()
            self.original = termios.tcgetattr(self.fd)
            tty.setraw(self.fd)
        return self

    def __exit__(self, exc_type, exc, tb):
        if self.enabled and self.original is not None:
            termios.tcsetattr(self.fd, termios.TCSADRAIN, self.original)


def pump_serial_to_console(ser, stop_event):
    while not stop_event.is_set():
        try:
            data = ser.read(4096)
        except Exception:
            if stop_event.is_set():
                break
            continue

        if not data:
            continue

        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()


def pump_console_to_serial(ser, stop_event):
    stdin_fd = sys.stdin.fileno()

    while not stop_event.is_set():
        ready, _, _ = select.select([stdin_fd], [], [], 0.1)
        if not ready:
            continue

        try:
            data = os.read(stdin_fd, 1)
        except OSError:
            break

        if not data:
            stop_event.set()
            break

        if data == b"\x03":
            raise KeyboardInterrupt

        ser.write(data)
        ser.flush()


def run_sample(args):
    try:
        import serial
    except ImportError as err:
        raise SystemExit("pyserial is required to run samples; install it or use the right venv.") from err

    openocd = start_openocd(args.openocd_cfg)
    gdb = None
    ser = None
    stop_event = threading.Event()
    serial_thread = None

    try:
        time.sleep(2)
        gdb = start_gdb(args.gdb_cmds, args.elf_file)
        time.sleep(1)

        gdb.stdin.write("run\n")
        gdb.stdin.flush()

        ser = serial.Serial(args.serial_port, args.baud, timeout=0.1)

        print("[*] Serial bridge active. Press Ctrl-C to stop.")

        serial_thread = threading.Thread(
            target=pump_serial_to_console,
            args=(ser, stop_event),
            daemon=True,
        )
        serial_thread.start()

        with TerminalMode():
            pump_console_to_serial(ser, stop_event)

    except KeyboardInterrupt:
        print("\n[*] Stopping sample run.")
    finally:
        stop_event.set()
        if serial_thread is not None:
            serial_thread.join(timeout=1)
        if ser is not None:
            ser.close()
        if gdb is not None:
            gdb.kill()
        openocd.kill()


def main():
    args = parse_args()
    validate_sample_path(args.sample_path)
    build_sample(args)
    run_sample(args)


if __name__ == "__main__":
    main()
