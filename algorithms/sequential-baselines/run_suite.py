#!/usr/bin/env python3

import argparse
import json
import pathlib
import subprocess
import sys


PACKAGE_ROOT = pathlib.Path(__file__).resolve().parent
DEFAULT_ALGORITHMS = [
    "tarjan",
    "gabow",
    "pearce",
    "tarjan-zwick",
]


def parse_algorithms(value):
    if isinstance(value, list):
        algorithms = value
    else:
        algorithms = value.split(",")
    algorithms = [name.strip() for name in algorithms if name.strip()]
    if not algorithms:
        raise ValueError("at least one algorithm is required")
    duplicates = {
        name for name in algorithms if algorithms.count(name) > 1
    }
    if duplicates:
        raise ValueError(
            "duplicate algorithms: " + ", ".join(sorted(duplicates))
        )
    return algorithms


def load_config(path):
    if path is None:
        return {}, pathlib.Path.cwd()
    config_path = pathlib.Path(path).resolve()
    with config_path.open(encoding="utf-8") as source:
        return json.load(source), config_path.parent


def resolve_from(base, value):
    path = pathlib.Path(value).expanduser()
    return path if path.is_absolute() else (base / path).resolve()


def resolve_graph(graph, graph_directory):
    path = pathlib.Path(graph).expanduser()
    if path.is_absolute() or path.parent != pathlib.Path("."):
        candidate = path
    else:
        candidate = graph_directory / path
    if candidate.suffix != ".bgr":
        candidate = candidate.with_suffix(".bgr")
    candidate = candidate.resolve()
    if not candidate.is_file():
        raise FileNotFoundError(f"graph does not exist: {candidate}")
    return candidate


def stream_command(command, log_path, dry_run):
    print(
        "SUITE_COMMAND "
        + json.dumps(
            {"command": [str(argument) for argument in command]},
            separators=(",", ":"),
        ),
        flush=True,
    )
    if dry_run:
        return

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(argument) for argument in command],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
        returncode = process.wait()
    if returncode != 0:
        raise subprocess.CalledProcessError(returncode, command)


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Run selected SCC algorithms in one process per graph so the "
            "checked BGR mapping and validation are shared."
        )
    )
    parser.add_argument(
        "graphs",
        nargs="*",
        help="graph names or BGR paths; overrides config graphs",
    )
    parser.add_argument(
        "--config",
        help="JSON configuration file (see suite.example.json)",
    )
    parser.add_argument("--graph-dir", help="directory containing BGR files")
    parser.add_argument(
        "--algorithms",
        help="comma-separated algorithm names; overrides config",
    )
    parser.add_argument("--binary", help="path to scc_benchmark")
    parser.add_argument("--labels-dir", help="optional label output root")
    parser.add_argument("--results-dir", help="per-graph log directory")
    parser.add_argument(
        "--time-limit",
        type=float,
        help="per-algorithm SCC-phase limit in seconds",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="build scc_benchmark before running",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="validate configuration and print commands only",
    )
    parser.add_argument(
        "--no-progress",
        action="store_true",
        help="disable SCC progress records",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    config, config_base = load_config(args.config)

    graph_directory = resolve_from(
        config_base,
        args.graph_dir
        or config.get("graph_dir", "."),
    )
    graph_names = args.graphs or config.get("graphs", [])
    if not graph_names:
        raise ValueError(
            "no graphs selected; pass graph names or edit config graphs"
        )
    graphs = [
        resolve_graph(graph, graph_directory) for graph in graph_names
    ]

    algorithms = parse_algorithms(
        args.algorithms
        if args.algorithms is not None
        else config.get("algorithms", DEFAULT_ALGORITHMS)
    )
    binary = resolve_from(
        config_base,
        args.binary
        or config.get("binary", str(PACKAGE_ROOT / "scc_benchmark")),
    )
    results_directory = resolve_from(
        config_base,
        args.results_dir
        or config.get("results_dir", "suite-results"),
    )

    labels_value = (
        args.labels_dir
        if args.labels_dir is not None
        else config.get("labels_dir")
    )
    labels_directory = (
        resolve_from(config_base, labels_value)
        if labels_value
        else None
    )
    time_limit = (
        args.time_limit
        if args.time_limit is not None
        else float(config.get("time_limit_seconds", 0))
    )
    if time_limit < 0:
        raise ValueError("time limit must be non-negative")

    if args.build:
        subprocess.run(
            ["make", "-C", str(PACKAGE_ROOT), "scc_benchmark"],
            check=True,
        )
    if not args.dry_run and not binary.is_file():
        raise FileNotFoundError(
            f"benchmark binary does not exist: {binary}; use --build"
        )

    print(
        "SUITE_PLAN "
        + json.dumps(
            {
                "graphs": [str(graph) for graph in graphs],
                "algorithms": algorithms,
                "binary": str(binary),
                "time_limit_seconds": time_limit,
                "labels_dir": (
                    str(labels_directory)
                    if labels_directory is not None
                    else None
                ),
                "load_policy": "once_per_graph",
                "transpose_policy": "not_required_by_selected_algorithms",
            },
            separators=(",", ":"),
        ),
        flush=True,
    )

    for graph in graphs:
        command = [
            binary,
            graph,
            "--algorithms",
            ",".join(algorithms),
            "--time-limit",
            str(time_limit),
        ]
        if args.no_progress or config.get("report_progress") is False:
            command.append("--no-progress")
        if labels_directory is not None:
            command.extend(
                [
                    "--labels-dir",
                    labels_directory / graph.stem,
                ]
            )
        stream_command(
            command,
            results_directory / f"{graph.stem}.log",
            args.dry_run,
        )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"SUITE_ERROR {error}", file=sys.stderr)
        raise SystemExit(1)
