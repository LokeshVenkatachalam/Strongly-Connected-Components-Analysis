#!/usr/bin/env python3

import argparse
import csv
import json
import re
from pathlib import Path


def parse_elapsed(value: str) -> float:
    parts = [float(part) for part in value.split(":")]
    if len(parts) == 2:
        return parts[0] * 60 + parts[1]
    if len(parts) == 3:
        return parts[0] * 3600 + parts[1] * 60 + parts[2]
    raise ValueError(f"unsupported elapsed time: {value}")


def process_wall_seconds(text: str) -> float:
    match = re.search(
        r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([0-9:.]+)",
        text,
    )
    if not match:
        raise ValueError("missing process wall-clock time")
    return parse_elapsed(match.group(1))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("conversion_log", type=Path)
    parser.add_argument("gbbs_log", type=Path)
    parser.add_argument("output_csv", type=Path)
    args = parser.parse_args()

    conversion_text = args.conversion_log.read_text(errors="replace")
    gbbs_text = args.gbbs_log.read_text(errors="replace")
    rows: list[dict[str, object]] = []

    def add(
        scope: str,
        operation: str,
        seconds: float,
        *,
        invocation: object = "",
        kind: object = "",
        round_number: object = "",
        is_total: bool = False,
    ) -> None:
        rows.append(
            {
                "scope": scope,
                "invocation": invocation,
                "kind": kind,
                "operation": operation,
                "round": round_number,
                "seconds": f"{seconds:.9f}",
                "is_total": str(is_total).lower(),
            }
        )

    conversion_result = None
    for line in conversion_text.splitlines():
        if line.startswith("BGR_TO_GBBS_STAGE "):
            record = json.loads(line.removeprefix("BGR_TO_GBBS_STAGE "))
            add("conversion", record["stage"], record["seconds"])
        elif line.startswith("BGR_TO_GBBS_RESULT "):
            conversion_result = json.loads(
                line.removeprefix("BGR_TO_GBBS_RESULT ")
            )
    if conversion_result is None:
        raise ValueError("missing BGR_TO_GBBS_RESULT")
    add(
        "conversion",
        "instrumented_total",
        conversion_result["seconds"],
        is_total=True,
    )
    conversion_process_wall = process_wall_seconds(conversion_text)
    add(
        "conversion",
        "process_wall",
        conversion_process_wall,
        is_total=True,
    )

    loader_total = None
    runner_totals: list[float] = []
    cleanup_total = 0.0
    for line in gbbs_text.splitlines():
        if line.startswith("GBBS_WALL_OPERATION "):
            record = json.loads(line.removeprefix("GBBS_WALL_OPERATION "))
            operation = record["operation"]
            if operation.startswith("loader."):
                scope = "loader"
            elif operation.startswith("cleanup."):
                scope = "cleanup"
            else:
                scope = "runner"
            is_total = operation.endswith(".total") or operation in {
                "runner_total",
                "scc",
            }
            add(
                scope,
                operation,
                record["seconds"],
                invocation=record.get("invocation", ""),
                kind=record.get("kind", ""),
                is_total=is_total,
            )
            if operation == "loader.total":
                loader_total = record["seconds"]
            elif operation == "runner_total":
                runner_totals.append(record["seconds"])
            elif operation.startswith("cleanup."):
                cleanup_total += record["seconds"]
        elif line.startswith("GBBS_SCC_WALL "):
            record = json.loads(line.removeprefix("GBBS_SCC_WALL "))
            operation = record["operation"]
            add(
                "algorithm",
                operation,
                record["seconds"],
                invocation=record["invocation"],
                kind="warmup" if record["invocation"] == 0 else "measured",
                round_number=record["round"],
                is_total=operation.endswith("_total")
                or operation == "algorithm.total",
            )

    if loader_total is None or len(runner_totals) != 2:
        raise ValueError("incomplete GBBS operation trace")
    gbbs_process_wall = process_wall_seconds(gbbs_text)
    add("gbbs", "process_wall", gbbs_process_wall, is_total=True)
    attributed = loader_total + sum(runner_totals) + cleanup_total
    add(
        "gbbs",
        "unattributed_process_overhead",
        gbbs_process_wall - attributed,
    )
    add(
        "pipeline",
        "conversion_plus_gbbs_process_wall",
        conversion_process_wall + gbbs_process_wall,
        is_total=True,
    )

    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    with args.output_csv.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
