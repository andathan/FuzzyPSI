# `test_fuzzypsi_parts`

`test_fuzzypsi.cpp` includes these files directly to keep the experiment as one
translation unit while avoiding a single enormous source file.

## Files

- `globals_and_utils.inc.cpp`: shared flags, paths, argument parsing helpers, timers, and utility functions.
- `datasets.inc.cpp`: DB preset selection and generated dataset modes.
- `grid_search.inc.cpp`: direct Cuckoo L/k/w calibration.
- `cuckoo_prf_keys.inc.cpp`: PRF/OPRF-derived Cuckoo keying; the evaluation itself is in `oprf/oprf_eval.inc.cpp`.
- `pir_readers.inc.cpp`: PIR storage/read adapters.
- `pir_membership.inc.cpp`: Cuckoo private membership query logic.
- `experiment_runner.inc.cpp`: normal fuzzy PSI execution and metrics.
- `cli_args.inc.cpp`: command-line parsing and compatibility validation.

## Editing Rule of Thumb

Put new code beside the behavior it supports. Keep `test_fuzzypsi.cpp` focused
on help text, validating incompatible modes, and dispatching to these
implementation fragments.
