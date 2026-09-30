

## Repository Map

- `test_fuzzypsi.cpp`: main CLI and top-level experiment dispatch.
- `test_fuzzypsi_parts/`: focused fragments of the FuzzyPSI protocol, included by `test_fuzzypsi.cpp`.
- `run_cuckoo_server_sweep.sh`: script to run multiple tests for multiple |S|, |C|.
- `run_cuckoo_client_sweep.sh`: script to sweep |C| for a fixed calibration.
- `pir/batchpir/`: BatchPIR readers 
- `pir/checklist/`: ChecklistPIR readers
- `cuckoo_filter.h`, `lsh*.h`: core filter and LSH code.
- `oprf/droidcrypto/`, `pir/batchpir/vectorized_batchpir/`: third-party dependencies.

## Staged functionality demo

The small deterministic demo explains and checks the protocol one component at
a time.

```bash
make demo
./fuzzy_pets_demo
```


## Debugging
To enable debugging messages:
```
--debug=1
```


## Protocol Implementation

### Overview 
The main file is test_fuzzypsi.cpp. It runs a calibration (calibrate_lkw) if asked (meaning test all possible combinations of E2LSH parameters to get the target FP/FN rate) and than runs the actual protocol fuzzypsi()

The most important file however is test_fuzzypsi_parts/experiment_runner.inc.cpp. This is where our protocol is implemented:

Step 1: Load databases: for Gowalla, the default `--db=gowalla` preset loads the stored protocol split from `datasets/calibrations/gowalla_protocol_server.json`, `datasets/calibrations/gowalla_protocol_client_far.json` (client points far from any server point), and `datasets/calibrations/gowalla_protocol_client_close.json` (client points close to a server point). Sanity checks are available (please uncomment them if you wish).

Step 2: Create filter: initialises the filter, inserts all server values and does the PIR + OPRF setup.

Step 3: Membership query. For the client elements we call batched_pir_memberships_for_reader() which returns the membership status. The membership is appended on the list batched_reported.

Step 4: Membership Evaluation: For each client value we take the ground truth from client_is_close[i]. Then we compare with the reported one and calculate the statistics (FP/FN).

Step 5: Timing: We log and print the runtime of each component and bandwith where applicable. 


### Main Functionalities 

Below is a list of where the main functionalities of our protocol are implemented:

- `lsh/lsh_e2lsh.h`
- Cuckoo Filters: cuckoo_filter.h and test_fuzzypsi_parts/cuckoo_prf_keys.inc.cpp
- PIR: pir/batchpir/batchpir_row_reader.cpp includes the third-party BatchPIR lirbary (pir/batchpir/vectorized_batchpir)

### Insertions and Membership check with OPRF

The GCAES server is provided as a separate executable (see the execution instructions above). Therefore, an OPRF server must already be running before executing our scripts.

### Filter Construction

The filter setup begins at:

test_fuzzypsi_parts/cuckoo_prf_keys.inc.cpp → build_cuckoo_canonical_prf_range

Following the call chain eventually leads to:

oprf_eval_blocks

which enters the Disco codebase at:

disco/contact-discovery/oprf_c/disco_gcaes_oprf_adapter.cpp

From this point onward, the execution follows Disco's OPRF implementation. The code traverses the libdroidcrypto library and ultimately invokes the functionality provided by OPRFAESPSIServer.

### Membership Queries

Membership queries follow a similar execution path. However, instead of invoking the server-side functionality, the chain eventually reaches the client-side implementation and calls OPRFAESPSIClient from the Disco library.

