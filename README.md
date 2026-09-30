This branch includes our code for the VLDB submission. 

# Fuzzy PETS

Implementation of our Fuzzy PSI protocol using Cuckoo Filters, GCAES, E2LSH and BatchPIR/ChecklistPIR. 
The active experiment is `./test`; the main function is in `test_fuzzypsi.cpp`.

# Installation

## Build the main binary and the SEAL library:
```
git clone https://github.com/andathan/FuzzyPSI.git
cd fuzzy_pets
python3 -m venv venv  
source venv/bin/activate 
sudo apt update 
pip install -r requirements.txt
sudo apt install nlohmann-json3-dev
sudo apt install git-lfs
git clone https://github.com/microsoft/SEAL.git  
cd SEAL  
cmake -S . -B build
cmake --build build -j$(nproc)
cd ../
make clean
make 
```

## Build the OPRF server:

Linux:
```
cd oprf/droidcrypto
rm -rf build-linux
cmake -S . -B build-linux
cmake --build build-linux --target oprf_server
cd ../..
```

Mac:
```
cd oprf/droidcrypto
rm -rf build-mac
cmake -S . -B build-mac
cmake --build build-mac --target oprf_server
cd ../..
```

## Fetch Dataset Artifacts

Dataset JSONs and zip archives are stored with Git LFS:

```bash
git lfs pull
```

The MNIST and FashionMNIST JSON files are ready after `git lfs pull`.

Unzip the Gowalla dataset before running `--db=gowalla`:

```bash
mkdir -p datasets/gowalla_client
unzip -o datasets/gowalla_client.zip -d datasets/gowalla_client
unzip -o datasets/gowalla_server.zip -d datasets
```



## Test Run

See the available flags:

```bash
./test --help
make help
```

Test run without OPRF/PIR:

```
./test --L=5 --k=5 --w=0.5 --server_size=1000 --client_size=1000
```

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



## Running the experiments of the paper

First start the OPRF server on terminal 1:

```bash
oprf/oprf_server \
  -port 50051 \
  -prf GCAES \
  -loop
```

**Do not run multiple experiments on the same server port!**

Then on another terminal:


## Location Experiment:

## Minimum possible error at 1% FN (Figure 5)
```
./run_cuckoo_server_sweep.sh -target-fp min_fp
```
## 1%/3%/5% FP at 1% FN (Figure 6, 7, 8 and Table 2)

```
./run_cuckoo_server_sweep.sh -target-fp 0.01
./run_cuckoo_server_sweep.sh -target-fp 0.03
./run_cuckoo_server_sweep.sh -target-fp 0.05
```

Besides stdout, results are also written in results/[exp_name]/summary_.csv

Exp_name follows the format [db] [target_fp] [datetime]

### Reading the filter columns of summary_.csv

The Cuckoo filter sizes itself: it starts from the estimated number of distinct entries and doubles until every insert succeeds, so no requested size reaches `./test`. Only `filter_size_mb` reports the filter that was actually built — it is parsed from the run's `Filter Size:` line, i.e. the average final slot count over `--num_runs`.

The `filter_size`, `buckets` and `total_size_bits` columns are instead derived from the calibration table's `filter_size` value (`buckets = filter_size / 4`, `total_size_bits = filter_size * 32`). That value is carried through as calibration metadata only and does not describe the filter that was measured, so it can disagree with `filter_size_mb` in the same row.

## Table2 (comparison with other works)

The following script conveniently runs all instances in Table 2:

```
./table2
```

## Scale to 1B


### BatchPIR

|Server| = 2^28:

```bash
./test --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=268435456 --client_size=1024 --L=3 --k=20 --w=0.05 --pir_batchpir_batch_size=1000 > results/scale_2_28_client_2_10_batchpir.log
./test --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=268435456 --client_size=8192 --L=3 --k=20 --w=0.05 --pir_batchpir_batch_size=1000 > results/scale_2_28_client_2_13_batchpir.log
```

|Server| = 2^29:

```bash
./test --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=536870912 --client_size=1024 --L=3 --k=20 --w=0.05 --pir_batchpir_batch_size=1000 > results/scale_2_29_client_2_10_batchpir.log
./test --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=536870912 --client_size=8192 --L=3 --k=20 --w=0.05 --pir_batchpir_batch_size=1000 > results/scale_2_29_client_2_13_batchpir.log
```

|Server| = 2^30:

```bash
./test --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=1073741824 --client_size=1024 --L=3 --k=20 --w=0.05 --pir_batchpir_batch_size=1000 > results/scale_2_30_client_2_10_batchpir.log
./test --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=1073741824 --client_size=8192 --L=3 --k=20 --w=0.05 --pir_batchpir_batch_size=1000 > results/scale_2_30_client_2_13_batchpir.log
```

### ChecklistPIR

|Server| = 2^28:

```bash
./test --PIR_checklist --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=268435456 --client_size=1024 --L=3 --k=20 --w=0.05 > results/scale_2_28_client_2_10_checklistpir.log
./test --PIR_checklist --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=268435456 --client_size=8192 --L=3 --k=20 --w=0.05 > results/scale_2_28_client_2_13_checklistpir.log
```

|Server| = 2^29:

```bash
./test --PIR_checklist --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=536870912 --client_size=1024 --L=3 --k=20 --w=0.05 > results/scale_2_29_client_2_10_checklistpir.log
./test --PIR_checklist --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=536870912 --client_size=8192 --L=3 --k=20 --w=0.05 > results/scale_2_29_client_2_13_checklistpir.log
```

|Server| = 2^30:

```bash
./test --PIR_checklist --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=1073741824 --client_size=1024 --L=3 --k=20 --w=0.05 > results/scale_2_30_client_2_10_checklistpir.log
./test --PIR_checklist --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --db=default_plus_augmented_generated_client --server_size=1073741824 --client_size=8192 --L=3 --k=20 --w=0.05 > results/scale_2_30_client_2_13_checklistpir.log
```

## Machine learning Experiment
```
./run_cuckoo_client_sweep.sh db=MNIST 
./run_cuckoo_client_sweep.sh db=FashionMNIST
```

## ACM-DBLP Experiment

Build the experiment binary first:


### L2 Distance:
```bash
./test \
  --db=acm_dblp \
  --filter=Cuckoo \
  --PIR_BatchPIR \
  --OPRF \
  --oprf_mechanism=GCAES \
  --server_size=2616 \
  --client_size=128 \
  --num_runs=5
```


### Edit Distance:
```bash
./test \
  --db=acm_dblp \
  --edit_distance \
  --PIR_BatchPIR \
  --OPRF \
  --oprf_mechanism=GCAES \
  --server_size=2616 \
  --client_size=128 \
  --num_runs=5
```

The defaults are: 12 number of independent OMH tables, k=8 for UTF-8 byte k-mer length, and ell=5 for OMH selection parameter ell. 


## SpaceV use case

`--db=space100m` to use the 100M SpaceV dataset
[SpaceV dataset](https://github.com/ashvardanian/SpaceV), respectively.

Download the 100M distribution into the default location:

```bash
mkdir -p datasets/spacev-100m
wget -nc https://huggingface.co/datasets/unum-cloud/ann-spacev-100m/resolve/main/base.100M.i8bin -P datasets/spacev-100m/
wget -nc https://huggingface.co/datasets/unum-cloud/ann-spacev-100m/resolve/main/ids.100M.i32bin -P datasets/spacev-100m/
wget -nc https://huggingface.co/datasets/unum-cloud/ann-spacev-100m/resolve/main/query.30K.i8bin -P datasets/spacev-100m/
wget -nc https://huggingface.co/datasets/unum-cloud/ann-spacev-100m/resolve/main/groundtruth.30K.i32bin -P datasets/spacev-100m/
wget -nc https://huggingface.co/datasets/unum-cloud/ann-spacev-100m/resolve/main/groundtruth.30K.f32bin -P datasets/spacev-100m/
```



Then run using `--db=space100m`. `--space_close_radius` defines r_i and `--space_far_min_radius` defines r_o.
For example to run the results of Section 5.4. use:

```bash
./test --db=space100m --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --num_runs=5 --server_size=1000000 --client_size=1024 --space_close_radius=30 --space_far_min_radius=80 --L=40 --k=28 --w=205 --pir_batchpir_batch_size=1000
```



## Test experiment

```bash
./test \
  --PIR_BatchPIR \
  --OPRF \
  --oprf_mechanism=GCAES \
  --oprf_addr=127.0.0.1:50051 \
  --num_runs=1 \
  --db=gowalla \
  --L=5 \
  --k=5 \
  --w=0.5 \
  --server_size=1000 \
  --client_size=1000 \
  --filter_size=3152 \
  --pir_batchpir_batch_size=200
```

## Repository Map

- `test_fuzzypsi.cpp`: main CLI and top-level experiment dispatch.
- `test_fuzzypsi_parts/`: focused fragments of the FuzzyPSI protocol, included by `test_fuzzypsi.cpp`.
- `run_cuckoo_server_sweep.sh`: script to run multiple tests for multiple |S|, |C|.
- `run_cuckoo_client_sweep.sh`: script to sweep |C| for a fixed calibration.
- `datasets/prepare_db_ML.py`, `datasets/prepare_db_location.py`: dataset preparation.
- `pir/`: the `RowReader` interface both PIR schemes implement, plus the membership queries and cost reporting shared between them.
- `pir/batchpir/`: BatchPIR readers and the `batchpir_row_reader.*` adapter between experiment rows and BatchPIR.
- `pir/checklist/`: ChecklistPIR readers and the two-server Punc/PSetGGM port.
- `cuckoo_filter.h`, `lsh*.h`: core filter and LSH code.
- `oprf/droidcrypto/`, `pir/batchpir/vectorized_batchpir/`: third-party dependencies.


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



## Build Notes

Default build:

```bash
make test
```

The makefile assumes local dependency trees in `oprf/droidcrypto/` and
`pir/batchpir/vectorized_batchpir/`. On macOS it tries Homebrew OpenSSL and
SEAL paths; on Linux it tries common system paths.


## Third-Party Libraries

This project uses code from the following third-party libraries:
  - a C++ re-implementation of (**ChecklistPIR**)[https://github.com/dimakogan/checklist], reusing parts of the original code base  
  - the OPRF implementation from (**mobile_psi_cpp**)[https://github.com/contact-discovery/mobile_psi_cpp]
  - (**BatchPIR**)[https://github.com/mhmughees/vectorized_batchpir]
  - (**Microsoft SEAL**)[https://github.com/microsoft/SEAL]


### Disclaimer 
This repository is research code. Not for usage in production. 
