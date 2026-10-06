# Direct GBBS BGR-pair loader test

Job `42534` built the benchmark-matched GBBS BGSS16 source at commit `21550b0`
with a direct forward-BGR + reverse-BGR loader and `-march=znver2`.

The loader:

- Maps and validates both BGR headers and dimensions.
- Validates CSR offsets and destination ranges.
- Copies both unaligned BGR destination arrays into aligned GBBS storage.
- Builds only outgoing and incoming vertex metadata.
- Does not convert an intermediate file, sort edge pairs, or construct a
  transpose.

## Test graph

The generated directed fixture has seven vertices and ten arcs, including a
duplicate arc and a self-loop. Its exact SCC partition has four components with
largest size three.

| Implementation | SCCs | Largest SCC | Result |
|:---------------|-----:|------------:|:-------|
| Legacy forward-only GBBS loader | 4 | 3 | Pass |
| Direct forward/reverse BGR loader | 4 | 3 | Pass |

Both the warm-up and measured invocation produced the expected summary. The
direct loader reported:

```text
GBBS_BGR_PAIR_RESULT {"vertices":7,"edges":10}
```

A reverse BGR with deliberately mismatched dimensions was rejected. The test
job completed successfully in 27 seconds with two allocated CPUs, charging
0.015 CPU-hours. The resulting binary SHA-256 is:

```text
44d4c5478fc7e06240325c2647e74c3be15c7255bd5abfcf31f5720b3a25e26e
```

Apply [`tools/gbbs_bgr_pair_loader.patch`](../tools/gbbs_bgr_pair_loader.patch)
from the GBBS repository root, then invoke:

```bash
StronglyConnectedComponents \
  -rounds 1 -stats \
  -bgr-reverse graph.reverse.bgr \
  graph.forward.bgr
```

The loader deliberately copies destinations into aligned arrays because BGR's
13-byte header leaves its CSR payload unaligned for GBBS's typed neighbor
representation. It eliminates disk-format conversion and transpose
construction, but is not a zero-copy loader.
