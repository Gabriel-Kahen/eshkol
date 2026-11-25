#!/usr/bin/env python3
"""Build and run a downstream C consumer of an installed embedding SDK."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile


FIXTURE = Path(__file__).resolve().parents[1] / "tests/integration/embedding_sdk"
STDLIB_MESSAGES = (
    "[REPL] Loaded stdlib bitcode from: ",
    "[REPL] Loaded stdlib from cached object: ",
    "[REPL] Loaded stdlib from: ",
)
EXPECTED_OUTPUT = "eshkol-embedding-sdk-ok:42"


def stage_package(prefix, archive, destination):
    if prefix:
        if destination.is_relative_to(prefix.resolve()):
            raise ValueError("choose a work directory outside the installed SDK prefix")
        shutil.copytree(prefix, destination, symlinks=True)
        return destination

    destination.mkdir()
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as package:
            package.extractall(destination)
    else:
        if not hasattr(tarfile, "data_filter"):
            raise ValueError("tar extraction requires Python tarfile.data_filter. "
                             "Extract the package and use --prefix")
        with tarfile.open(archive) as package:
            package.extractall(destination, filter="data")

    candidates = [destination, *(path for path in destination.iterdir() if path.is_dir())]
    prefixes = [path for path in candidates if (path / "include/eshkol/eshkol_ffi.h").is_file()]
    if len(prefixes) != 1:
        raise ValueError("expected one embedding SDK prefix in the archive")
    return prefixes[0].resolve()


def run_consumer(executable, cwd, env):
    result = subprocess.run(
        [str(executable)],
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        timeout=300,
        check=False,
    )
    print(result.stdout, end="", flush=True)
    return result


def verify_consumer(args, work):
    prefix = stage_package(args.prefix, args.archive, work / "installed SDK").resolve()
    source = work / "consumer-src"
    build = work / "consumer-build"
    shutil.copytree(FIXTURE, source)

    subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
         *args.cmake_arg, f"-DESHKOL_SDK_PREFIX:PATH={prefix}"],
        cwd=work, check=True, timeout=600,
    )
    executable, config, stdlib = (
        Path(line).resolve()
        for line in (build / "consumer-Release.txt").read_text(encoding="utf-8").splitlines()
    )
    if not config.is_relative_to(prefix) or not stdlib.is_relative_to(prefix):
        raise ValueError("CMake did not resolve the embedding SDK and stdlib from the staged prefix")

    subprocess.run(
        ["cmake", "--build", str(build), "--config", "Release"],
        cwd=work, check=True, timeout=600,
    )

    for name in ("stdlib.bc", "stdlib.o"):
        artifact = stdlib / name
        if artifact.resolve(strict=True).parent != stdlib or not artifact.is_file():
            raise ValueError(f"the staged SDK must contain {artifact}")
    for cache in stdlib.glob("stdlib-jit-*.o"):
        cache.unlink()

    runtime = work / "consumer-run"
    runtime.mkdir()
    env = {name: value for name, value in os.environ.items() if not name.startswith("ESHKOL_")}
    env["ESHKOL_LIB_DIR"] = str(stdlib)
    env["ESHKOL_JIT_CACHE"] = "0"
    if os.name == "nt":
        env["PATH"] = os.pathsep.join((str(prefix / "bin"), env.get("PATH", "")))

    result = run_consumer(executable, runtime, env)
    result.check_returncode()
    lines = result.stdout.splitlines()
    loaded = [
        Path(line[len(message):]).resolve()
        for line in lines for message in STDLIB_MESSAGES if line.startswith(message)
    ]
    if EXPECTED_OUTPUT not in lines or not loaded or any(path.parent != stdlib for path in loaded):
        raise ValueError("the consumer must return integer 42 and load the staged standard library")

    invalid = work / "invalid-stdlib"
    invalid.mkdir()
    (invalid / "stdlib.bc").write_bytes(b"invalid stdlib bitcode\n")
    env["ESHKOL_LIB_DIR"] = str(invalid)
    refused = run_consumer(executable, runtime, env)
    if refused.returncode != 1 or "rejecting stdlib.bc" not in refused.stdout:
        raise ValueError("the consumer did not refuse invalid stdlib bitcode during initialization")

    print(f"Installed embedding SDK consumer passed: {prefix}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    package = parser.add_mutually_exclusive_group(required=True)
    package.add_argument("--prefix", type=Path, help="installed SDK prefix to copy")
    package.add_argument("--archive", type=Path, help="embedding SDK release archive to extract")
    parser.add_argument("--cmake-arg", action="append", default=[], help="argument to consumer CMake configure")
    parser.add_argument("--work-dir", type=Path, help="retain a scratch directory here for inspection")
    args = parser.parse_args()

    if args.work_dir:
        args.work_dir.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="eshkol-sdk-consumer-", dir=args.work_dir)).resolve()
        print(f"Consumer work directory: {work}", flush=True)
        verify_consumer(args, work)
    else:
        with tempfile.TemporaryDirectory(prefix="eshkol-sdk-consumer-") as path:
            verify_consumer(args, Path(path).resolve())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        sys.exit(f"Embedding SDK consumer failed: {error}")
