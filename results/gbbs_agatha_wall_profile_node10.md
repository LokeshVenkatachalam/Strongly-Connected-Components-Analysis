# AGATHA GBBS wall-time profile

Jobs `42471` and `42472` profiled the benchmark-matched GBBS BGSS16 source at
commit `21550b0` on Node 10 with Zen 2 optimization. The converter used eight
CPUs; GBBS used 128 workers. Both warm-up and measured executions found 13 SCCs
with a largest SCC of 183,963,989 vertices.

The GBBS executable exited successfully. Slurm marked job `42472` failed only
because the post-run validator used an over-escaped regular expression. The
saved output was independently revalidated before the temporary graph was
removed.

## End-to-end wall time

| Operation | Wall time |
|:----------|----------:|
| BGR-to-GBBS process | 100.350000 s |
| GBBS executable process | 173.740000 s |
| **Conversion plus GBBS** | **274.090000 s (4m 34.090s)** |
| Slurm elapsed for both jobs | 278 s (4m 38s) |

## BGR-to-GBBS conversion

| Operation | Wall time |
|:----------|----------:|
| Map input | 0.000278 s |
| Validate header and calculate sizes | 0.002811 s |
| Create output | 0.000044 s |
| Reserve output space | 0.154684 s |
| Write header | 0.000024 s |
| Write offsets | 2.323109 s |
| Write destinations | 95.284539 s |
| Flush and close | 0.060137 s |
| Publish output | 0.000068 s |
| **Instrumented conversion total** | **97.825912 s** |
| **Complete conversion process** | **100.350000 s** |

`mmap` is lazy, so input page-fault time is charged to the operations that
subsequently read the mapped data rather than to `Map input`.

## GBBS graph loader and reverse construction

| Operation | Wall time |
|:----------|----------:|
| Map forward file | 0.000049 s |
| Parse forward header | 0.002496 s |
| Allocate outgoing metadata | 0.091247 s |
| Populate outgoing metadata | 0.574500 s |
| Allocate reverse offsets | 0.000044 s |
| Initialize reverse offsets | 0.054191 s |
| Allocate `(destination, source)` pairs | 0.000029 s |
| Populate edge pairs | 5.383392 s |
| **Integer-sort edge pairs** | **126.131995 s** |
| Allocate reverse edges | 0.000048 s |
| Populate reverse edges | 1.818964 s |
| Release edge-pair array | 17.762119 s |
| Scan/fill reverse offsets | 0.024251 s |
| Allocate incoming metadata | 0.000462 s |
| Populate incoming metadata | 0.110441 s |
| **Loader total** | **151.955859 s** |

The unnecessary edge-pair sort consumed 83.0% of loader time. A GBBS dual-CSR
input built from the already prepared reverse BGR would eliminate this step and
the large temporary edge-pair array.

## SCC and result operations

| Operation | Warm-up | Measured |
|:----------|--------:|---------:|
| Algorithm initialization | 0.239197 s | 0.179863 s |
| Initial max-degree FW-BW round | 2.121185 s | 2.144178 s |
| Filter initial residual | 0.260636 s | 0.261693 s |
| All 13 residual rounds | 0.012622 s | 0.013106 s |
| Normalize labels | 0.012894 s | 0.012861 s |
| **SCC algorithm total** | **2.647090 s** | **2.612281 s** |
| Count components | 0.028100 s | 0.027731 s |
| Largest-component statistics | 0.773466 s | 0.781797 s |
| Release labels | 0.000001 s | 0.000001 s |
| **Complete runner invocation** | **3.449116 s** | **3.422229 s** |

Initial max-degree FW-BW detail:

| Operation | Warm-up | Measured |
|:----------|--------:|---------:|
| Select maximum-outdegree pivot | 0.019374 s | 0.019470 s |
| Backward search | 1.056415 s | 1.080649 s |
| Forward search | 1.029374 s | 1.027777 s |
| Classify vertices | 0.015576 s | 0.015841 s |

## GBBS process reconciliation

| Operation | Wall time |
|:----------|----------:|
| Loader and reverse construction | 151.955859 s |
| Warm-up runner | 3.449116 s |
| Measured runner | 3.422229 s |
| Explicit metadata release | 0.000003 s |
| Unattributed process/runtime teardown | 14.912794 s |
| **Complete GBBS executable** | **173.740000 s** |

The unattributed remainder includes return-time destruction of loader
temporaries, Parlay runtime teardown, mapped-file/process cleanup, wrapper
startup, and output-pipeline overhead.

Peak resident memory was 32,458,656 KiB for conversion and 237,914,900 KiB
(226.9 GiB) for GBBS. There was no swapping. Binary SHA-256 values:

- Profiled GBBS:
  `2a46f66d0e57588771a3c180e5d62c6b9de2ed97faf25ed25502a3afb4277bfb`
- Profiled converter:
  `421c4608ea06a4a41f7482bf2223ac6cbae6666f0d5d0d4f1a7ccdeacc497212`

Building, converting, and running the profile charged **6.500 CPU-hours**:
0.016 for the build, 0.227 for conversion, and 6.258 for GBBS.

The complete 165-row operation trace is in
[`gbbs_agatha_wall_profile_node10.csv`](gbbs_agatha_wall_profile_node10.csv).
