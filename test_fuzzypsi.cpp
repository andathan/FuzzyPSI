/*
Runs the FuzzyPSI protocol 
Two modes:
- If --CALIBRATE_LKW is set, calibrate Cuckoo L/k/w over the full Server dataset
  and exit.
- If --L,--k and --w are set, run the experiment with the given parameters.
*/

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <fstream>
#include <sstream>
#include <memory>
#include <functional>
#include <iterator>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <thread>
#include <mutex>
#include <atomic>
#include <exception>
#include <span>
#include "lsh/lsh_e2lsh.h"
#include "cuckoo_filter.h"
#include "pir/pir_row_reader.h"
#include "pir/batchpir/batchpir_row_reader.h"
#include "Utils/AES.h"
#include "Utils/utils.h"
#include "oprf/droidcrypto_adapter.h"
#include <string>
#include <vector>
#include <stdexcept>
#define PIR_PUNC_NO_DEMO_MAIN
#include "pir/checklist/pir_punc_psetggm_port.cpp"

using namespace std;

// Keep this experiment as one translation unit, but split the large implementation
// into focused fragments so templates and shared globals stay simple.
#include "test_fuzzypsi_parts/globals_and_utils.inc.cpp"
#include "pir/pir_scheme.inc.cpp"
#include "pir/checklist/checklist_readers.inc.cpp"
#include "pir/batchpir/batchpir_readers.inc.cpp"
#include "oprf/oprf_eval.inc.cpp"
#include "test_fuzzypsi_parts/cuckoo_prf_keys.inc.cpp"
#include "pir/pir_membership.inc.cpp"
#include "test_fuzzypsi_parts/datasets.inc.cpp"
#include "test_fuzzypsi_parts/grid_search.inc.cpp"
#include "test_fuzzypsi_parts/experiment_runner.inc.cpp"
#include "test_fuzzypsi_parts/cli_args.inc.cpp"

static void print_usage(const char* program_name) {
    cout
        << "Usage:\n"
        << "  " << program_name << " --L=5 --k=5 --w=0.5 --server_size=1000 --client_size=1000\n"
        << "  " << program_name << " --PIR_BatchPIR --OPRF --oprf_mechanism=GCAES --oprf_addr=127.0.0.1:50051 --L=5 --k=5 --w=0.5 --server_size=4500000 --client_size=1000\n"
        << "\n"
        << "Functionality Flags:\n"
        << "  --L --k --w               E2LSH parameters for normal runs.\n"
        << "  --edit_distance            ACM-DBLP only: use OrderMinHash instead of E2LSH; --L is the\n"
        << "                             number of OMH tables, --k the normalized title|year UTF-8 k-mer\n"
        << "                             length, and --w integer ell (defaults: 12, 8, 5).\n"
        << "  --server_size=N --client_size=N     Server and client sample sizes for the run.\n"
        << "  --filter_size=N                 Sweep/CSV metadata only; the Cuckoo filter sizes itself.\n"
        << "  --cuckoo_canonical_no_prf_cache   Recompute the PRF on every resize instead of caching it.\n"
        << "  --num_runs=N                    Number of times to repeat the experiment and get the average\n"
        << "  --db=PRESET                     gowalla, gowalla_calibration, random, random_small, MNIST, FashionMNIST, febrl, nf_bot_iot, acm_dblp, space100m, space1b\n"
        << "  --space_ids_path=PATH           SpaceV subset row-to-original-ID matrix (.i32bin).\n"
        << "  --space_query_path=PATH         SpaceV query matrix (.i8bin).\n"
        << "  --space_groundtruth_path=PATH   SpaceV top-k original IDs (.i32bin).\n"
        << "  --space_distances_path=PATH     SpaceV squared-L2 ground-truth matrix (.f32bin).\n"
        << "  --space_close_radius=R          Close L2 radius (default: 40).\n"
        << "  --space_far_min_radius=R        Guaranteed-far L2 radius (default: 80).\n"
        << "  --use_stored_client_split       Use client_close/client_far JSON instead of generated clients.\n"
        << "\n"
        << "Privacy Flags:\n"
        << "  --PIR_BatchPIR                  Uses BatchPIR.\n"
        << "  --PIR_checklist                 Uses the Checklist two-server Punc/PSetGGM PIR\n"
        << "  --OPRF                          Evaluate the client's PRF obliviously via the OPRF server.\n"
        << "                                  Without it the PRF is evaluated locally and the run is not\n"
        << "                                  private.\n"
        << "  --oprf_mechanism=GCAES|ECNR \n"
        << "  --oprf_addr=HOST:PORT           Address for server of GCAES OPRF. Be sure to first spawn a server (see README)\n"
        << "\n"
        << "Experiment Flags:\n"
        << "  --CALIBRATE_LKW                 Calibrate Cuckoo L/k/w over all Server entries and exit.\n"
        << "  --calibration_holdout           Reserve 20% of Server for calibration; normal runs use the other 80%.\n"
        << "  --SWEEP_FILTER_SIZE             Repeat the run for each --filter_size in the preset list.\n"
        << "\n"
        << "Useful wrappers:\n"
        << "  ./run_cuckoo_server_sweep.sh --help\n                   Runs the experiment on many server and client sizes\n" 
        << "\n";
}
int main(int argc, char* argv[]) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        print_usage(argv[0]);
        return 0;
    }
    //Parse arguments
    ParsedCliArgs args;
    try {
        if (!parse_cli_args(argc, argv, args)) {
            return -1;
        }
    } catch (const std::exception& ex) {
        cerr << "Invalid command line: " << ex.what() << "\n";
        return -1;
    }

    if (args.calibrate_lkw) {
        try {
            vector<LkwCandidate> calibration_candidates =
                parse_lkw_candidates(args.calibration_lkw_arg);
            if (calibration_candidates.empty()) {
                calibration_candidates = default_lkw_candidates();
            }
            return run_cuckoo_lkw_calibration_once(
                server_path,
                client_path,
                dim,
                N,
                CLIENT_SIZE_SEARCH,
                filter_size,
                calibration_candidates,
                args.calibration_target_fp,
                args.calibration_target_fn,
                args.calibration_stop_on_first_target,
                args.calibration_holdout
            );
        } catch (const exception& ex) {
            cerr << "Calibration failed: " << ex.what() << "\n";
            return -1;
        }
    }

    if (!args.has_all_lsh_params) {
        cerr << "Normal runs require --L=, --k=, and --w=. Use --CALIBRATE_LKW to calibrate parameters.\n";
        return -1;
    }

    // Calibration returns above, so this only covers measurement runs.
    if (!USE_OPRF) {
        cerr << "[warning] --OPRF not set: the Client evaluates the PRF locally instead of"
             << " obliviously, so it needs the Server's key.\n"
             << "[warning] This run is a functional test only. It is not private, and its"
             << " Client runtime and communication cost are not protocol-representative.\n"
             << std::flush;
    }

    REUSE_DATASET_BETWEEN_GRID_AND_TEST = 0;
    if (DEBUG_PHASES) {
        cout << "*** USING GIVEN PARAMETERS ***" << endl;

        cout << "\n[best params] L=" << L << " k=" << k << " w=" << w << "\n";
        if (EDIT_DISTANCE) {
            cout << "[lsh config] family=OrderMinHash"
             << " tables=" << L << " kmer_length=" << k
             << " ell=" << static_cast<int>(w) << "\n";
        }
        cout << "[filter config] filter_size=auto"
         << " sweep_filter_size=" << (args.sweep_filter_size ? "true" : "false")
         << " cuckoo_build=canonical_resizing";
        if (CUCKOO_CANONICAL_NO_PRF_CACHE) {
            cout << " cuckoo_canonical_no_prf_cache=true";
        }
        cout << " use_oprf=" << (USE_OPRF ? "true" : "false")
         << " use_batch=" << (USE_BATCH ? "true" : "false");
        if (USE_OPRF) {
            cout << " oprf_mechanism=" << oprf_mechanism()
             << " oprf_addr=" << gcaes_oprf_server_host()
             << ":" << gcaes_oprf_server_port()
             << " server_prf="
             << (OPRF_SERVER_INTERACTIVE
                     ? (std::string("interactive_") + oprf_mechanism() + "_oprf")
                     : (std::string("local_") + oprf_mechanism() + "_server_prf"));
        }
        cout << "\n";
        cout << "[dataset config] Running with |Server|=" << N
         << " db=" << db_preset
         << " server=" << server_path
         << " raw_client_split=" << (RAW_CLIENT_SPLIT ? "true" : "false")
         << " raw_client=" << (RAW_CLIENT_SPLIT ? RAW_CLIENT_PATH : "(unused)")
         << " client_close=" << CLIENT_CLOSE_PATH
         << " client_far=" << CLIENT_FAR_PATH
         << " generate_client_from_server=" << (GENERATE_CLIENT_FROM_SERVER ? "true" : "false")
         << " append_random_server=" << (APPEND_RANDOM_SERVER ? "true" : "false")
         << " append_augmented_server=" << (APPEND_AUGMENTED_SERVER ? "true" : "false")
         << " append_distribution_server=" << (APPEND_DISTRIBUTION_SERVER ? "true" : "false")
         << " calibration_holdout=" << (CALIBRATION_HOLDOUT ? "true" : "false")
         << " holdout_side="
         << (CALIBRATION_HOLDOUT_USE_HELD_OUT ? "calibration" : "train")
         << " augmented_server_jitter=" << AUGMENTED_SERVER_JITTER_DEG
         << " distribution_server_noise_scale=" << DISTRIBUTION_SERVER_NOISE_SCALE
         << " append_random_server_path=" << APPEND_RANDOM_SERVER_PATH
         << " synthetic_client_close_radius=" << SYNTHETIC_CLIENT_CLOSE_RADIUS_DEG
         << " synthetic_client_far_min_radius=" << SYNTHETIC_CLIENT_FAR_MIN_RADIUS_DEG
         << " portable_sampling=" << (PORTABLE_DATASET_SAMPLING ? "true" : "false")
         << " portable_lsh=" << (PORTABLE_LSH ? "true" : "false")
             << " debug=" << (DEBUG_PHASES ? "1" : "0")
             << "\n";
    }

    REUSE_DATASET_BETWEEN_GRID_AND_TEST = 0; //in case we want to run the exact same sampling of datasets for parameter optimization and experiment run (less privacy but better utility)
    if (args.sweep_filter_size) {
        PHASE_LOG << "-----------------------------\n";
        for (size_t sweep_filter_size : FILTER_SIZE_SWEEP) {
            filter_size = sweep_filter_size;
            PHASE_LOG << "\n================ FILTER_SIZE=" << filter_size
                 << " ================\n";
            fuzzypsi(L, k, w, "FILTER SIZE SWEEP");
        }
        PHASE_LOG << "-----------------------------\n";
        return 0;
    }

    PHASE_LOG << "-----------------------------\n";
    PHASE_LOG << "Grid Search found best parameters. Now running FUZZY PSI\n";
    fuzzypsi(L, k, w, "DIFFERENT SAMPLE");
    PHASE_LOG << "-----------------------------\n";
    return 0;
}
