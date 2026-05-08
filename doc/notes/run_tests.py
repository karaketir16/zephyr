#!/usr/bin/env python3

import argparse
from datetime import datetime
import glob
from pathlib import Path
import subprocess
import time

SCRIPT_DIR = Path(__file__).resolve().parent
WORKSPACE_ROOT = SCRIPT_DIR.parents[2]

BAUD = 115200
BOARD = "rpi_zero_w"
DEFAULT_TIMEOUT = 300
SERIAL_PORT = "/dev/tty.usbserial-0001"
FAILURE_STR = "PROJECT EXECUTION FAILED"
SUCCESS_STR = "PROJECT EXECUTION SUCCESSFUL"


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


def run_test(test_name, timeout, args):
    tstr = "forever" if timeout is None else f"{timeout}s"
    print(f"[*] Running {test_name} (timeout={tstr})")

    build = subprocess.run(
        [
            "west",
            "build",
            "-b",
            args.board,
            test_name,
            "-d",
            str(args.build_dir),
            "-p",
            "always",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        cwd=args.workspace_root,
    )
    if build.returncode != 0:
        return "BUILD_FAIL", 0.0, "BUILD FAILED\n"

    openocd = start_openocd(args.openocd_cfg)
    gdb = None
    ser = None

    try:
        time.sleep(2)

        gdb = start_gdb(args.gdb_cmds, args.elf_file)
        time.sleep(1)

        gdb.stdin.write("run\n")
        gdb.stdin.flush()

        try:
            import serial
        except ImportError as err:
            raise SystemExit("pyserial is required to run tests; install it or use the right venv.") from err

        ser = serial.Serial(args.serial_port, args.baud, timeout=0.1)

        output_lines = []
        result = "TIMEOUT"

        start = time.time()

        while True:
            runtime = time.time() - start

            if timeout is not None and runtime > timeout:
                break

            try:
                line = ser.readline().decode(errors="ignore")
            except Exception:
                continue

            if not line:
                continue

            output_lines.append(line)
            print(line, end="")

            if args.success_string in line:
                result = "PASS"
                break
            if args.failure_string in line:
                result = "FAIL"
                break

        runtime = time.time() - start

        if result == "TIMEOUT":
            if timeout is None:
                output_lines.append("\n[STOPPED]\n")
            else:
                output_lines.append(f"\n[TIMEOUT after {runtime:.2f}s]\n")

        return result, runtime, "".join(output_lines)

    finally:
        if ser is not None:
            ser.close()
        if gdb is not None:
            gdb.kill()
        openocd.kill()


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build and run Zephyr tests on a board through OpenOCD/GDB."
    )
    parser.add_argument("test_list", type=Path, help="File containing test paths and optional timeouts.")
    parser.add_argument("--workspace-root", type=Path, default=WORKSPACE_ROOT)
    parser.add_argument("--board", default=BOARD)
    parser.add_argument("--build-dir", type=Path, default=WORKSPACE_ROOT / "zephyr" / "build")
    parser.add_argument("--elf-file", type=Path, default=None)
    parser.add_argument("--openocd-cfg", type=Path, default=SCRIPT_DIR / "rpi-zero-jlink.cfg")
    parser.add_argument("--gdb-cmds", type=Path, default=SCRIPT_DIR / "cmds.gdb")
    parser.add_argument("--serial-port", default=SERIAL_PORT)
    parser.add_argument("--baud", type=int, default=BAUD)
    parser.add_argument("--success-string", default=SUCCESS_STR)
    parser.add_argument("--failure-string", default=FAILURE_STR)
    parser.add_argument("--default-timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--results-root", type=Path, default=SCRIPT_DIR / "results")
    parser.add_argument("--result-name", default=None, help="Override the timestamped result folder name.")
    args = parser.parse_args()

    args.workspace_root = args.workspace_root.resolve()
    args.build_dir = args.build_dir.resolve()
    args.elf_file = (args.elf_file or args.build_dir / "zephyr" / "zephyr.elf").resolve()
    args.openocd_cfg = args.openocd_cfg.resolve()
    args.gdb_cmds = args.gdb_cmds.resolve()
    args.results_root = args.results_root.resolve()
    return args


def read_test_list(test_list):
    with test_list.open() as f:
        return [line.strip() for line in f if line.strip() and not line.lstrip().startswith("#")]


def parse_test_entry(line, default_timeout):
    parts = line.split()

    if len(parts) == 1:
        return parts[0], default_timeout
    if len(parts) == 2:
        return parts[0], int(parts[1])

    raise ValueError(f"Invalid line: {line}")


def as_test_arg(path, workspace_root):
    try:
        return path.relative_to(workspace_root).as_posix()
    except ValueError:
        return path.as_posix()


def find_test_dirs(path):
    if not path.is_dir():
        return []

    if (path / "CMakeLists.txt").is_file():
        return [path]

    tests = []
    for child in sorted(p for p in path.iterdir() if p.is_dir()):
        tests.extend(find_test_dirs(child))

    return tests


def resolve_test_path(test_name, workspace_root):
    path = Path(test_name)
    if not path.is_absolute():
        path = workspace_root / path
    return path.resolve()


def expand_test_name(test_name, workspace_root):
    if not glob.has_magic(test_name):
        path = resolve_test_path(test_name, workspace_root)
        return [as_test_arg(path, workspace_root)] if (path / "CMakeLists.txt").is_file() else []

    pattern = Path(test_name)
    if not pattern.is_absolute():
        pattern = workspace_root / pattern

    expanded = []
    seen = set()
    for match in sorted(Path(p).resolve() for p in glob.glob(str(pattern), recursive=True)):
        for test_dir in find_test_dirs(match):
            if test_dir in seen:
                continue
            seen.add(test_dir)
            expanded.append(as_test_arg(test_dir, workspace_root))

    return expanded


def skipped_test_reason(test_name, workspace_root):
    if glob.has_magic(test_name):
        return f"No CMakeLists.txt test directories matched: {test_name}"

    path = resolve_test_path(test_name, workspace_root)
    if not path.exists():
        return f"Test path does not exist: {test_name}"
    if not path.is_dir():
        return f"Test path is not a directory: {test_name}"
    return f"Test directory has no CMakeLists.txt: {test_name}"


def result_file_name(test_name):
    return test_name.replace("/", "_")


def write_result(result_dir, test_name, output):
    result_file = result_dir / f"{result_file_name(test_name)}.log"
    with result_file.open("w") as f:
        f.write(output)


def main():
    args = parse_args()
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    result_dir = args.results_root / (args.result_name or f"run-{timestamp}")
    result_dir.mkdir(parents=True, exist_ok=False)
    results = []

    for line in read_test_list(args.test_list):
        try:
            test_name, timeout = parse_test_entry(line, args.default_timeout)
        except ValueError as err:
            print(err)
            continue

        expanded_tests = expand_test_name(test_name, args.workspace_root)
        if not expanded_tests:
            print(skipped_test_reason(test_name, args.workspace_root))
            continue

        for expanded_test in expanded_tests:
            result, runtime, output = run_test(expanded_test, timeout, args)

            write_result(result_dir, expanded_test, output)
            results.append((expanded_test, result, runtime))

    summary_path = result_dir / "summary.txt"
    with summary_path.open("w") as f:
        for name, res, runtime in results:
            line = f"{name}: {res} ({runtime:.2f}s)\n"
            f.write(line)
            print(line, end="")

    print(f"\n[*] Summary written to {summary_path}")


if __name__ == "__main__":
    main()
