#!/usr/bin/env python3

import json
import pathlib
import struct
import subprocess
import sys
import tempfile


ROWS = [
    [1],
    [2],
    [0, 3],
    [3],
    [5],
    [4],
    [7],
    [6, 5],
]
EXPECTED_LABELS = [0, 0, 0, 3, 4, 4, 6, 6]
EXPECTED_COMPONENTS = 4
EXPECTED_LARGEST = 3


def write_bgr(path: pathlib.Path, flags: int, rows=ROWS) -> None:
    node_format = "Q" if flags & 1 else "I"
    edge_format = "Q" if flags & 2 else "I"
    destinations = [destination for row in rows for destination in row]
    row_ends = []
    edge_count = 0
    for row in rows:
        edge_count += len(row)
        row_ends.append(edge_count)

    with path.open("wb") as output:
        output.write(bytes([flags]))
        output.write(struct.pack("<" + node_format, len(rows)))
        output.write(struct.pack("<" + edge_format, edge_count))
        output.write(
            struct.pack("<" + edge_format * len(row_ends), *row_ends)
        )
        output.write(
            struct.pack(
                "<" + node_format * len(destinations), *destinations
            )
        )
        if flags & 8:
            output.write(
                struct.pack("<" + "f" * edge_count, *([1.0] * edge_count))
            )


def run(command, expected_returncode=0):
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        capture_output=True,
    )
    if completed.returncode != expected_returncode:
        raise AssertionError(
            f"{command} returned {completed.returncode}, expected "
            f"{expected_returncode}\nstdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return completed


def parse_prefixed_json(output: str, prefix: str):
    records = [
        line[len(prefix) :]
        for line in output.splitlines()
        if line.startswith(prefix)
    ]
    if len(records) != 1:
        raise AssertionError(f"expected one {prefix!r} record in {output!r}")
    return json.loads(records[0])


def check_comparison(binary: str, graph: pathlib.Path, directory: pathlib.Path):
    label_files = []
    for algorithm in ("tarjan", "gabow", "pearce", "tarjan-zwick"):
        labels = directory / f"{graph.stem}.{algorithm}.labels.bin"
        completed = run([binary, str(graph), algorithm, str(labels), "60"])
        record = parse_prefixed_json(completed.stdout, "SCC_RESULT ")
        assert record["algorithm"] == algorithm
        assert record["nodes"] == len(ROWS)
        assert record["edges"] == sum(map(len, ROWS))
        assert record["components"] == EXPECTED_COMPONENTS
        assert record["largest"] == EXPECTED_LARGEST
        if algorithm == "pearce":
            assert record["scanned_edges"] == 2 * sum(map(len, ROWS))
        else:
            assert 0 < record["scanned_edges"] <= sum(map(len, ROWS))
        actual = list(struct.unpack("<" + "I" * len(ROWS), labels.read_bytes()))
        assert actual == EXPECTED_LABELS
        label_files.append(labels)

    assert label_files[0].read_bytes() == label_files[1].read_bytes()


def check_benchmark(
    binary: str,
    graph: pathlib.Path,
    directory: pathlib.Path,
):
    labels_directory = directory / "benchmark-labels"
    completed = run(
        [
            binary,
            str(graph),
            "--algorithms",
            "tarjan,gabow,pearce,tarjan-zwick",
            "--labels-dir",
            str(labels_directory),
            "--time-limit",
            "60",
            "--validation-threads",
            "2",
            "--no-progress",
        ]
    )
    load_records = [
        line for line in completed.stdout.splitlines()
        if line.startswith("BGR_LOAD ")
    ]
    result_records = [
        json.loads(line[len("SCC_RESULT ") :])
        for line in completed.stdout.splitlines()
        if line.startswith("SCC_RESULT ")
    ]
    summaries = [
        line for line in completed.stdout.splitlines()
        if line.startswith("BENCHMARK_RESULT ")
    ]
    assert len(load_records) == 1
    assert len(result_records) == 4
    assert len(summaries) == 1
    assert [record["algorithm"] for record in result_records] == [
        "tarjan",
        "gabow",
        "pearce",
        "tarjan-zwick",
    ]
    expected_bytes = struct.pack(
        "<" + "I" * len(EXPECTED_LABELS), *EXPECTED_LABELS
    )
    for record in result_records:
        assert record["components"] == EXPECTED_COMPONENTS
        assert record["largest"] == EXPECTED_LARGEST
        assert pathlib.Path(record["labels"]).read_bytes() == expected_bytes


def check_exact(binary: str, graph: pathlib.Path):
    completed = run([binary, str(graph)])
    records = [
        json.loads(line)
        for line in completed.stdout.splitlines()
        if line.startswith("{")
    ]
    assert len(records) == 1
    record = records[0]
    assert record["status"] == "complete"
    assert record["exact"] is True
    assert record["scc_count"] == EXPECTED_COMPONENTS
    assert record["largest_scc_vertices"] == EXPECTED_LARGEST
    assert record["tarjan_edges_visited"] == sum(map(len, ROWS))


def check_invalid_endpoint(
    comparison_binary: str,
    exact_binary: str,
    directory: pathlib.Path,
):
    graph = directory / "invalid-endpoint.bgr"
    invalid_rows = [row[:] for row in ROWS]
    invalid_rows[-1][-1] = len(ROWS)
    write_bgr(graph, 0, invalid_rows)

    comparison = run(
        [comparison_binary, str(graph), "tarjan", "-"],
        expected_returncode=1,
    )
    assert "destination out of range" in comparison.stderr

    exact = run([exact_binary, str(graph)], expected_returncode=2)
    record = next(
        json.loads(line)
        for line in exact.stdout.splitlines()
        if line.startswith("{")
    )
    assert record["status"] == "invalid_endpoints"
    assert record["invalid_endpoints"] == 1


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: test_bgr_cli.py SCC_COMPARE SCC_BENCHMARK EXACT_SCC"
        )
    comparison_binary = str(pathlib.Path(sys.argv[1]).resolve())
    benchmark_binary = str(pathlib.Path(sys.argv[2]).resolve())
    exact_binary = str(pathlib.Path(sys.argv[3]).resolve())

    with tempfile.TemporaryDirectory(prefix="scc-bgr-test-") as temporary:
        directory = pathlib.Path(temporary)
        for flags in (0, 1, 2, 3, 8, 9, 10, 11):
            graph = directory / f"fixture-{flags}.bgr"
            write_bgr(graph, flags)
            check_comparison(comparison_binary, graph, directory)
            check_exact(exact_binary, graph)
        check_benchmark(
            benchmark_binary, directory / "fixture-0.bgr", directory
        )
        check_invalid_endpoint(
            comparison_binary, exact_binary, directory
        )

    print(
        "BGR_CLI_TEST_OK "
        "formats=8 algorithms=tarjan,gabow,pearce,tarjan-zwick "
        "invalid_endpoint=checked"
    )


if __name__ == "__main__":
    main()
