# Code Map

This repo is organized around one main experiment binary, `./test`, plus
wrappers that run repeatable sweeps.

## Main C++ Experiment

`test_fuzzypsi.cpp` owns the CLI, top-level validation, and dispatch. The
implementation is intentionally included as one translation unit because much
of the code relies on templates, feature macros, and shared globals.

The split files under `test_fuzzypsi_parts/` are:

- `globals_and_utils.inc.cpp`: runtime flags, paths, argument helpers, timers, and shared utility functions.
- `datasets.inc.cpp`: dataset preset selection and generated-Client dataset modes.
- `grid_search.inc.cpp`: direct Cuckoo L/k/w calibration.
- `cuckoo_prf_keys.inc.cpp`: PRF/OPRF-derived Cuckoo insertion and query key logic. The filter is only ever keyed through here. The evaluation itself lives in `oprf/oprf_eval.inc.cpp`; `--OPRF` selects oblivious evaluation over local, and calibration always evaluates locally.
- `experiment_runner.inc.cpp`: normal fuzzy PSI run loop, metrics, and reporting.
- `cli_args.inc.cpp`: command-line parsing, flag defaults, and argument validation.

PIR lives under `pir/`, with everything both schemes share at the top level:

- `pir/pir_row_reader.h`: the `fuzzy_pets_pir::RowReader` interface every scheme implements — read rows, report per-phase runtime and communication.
- `pir/pir_membership.inc.cpp`: Cuckoo membership queries against any `RowReader`, batched and per-item.
- `pir/pir_scheme.inc.cpp`: which scheme is active, and the per-run cost totals summed over its readers.

Each scheme adds only its own reader: `pir/checklist/checklist_readers.inc.cpp` and `pir/batchpir/batchpir_readers.inc.cpp`, included by `test_fuzzypsi.cpp` the same way. A new scheme needs a `RowReader` implementation and a reader builder; the membership and reporting code above then works unchanged.

When adding code, prefer putting helper functions near the behavior they serve.
Keep `test_fuzzypsi.cpp` mostly limited to help text and mode dispatch.

## Experiment Wrappers

`run_cuckoo_server_sweep.sh` is the main paper-style runner. It does four jobs:

- builds `./test`;
- selects or calibrates `L/k/w` (`filter_size` is carried as CSV metadata only);
- runs Cuckoo + BatchPIR + OPRF measurements;
- writes logs, summary CSVs, and seconds CSVs under `results/`.

`run_cuckoo_client_sweep.sh` sweeps |C| for a fixed calibration. Both runners pass `--OPRF` for measurement runs and omit it for calibration, which evaluates the PRF locally by design.

## Dependencies

The large directories are dependency/vendor trees:

- `pir/checklist/`: ChecklistPIR — the `pir.Punc` port and psetggm.
- `pir/batchpir/vectorized_batchpir/`: BatchPIR client/server implementation.
- `oprf/droidcrypto/`: droidCrypto, which provides `libdroidcrypto` and the `oprf_server` binary.

Treat these as external code unless the experiment adapter specifically needs a
change.

`Utils/` holds the pieces shared across those trees, so no area has to reach
into another for them: `AES.{h,cpp}`, `Defines.h`, `intrinsics.h` (with
`sse2neon.h` for ARM) and `gsl-lite.hpp` came from psetggm and now serve both
the OPRF local PRF and ChecklistPIR.

