#!/usr/bin/env python3
"""Host-only routing A/B: exact baseline service versus the S3 payload variant.

Both executables use the same current contract harness, SDK stubs and unchanged
Gateway/CRC libraries. Ordinary host RAM does NOT model PSRAM/cache or Wi-Fi.
No flash, network, Git mutation, device or production-configuration access.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SERVICE = "embedded/esp32/components/ug_gateway_service/src/ug_gateway_service.cpp"
TESTS = ROOT / "embedded/esp32/native/tests"


def execute(args, cwd=None):
    return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.STDOUT)


def cmake_path(path):
    return '"' + str(path).replace('\\', '/').replace('"', '\\"').replace('$', '\\$') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=7)
    args = parser.parse_args()
    if os.getuid() == 0 or args.runs < 3:
        raise ValueError("normal project user and at least three paired runs required")
    out = args.output.resolve()
    if out == ROOT or ROOT in out.parents or out.exists():
        raise ValueError("output must be a new directory outside the repository")
    base = execute(["git", "rev-parse", "--verify", args.base + "^{commit}"], ROOT).strip()
    unchanged = ["core/gnss_core", "protocols/gnss_protocols",
                 "embedded/esp32/components/ug_gateway", "embedded/esp32/components/ug_rtcm",
                 "embedded/esp32/components/ug_gateway_service/include/ug_gateway_service.hpp"]
    execute(["git", "diff", "--exit-code", base, "--", *unchanged], ROOT)
    out.mkdir()
    old_service = out / "baseline_service.cpp"
    old_service.write_text(execute(["git", "show", base + ":" + SERVICE], ROOT))
    harness = TESTS / "test_routing_storage.cpp"
    source = harness.read_text()
    needle = '#include "../../components/ug_gateway_service/src/ug_gateway_service.cpp"'
    if source.count(needle) != 1:
        raise ValueError("unexpected contract harness include")
    reference = out / "baseline_test.cpp"
    reference.write_text(source.replace(needle, "#include " + cmake_path(old_service)))
    components = ROOT / "embedded/esp32/components"
    cmake = ["cmake_minimum_required(VERSION 3.22)", "project(routing_ab LANGUAGES CXX)",
             "set(BUILD_TESTING OFF)",
             f"add_subdirectory({cmake_path(ROOT/'core/gnss_core')} core)",
             f"add_subdirectory({cmake_path(ROOT/'protocols/gnss_protocols')} protocols)",
             f"add_library(gateway {cmake_path(components/'ug_gateway/src/ug_gateway.cpp')} "
             f"{cmake_path(components/'ug_rtcm/src/rtcm.cpp')})",
             f"target_include_directories(gateway PUBLIC {cmake_path(components/'ug_gateway/include')} "
             f"{cmake_path(components/'ug_rtcm/include')})",
             "target_link_libraries(gateway PUBLIC universal_gnss_protocols)",
             "target_compile_features(gateway PUBLIC cxx_std_17)"]
    for name, file, s3 in [("baseline", reference, 0), ("candidate", harness, 1)]:
        cmake.extend([
            f"add_executable({name} {cmake_path(file)})",
            f"target_link_libraries({name} PRIVATE gateway)",
            f"target_compile_definitions({name} PRIVATE CONFIG_IDF_TARGET_ESP32S3={s3})",
            f"target_compile_options({name} PRIVATE -UNDEBUG)",
            f"target_include_directories({name} PRIVATE {cmake_path(TESTS/'routing_stubs')} "
            f"{cmake_path(components/'ug_gateway_service/include')} "
            f"{cmake_path(components/'ug_supervisor/include')} "
            f"{cmake_path(components/'espnow_rtcm/include')})"])
    (out / "CMakeLists.txt").write_text("\n".join(cmake) + "\n")
    (out / "configure.log").write_text(execute([
        "cmake", "-S", str(out), "-B", str(out / "build"), "-DCMAKE_BUILD_TYPE=Release"]))
    (out / "build.log").write_text(execute(["cmake", "--build", str(out / "build"), "-j3"]))
    for name in ("baseline", "candidate"):
        (out / (name + "-contract.log")).write_text(execute([str(out / "build" / name), "normal"]))
    result = {"base": base, "candidate_head": execute(["git", "rev-parse", "HEAD"], ROOT).strip(),
              "host": platform.platform(), "runs": args.runs,
              "compiler": execute(["c++", "--version"]).splitlines()[0],
              "harness_sha256": hashlib.sha256(harness.read_bytes()).hexdigest(),
              "baseline_service_sha256": hashlib.sha256(old_service.read_bytes()).hexdigest(),
              "candidate_service_sha256": hashlib.sha256((ROOT / SERVICE).read_bytes()).hexdigest(),
              "qualification": "host only; no PSRAM/RTOS scheduling/radio/physical-latency model",
              "measurements": {"baseline": [], "candidate": []}}
    for i in range(args.runs):
        order = ("baseline", "candidate") if i % 2 == 0 else ("candidate", "baseline")
        for name in order:
            text = execute([str(out / "build" / name), "benchmark"])
            (out / f"{name}-{i}.log").write_text(text)
            rows = [dict((key, int(value)) for key, value in re.findall(r"(\w+)=(\d+)", line))
                    for line in text.splitlines() if line.startswith("ROUTING_BENCH ")]
            if len(rows) != 3 or any(row["outputs"] != row["input_frames"] * 7 for row in rows):
                raise ValueError("incomplete routing benchmark")
            result["measurements"][name].append(rows)
    result["medians"] = {name: [
        {key: statistics.median(run[index][key] for run in runs)
         for key in runs[0][index]} for index in range(3)]
        for name, runs in result["measurements"].items()}
    (out / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["medians"], indent=2))


if __name__ == "__main__":
    main()
