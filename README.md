# FuzzyPSI

This repository contains the code for our paper _Fuzzy Private Set Intersection with Large Databases via Locality-Sensitive Hashing_


# Installation

## Build the main binary and the SEAL library:
```
git clone https://github.com/andathan/FuzzyPSI.git
cd FuzzyPSI
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
## 1%/3%/5% FP at 1% FN (Figure 6, 7, 8 and Table 1)

```
./run_cuckoo_server_sweep.sh -target-fp 0.01
./run_cuckoo_server_sweep.sh -target-fp 0.03
./run_cuckoo_server_sweep.sh -target-fp 0.05
```

Besides stdout, results are also written in results/[exp_name]/summary_.csv

Exp_name follows the format [db] [target_fp] [datetime]


## Table 1 (comparison with other works)

The following script conveniently runs all instances apart from 1B (see below) for Table 1:

```
./table1
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

## Machine learning Experiment (Figure 9)
```
./run_cuckoo_client_sweep.sh db=MNIST 
./run_cuckoo_client_sweep.sh db=FashionMNIST
```

## ACM-DBLP Experiment


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



## Third-Party Libraries
This project uses code from the following third-party libraries:
- the OPRF implementation from [**mobile_psi_cpp**](https://github.com/contact-discovery/mobile_psi_cpp)
- [**BatchPIR**](https://github.com/mhmughees/vectorized_batchpir)
- [**ChecklistPIR**](https://github.com/dimakogan/checklist)
- [**Microsoft SEAL**](https://github.com/microsoft/SEAL)

### Disclaimer 
This repository is research code. Not for usage in production. 
