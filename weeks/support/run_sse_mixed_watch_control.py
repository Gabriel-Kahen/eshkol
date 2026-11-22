"""Build and run the mixed-watch control against an Eshkol source checkout."""

import argparse
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


def run_control(source, name, flags):
    fixture = Path(__file__).with_name("sse_mixed_watch_control.c").resolve()
    c_sources = [fixture] + [
        source / f"lib/core/{file}"
        for file in ("resource_lease.c", "event_loop.c", "event_loop_epoll.c")
    ]
    common = [*flags, "-g", "-D_GNU_SOURCE", "-pthread", "-I", source / "inc"]
    with TemporaryDirectory(prefix="sse-mixed-watch-") as scratch:
        build = Path(scratch)
        objects = []
        for path in c_sources:
            output = build / f"{path.stem}.o"
            subprocess.run(
                ["clang", "-std=c11", "-DESHKOL_RESOURCE_TESTING", *common,
                 "-c", path, "-o", output], check=True, timeout=60,
            )
            objects.append(output)
        handle = build / "runtime_handle.o"
        subprocess.run(
            ["clang++", "-std=c++17", *common, "-c",
             source / "lib/core/runtime_handle.cpp", "-o", handle],
            check=True, timeout=60,
        )
        executable = build / "mixed-watch"
        subprocess.run(
            ["clang++", "-pthread", *flags, *objects, handle, "-o", executable],
            check=True, timeout=60,
        )
        env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
               "UBSAN_OPTIONS": "halt_on_error=1"}
        print(f"{name} Linux epoll control", flush=True)
        subprocess.run([executable], check=True, timeout=30, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_checkout", type=Path)
    source = parser.parse_args().source_checkout.resolve()
    commit, tree = subprocess.check_output(
        ["git", "-C", source, "rev-parse", "HEAD", "HEAD^{tree}"], text=True,
    ).splitlines()
    modified = subprocess.check_output(
        ["git", "-C", source, "status", "--porcelain", "--untracked-files=no"], text=True,
    )
    print(f"Source commit {commit}\nSource tree {tree}\nTracked changes {bool(modified)}", flush=True)
    run_control(source, "Optimized", ["-O2"])
    run_control(source, "Sanitizer", ["-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])


if __name__ == "__main__":
    main()
