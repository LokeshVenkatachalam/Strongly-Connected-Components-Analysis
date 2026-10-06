#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile


FORWARD = [
    [1, 1],
    [2],
    [0, 3],
    [4],
    [3, 5],
    [5, 6],
    [],
]

REVERSE = [
    [2],
    [0, 0],
    [1],
    [2, 4],
    [3],
    [4, 5],
    [5],
]


def write_bgr(path: Path, adjacency: list[list[int]]) -> None:
    offsets: list[int] = []
    destinations: list[int] = []
    for row in adjacency:
        destinations.extend(row)
        offsets.append(len(destinations))
    with path.open("wb") as output:
        output.write(
            struct.pack("<BIQ", 0x02, len(adjacency), len(destinations))
        )
        output.write(struct.pack(f"<{len(offsets)}Q", *offsets))
        output.write(
            struct.pack(f"<{len(destinations)}I", *destinations)
        )


def run(command: list[str], env: dict[str, str]) -> str:
    result = subprocess.run(
        command,
        check=True,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return result.stdout


def validate_result(output: str) -> None:
    components = re.findall(r"^n_scc = (\d+)$", output, re.MULTILINE)
    largest = re.findall(
        r"^Largest StronglyConnectedComponents has (\d+) vertices$",
        output,
        re.MULTILINE,
    )
    if components != ["4", "4"]:
        raise RuntimeError(f"unexpected SCC counts: {components}")
    if largest != ["3", "3"]:
        raise RuntimeError(f"unexpected largest SCC values: {largest}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--direct", required=True)
    parser.add_argument("--legacy", required=True)
    parser.add_argument("--converter", required=True)
    parser.add_argument("--checker")
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()

    env = os.environ.copy()
    env["PARLAY_NUM_THREADS"] = str(args.threads)

    with tempfile.TemporaryDirectory(prefix="gbbs-bgr-pair-") as temporary:
        root = Path(temporary)
        forward = root / "forward.bgr"
        reverse = root / "reverse.bgr"
        converted = root / "forward.gbbs.bin"
        write_bgr(forward, FORWARD)
        write_bgr(reverse, REVERSE)

        if args.checker:
            subprocess.run(
                ["python3", args.checker, str(forward)], check=True, env=env
            )
            subprocess.run(
                ["python3", args.checker, str(reverse)], check=True, env=env
            )

        run(
            [
                args.converter,
                str(forward),
                str(converted),
                str(args.threads),
            ],
            env,
        )
        legacy_output = run(
            [
                args.legacy,
                "-rounds",
                "1",
                "-stats",
                "-b",
                str(converted),
            ],
            env,
        )
        direct_output = run(
            [
                args.direct,
                "-rounds",
                "1",
                "-stats",
                "-bgr-reverse",
                str(reverse),
                str(forward),
            ],
            env,
        )
        validate_result(legacy_output)
        validate_result(direct_output)
        if "GBBS_BGR_PAIR_RESULT " not in direct_output:
            raise RuntimeError("missing direct-loader result marker")

        mismatched = root / "mismatched-reverse.bgr"
        data = bytearray(reverse.read_bytes())
        data[1:5] = struct.pack("<I", 8)
        mismatched.write_bytes(data)
        mismatch = subprocess.run(
            [
                args.direct,
                "-rounds",
                "1",
                "-stats",
                "-bgr-reverse",
                str(mismatched),
                str(forward),
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if mismatch.returncode == 0:
            raise RuntimeError("mismatched BGR dimensions were accepted")

    print(
        "GBBS_BGR_PAIR_TEST "
        + json.dumps(
            {
                "vertices": 7,
                "edges": 10,
                "components": 4,
                "largest_component": 3,
                "threads": args.threads,
                "mismatch_rejected": True,
            },
            separators=(",", ":"),
        )
    )


if __name__ == "__main__":
    main()
