
bool CLOSE_NOISE = false;
int CLOSE_NOISE_START = 0;
int CLOSE_NOISE_END   = 0;
int FAR_NOISE_START   = 0;
int FAR_NOISE_END     = 0;

std::string MODE = "location";

// Canonical Cuckoo sizes itself while building; this is sweep/CSV metadata only.
size_t filter_size;
int N, CLIENT_SIZE_SEARCH, dim;
int L, k;
double w;
int DATASET_SEED = 1;
std::vector<int> CLIENT_SIZES_LIST;
std::vector<size_t> FILTER_SIZE_SWEEP;
std::vector<int> L_vals, k_vals;
std::vector<double> w_vals;
static const std::string DATASET_DIR = "datasets";
string loc_metadata_path = DATASET_DIR + "/loc_metadata.txt";

static bool read_server_n_from_metadata(const string& path, int& out);

static uint32_t dataset_seed_for_run(int run) {
    return static_cast<uint32_t>(DATASET_SEED) + static_cast<uint32_t>(run);
}

static size_t PIR_BATCHPIR_BATCH_SIZE = 200;
static bool CUCKOO_CANONICAL_NO_PRF_CACHE = false;
static bool OPRF_SERVER_INTERACTIVE = false;

extern bool USE_PIR_CHECKLIST;
extern bool USE_PIR_BATCHPIR;

static size_t parallel_worker_count(size_t task_count) {
    if (task_count == 0) {
        return 0;
    }
    return 1;
}

static void print_parallel_launch(const char* label, size_t task_count, size_t workers) {
    if (!DEBUG_PHASES || label == nullptr) {
        return;
    }
    const std::string label_text(label);
    if (label_text.find("PIR") != std::string::npos ||
        label_text.find("OPRF") != std::string::npos) {
        return;
    }
    static std::mutex print_mutex;
    static std::unordered_set<std::string> printed_labels;
    std::lock_guard<std::mutex> lock(print_mutex);
    if (!printed_labels.insert(label).second) {
        return;
    }
    cout << "[serial] " << label
         << " task_count=" << task_count
         << " workers=" << workers
         << " hardware_concurrency=" << std::thread::hardware_concurrency()
         << "\n";
}

template <typename Fn>
static void parallel_for_indices(size_t task_count, Fn&& fn, const char* label = nullptr) {
    const size_t workers = parallel_worker_count(task_count);
    print_parallel_launch(label, task_count, workers);
    if (workers <= 1) {
        for (size_t i = 0; i < task_count; ++i) {
            fn(i);
        }
        return;
    }

    std::atomic<size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex exception_mutex;
    std::exception_ptr first_exception = nullptr;
    std::vector<std::thread> threads;
    threads.reserve(workers);

    for (size_t worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&]() {
            try {
                while (!stop.load(std::memory_order_relaxed)) {
                    const size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= task_count) {
                        break;
                    }
                    fn(i);
                }
            } catch (...) {
                {
                    std::lock_guard<std::mutex> lock(exception_mutex);
                    if (!first_exception) {
                        first_exception = std::current_exception();
                    }
                }
                stop.store(true, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

void init_config() {
    if (MODE == "space") {
        // SpaceV vectors are 100-dimensional signed int8 values.  These are
        // deliberately conservative defaults for a local smoke run; calibrate
        // L/k/w and filter size for the actual Server size before benchmarking.
        filter_size = 2'000'000;
        N = 100'000;
        CLIENT_SIZE_SEARCH = 100;
        dim = 100;

        CLIENT_SIZES_LIST = {100};
        FILTER_SIZE_SWEEP = {
            1'000'000ULL,
            2'000'000ULL,
            4'000'000ULL
        };
        L_vals = {5, 10, 20, 30, 40, 50, 75, 100, 200, 300};
        k_vals = {4, 6, 8, 10, 12, 14, 16, 20, 24, 28};
        w_vals = {40, 60, 80, 100, 120, 140, 160, 180, 205, 240};

    } else if (MODE == "location") {
        filter_size = 1000000;
        N = 4500000;
        CLIENT_SIZE_SEARCH = 1000;
        dim = 2;

        CLIENT_SIZES_LIST = {1000};
        FILTER_SIZE_SWEEP = {
            80000000
            };
            /*1'000ULL,
            5'000ULL,
            10'000ULL,
            20'000ULL,
            25'000ULL,
            50'000ULL,
            100'000ULL,
            250'000ULL
            500'000ULL,
            1'000'000ULL,
            10'000'000ULL,
            50'000'000ULL,
            100'000'000ULL
            500'000'000ULL,
            1'000'000'000ULL
            5'000'000'000ULL,
            10'000'000'000ULL,
            50'000'000'000ULL,
            100'000'000'000ULL
            */
        
        L_vals = {1, 3, 5, 7};
        k_vals = {1, 3, 5,10,20};
        w_vals = {0.05,0.01,0.05,0.1,0.5,1,2,5};

    } else if (MODE == "ML") {
        filter_size = 1'000'000;
        N = 5000;
        CLIENT_SIZE_SEARCH = 200;
        dim = 784;

        CLIENT_SIZES_LIST = {1000};
        FILTER_SIZE_SWEEP = {
            25'000ULL,
            50'000ULL,
            100'000ULL,
            250'000ULL,
            500'000ULL,
            1'000'000ULL,
            10'000'000ULL,
            50'000'000ULL,
            100'000'000ULL,
            500'000'000ULL,
            1'000'000'000ULL,
            5'000'000'000ULL
        };
        L_vals = {1, 3, 5, 7, 10, 15, 20, 25, 30};
        k_vals = {1, 5, 10, 20, 25, 30, 35, 40, 45};
        w_vals = {600, 800, 1000, 1200, 1400, 1600, 2000, 2500, 3000, 3500, 4000, 4500};

    } else if (MODE == "cyber") {
        filter_size = 1'000'000;
        N = 540000;
        CLIENT_SIZE_SEARCH = 1000;
        dim = 12;

        CLIENT_SIZES_LIST = {1000};
        FILTER_SIZE_SWEEP = {
            25'000ULL,
            50'000ULL,
            100'000ULL,
            250'000ULL,
            500'000ULL,
            1'000'000ULL,
            10'000'000ULL
        };
        L_vals = {1, 3, 5, 7};
        k_vals = {1, 3, 5, 10};
        w_vals = {
            1e-8,
            3e-8,
            1e-7,
            3e-7,
            1e-6,
            3e-6,
            1e-5,
            3e-5,
            1e-4,
            3e-4
        };

    } else if (MODE == "recordlinkage") {
        filter_size = 41'760;
        N = 2616;
        CLIENT_SIZE_SEARCH = 100;
        dim = 28;

        CLIENT_SIZES_LIST = {100};
        FILTER_SIZE_SWEEP = {
            8'192ULL,
            16'384ULL,
            32'768ULL,
            65'536ULL
        };
        L = 25;
        k = 10;
        w = 4;
        PIR_BATCHPIR_BATCH_SIZE = 100;
        L_vals = {1, 3, 5, 7, 10, 15, 20, 25, 30};
        k_vals = {1, 2, 3, 5, 7, 10, 15, 20};
        w_vals = {0.25, 0.5, 0.75, 1, 1.25, 1.5, 2, 3, 4, 5, 7, 10};

    } else if (MODE == "febrl") {
        filter_size = 524'288;
        N = 5000;
        CLIENT_SIZE_SEARCH = 500;
        dim = 2048;

        CLIENT_SIZES_LIST = {500};
        FILTER_SIZE_SWEEP = {
            262'144ULL,
            524'288ULL,
            1'048'576ULL
        };
        L_vals = {10, 15, 20, 30, 40};
        k_vals = {3, 4, 5, 6};
        w_vals = {0.1, 0.2, 0.3, 0.4, 0.5};

    } else {
        throw std::runtime_error("Unknown MODE");
    }

    if (MODE == "location") {
        int metadata_N = -1;
        if (read_server_n_from_metadata(loc_metadata_path, metadata_N)) {
            N = metadata_N;
            PHASE_LOG << "[CONFIG] Loaded Server N from "
                      << loc_metadata_path << ": " << N << "\n";
        } else {
            PHASE_LOG << "[CONFIG] No " << loc_metadata_path
                      << " found; using default N=" << N << "\n";
        }
    } else if (MODE == "cyber" || MODE == "recordlinkage" || MODE == "febrl") {
        int metadata_N = -1;
        if (read_server_n_from_metadata(loc_metadata_path, metadata_N)) {
            N = metadata_N;
            PHASE_LOG << "[CONFIG] Loaded Server N from "
                      << loc_metadata_path << ": " << N << "\n";
        } else {
            PHASE_LOG << "[CONFIG] No " << loc_metadata_path
                      << " found; using default N=" << N << "\n";
        }
    }
}

int NUM_RUNS_PSI = 5; //how many times to run the fuzzyPSI experiment and take the avg
int NUM_RUNS_SEARCH = 5; //how many times to try a specific combination of params when brute force and take the avg
bool REUSE_DATASET_BETWEEN_GRID_AND_TEST;
bool USE_PIR_CHECKLIST = false;
bool USE_PIR_BATCHPIR = false;
bool USE_OPRF = false;
bool USE_BATCH = false;
string server_path = DATASET_DIR + "/gowalla_server.json"; //path to server file
string client_path = DATASET_DIR + "/gowalla_client.json"; //path to client file
string db_preset = "gowalla";

bool DEBUG_PHASES = false;
bool CALIBRATION_HOLDOUT = false;
bool CALIBRATION_HOLDOUT_USE_HELD_OUT = false;
bool GENERATE_CLIENT_FROM_SERVER = false;
bool RAW_CLIENT_SPLIT = false;
bool APPEND_RANDOM_SERVER = false;
bool APPEND_AUGMENTED_SERVER = false;
bool APPEND_DISTRIBUTION_SERVER = false;
bool PORTABLE_DATASET_SAMPLING = false;
bool PORTABLE_LSH = false;
bool EDIT_DISTANCE = false;
bool ACM_DBLP_GOLD_SAMPLING = false;
bool SPACEV_DATASET = false;
double SYNTHETIC_CLIENT_CLOSE_RADIUS_DEG = 0.005;
double SYNTHETIC_CLIENT_FAR_MIN_RADIUS_DEG = 1.0;
double AUGMENTED_SERVER_JITTER_DEG = 0.002;
double DISTRIBUTION_SERVER_NOISE_SCALE = 1.0;
std::string APPEND_RANDOM_SERVER_PATH = DATASET_DIR + "/server_random.json";
std::string RAW_CLIENT_PATH = DATASET_DIR + "/client.json";
std::string SPACEV_IDS_PATH = DATASET_DIR + "/spacev-100m/ids.100M.i32bin";
std::string SPACEV_QUERY_PATH = DATASET_DIR + "/spacev-100m/query.30K.i8bin";
std::string SPACEV_GROUNDTRUTH_PATH = DATASET_DIR + "/spacev-100m/groundtruth.30K.i32bin";
std::string SPACEV_DISTANCES_PATH = DATASET_DIR + "/spacev-100m/groundtruth.30K.f32bin";
double SPACEV_CLOSE_RADIUS_L2 = 40.0;
double SPACEV_FAR_MIN_RADIUS_L2 = 80.0;

static bool read_server_n_from_metadata(const string& path, int& out) {
    ifstream in(path);
    if (!in) return false;

    int value = -1;
    in >> value;
    if (!in || value <= 0) {
        throw runtime_error("Invalid Server N metadata in " + path);
    }

    out = value;
    return true;
}

static string dataset_path(const string& filename) {
    return DATASET_DIR + "/" + filename;
}

static bool apply_db_preset(const string& db) {
    SPACEV_DATASET = false;

    if (db == "space100m") {
        MODE = "space";
        db_preset = "space100m";
        server_path = dataset_path("spacev-100m/base.100M.i8bin");
        client_path = dataset_path("spacev-100m/query.30K.i8bin");
        SPACEV_IDS_PATH = dataset_path("spacev-100m/ids.100M.i32bin");
        SPACEV_QUERY_PATH = client_path;
        SPACEV_GROUNDTRUTH_PATH =
            dataset_path("spacev-100m/groundtruth.30K.i32bin");
        SPACEV_DISTANCES_PATH =
            dataset_path("spacev-100m/groundtruth.30K.f32bin");
        CLIENT_CLOSE_PATH = "(SpaceV sampled neighbor within close L2 radius)";
        CLIENT_FAR_PATH = "(SpaceV sampled nearest neighbor beyond far L2 radius)";
        loc_metadata_path = dataset_path("spacev-100m/metadata.txt");
        GENERATE_CLIENT_FROM_SERVER = false;
        RAW_CLIENT_SPLIT = false;
        APPEND_RANDOM_SERVER = false;
        APPEND_AUGMENTED_SERVER = false;
        APPEND_DISTRIBUTION_SERVER = false;
        SPACEV_DATASET = true;
        return true;
    }

    if (db == "space1b") {
        MODE = "space";
        db_preset = "space1b";
        server_path = dataset_path("space1v/base.1B.i8bin");
        client_path = dataset_path("space1v/query.30K.i8bin");
        // The full base uses original row IDs, so it needs no subset-ID mapping.
        SPACEV_IDS_PATH.clear();
        SPACEV_QUERY_PATH = client_path;
        SPACEV_GROUNDTRUTH_PATH =
            dataset_path("space1v/groundtruth.30K.i32bin");
        SPACEV_DISTANCES_PATH =
            dataset_path("space1v/groundtruth.30K.f32bin");
        CLIENT_CLOSE_PATH = "(SpaceV sampled neighbor within close L2 radius)";
        CLIENT_FAR_PATH = "(SpaceV sampled nearest neighbor beyond far L2 radius)";
        loc_metadata_path = dataset_path("space1v/metadata.txt");
        GENERATE_CLIENT_FROM_SERVER = false;
        RAW_CLIENT_SPLIT = false;
        APPEND_RANDOM_SERVER = false;
        APPEND_AUGMENTED_SERVER = false;
        APPEND_DISTRIBUTION_SERVER = false;
        SPACEV_DATASET = true;
        return true;
    }

    if (db.empty() || db == "gowalla" || db == "default") {
        MODE = "location";
        db_preset = "gowalla";
        server_path = dataset_path("gowalla_server.json");
        client_path = dataset_path("gowalla_client/gowalla_client.json");
        CLIENT_CLOSE_PATH = dataset_path("gowalla_client/gowalla_client_close.json");
        CLIENT_FAR_PATH = dataset_path("gowalla_client/gowalla_client_far.json");
        loc_metadata_path = dataset_path("gowalla_metadata.txt");
        GENERATE_CLIENT_FROM_SERVER = false;
        APPEND_RANDOM_SERVER = false;
        APPEND_AUGMENTED_SERVER = false;
        APPEND_DISTRIBUTION_SERVER = false;
        return true;
    }

    if (db == "gowalla_protocol" || db == "gowalla_protocol_split") {
        return apply_db_preset("gowalla");
    }

    if (db == "gowalla_calibration" || db == "gowalla_calibration_split") {
        MODE = "location";
        db_preset = "gowalla_calibration";
        server_path = dataset_path("calibrations/gowalla_calibration_server.json");
        client_path = dataset_path("calibrations/gowalla_calibration_client.json");
        CLIENT_CLOSE_PATH = dataset_path("calibrations/gowalla_calibration_client_close.json");
        CLIENT_FAR_PATH = dataset_path("calibrations/gowalla_calibration_client_far.json");
        loc_metadata_path = dataset_path("calibrations/gowalla_calibration_metadata.txt");
        GENERATE_CLIENT_FROM_SERVER = false;
        APPEND_RANDOM_SERVER = false;
        APPEND_AUGMENTED_SERVER = false;
        APPEND_DISTRIBUTION_SERVER = false;
        return true;
    }

    if (db == "acm_dblp" || db == "dblp_acm" || db == "acm_dplp") {
        MODE = "recordlinkage";
        db_preset = "acm_dblp";
        server_path = dataset_path("calibrations/acm_dblp_protocol_server.json");
        client_path = dataset_path("acm_dblp_protocol/client.json");
        CLIENT_CLOSE_PATH = dataset_path("calibrations/acm_dblp_protocol_client_close.json");
        CLIENT_FAR_PATH = dataset_path("calibrations/acm_dblp_protocol_client_far.json");
        loc_metadata_path = dataset_path("calibrations/acm_dblp_protocol_metadata.txt");
        return true;
    }

    if (db == "febrl" || db == "FEBRL" || db == "febrl4" || db == "FEBRL4") {
        MODE = "febrl";
        db_preset = "febrl";
        server_path = dataset_path("febrl_server.json");
        client_path = dataset_path("febrl_client.json");
        CLIENT_CLOSE_PATH = dataset_path("febrl_client_close.json");
        CLIENT_FAR_PATH = dataset_path("febrl_client_far.json");
        loc_metadata_path = dataset_path("febrl_metadata.txt");
        return true;
    }

    if (db == "random") {
        MODE = "location";
        db_preset = "random";
        server_path = dataset_path("server_random.json");
        client_path = dataset_path("client_random.json");
        CLIENT_CLOSE_PATH = dataset_path("client_close_random.json");
        CLIENT_FAR_PATH = dataset_path("client_far_random.json");
        loc_metadata_path = dataset_path("loc_metadata_random.txt");
        return true;
    }

    if (db == "random_generated_client") {
        MODE = "location";
        db_preset = "random_generated_client";
        server_path = dataset_path("server_random.json");
        client_path = dataset_path("client_random_generated.json");
        CLIENT_CLOSE_PATH = "(generated-from-sampled-server)";
        CLIENT_FAR_PATH = "(generated-far-region)";
        loc_metadata_path = dataset_path("loc_metadata_random.txt");
        GENERATE_CLIENT_FROM_SERVER = true;
        return true;
    }

    if (db == "gowalla_plus_random_generated_client" || db == "default_plus_random_generated_client") {
        MODE = "location";
        db_preset = "gowalla_plus_random_generated_client";
        server_path = dataset_path("gowalla_server.json");
        client_path = dataset_path("client_default_plus_random_generated.json");
        CLIENT_CLOSE_PATH = "(generated-from-mixed-server)";
        CLIENT_FAR_PATH = "(generated-far-region)";
        loc_metadata_path = dataset_path("loc_metadata_random.txt");
        GENERATE_CLIENT_FROM_SERVER = true;
        APPEND_RANDOM_SERVER = true;
        APPEND_RANDOM_SERVER_PATH = dataset_path("server_random.json");
        return true;
    }

    if (db == "gowalla_plus_augmented_generated_client" || db == "default_plus_augmented_generated_client") {
        MODE = "location";
        db_preset = "gowalla_plus_augmented_generated_client";
        server_path = dataset_path("gowalla_server.json");
        client_path = dataset_path("client_default_plus_augmented_generated.json");
        CLIENT_CLOSE_PATH = "(generated-from-augmented-server)";
        CLIENT_FAR_PATH = "(generated-far-region)";
        loc_metadata_path = dataset_path("loc_metadata_random.txt");
        GENERATE_CLIENT_FROM_SERVER = true;
        APPEND_AUGMENTED_SERVER = true;
        return true;
    }

    if (db == "gowalla_plus_distribution_generated_client" || db == "default_plus_distribution_generated_client") {
        MODE = "location";
        db_preset = "gowalla_plus_distribution_generated_client";
        server_path = dataset_path("gowalla_server.json");
        client_path = dataset_path("client_default_plus_distribution_generated.json");
        CLIENT_CLOSE_PATH = "(generated-from-distribution-server)";
        CLIENT_FAR_PATH = "(generated-far-region)";
        loc_metadata_path = dataset_path("loc_metadata_random.txt");
        GENERATE_CLIENT_FROM_SERVER = true;
        APPEND_DISTRIBUTION_SERVER = true;
        return true;
    }

    if (db == "random_small") {
        MODE = "location";
        db_preset = "random_small";
        server_path = dataset_path("server_random_small.json");
        client_path = dataset_path("client_random_small.json");
        CLIENT_CLOSE_PATH = dataset_path("client_close_random_small.json");
        CLIENT_FAR_PATH = dataset_path("client_far_random_small.json");
        loc_metadata_path = dataset_path("loc_metadata_random_small.txt");
        return true;
    }

    if (db == "MNIST" || db == "mnist") {
        MODE = "ML";
        db_preset = "MNIST";
        server_path = dataset_path("mnist_server.json");
        client_path = dataset_path("mnist_client.json");
        CLIENT_CLOSE_PATH = dataset_path("mnist_client_close.json");
        CLIENT_FAR_PATH = dataset_path("mnist_client_far.json");
        loc_metadata_path = dataset_path("mnist_metadata.txt");
        return true;
    }

    if (db == "FashionMNIST" || db == "fashionmnist" ||
        db == "FashionMNST" || db == "fashionmnst") {
        MODE = "ML";
        db_preset = "FashionMNIST";
        server_path = dataset_path("fashionmnist_server.json");
        client_path = dataset_path("fashionmnist_client.json");
        CLIENT_CLOSE_PATH = dataset_path("fashionmnist_client_close.json");
        CLIENT_FAR_PATH = dataset_path("fashionmnist_client_far.json");
        loc_metadata_path = dataset_path("fashionmnist_metadata.txt");
        return true;
    }

    if (db == "nf_bot_iot" || db == "NF-BoT-IoT" || db == "nf-bot-iot" ||
        db == "bot_iot" || db == "bot-iot") {
        MODE = "cyber";
        db_preset = "nf_bot_iot";
        server_path = dataset_path("nf_bot_iot_server.json");
        client_path = dataset_path("nf_bot_iot_client.json");
        RAW_CLIENT_PATH = client_path;
        CLIENT_CLOSE_PATH = dataset_path("nf_bot_iot_client_close.json");
        CLIENT_FAR_PATH = dataset_path("nf_bot_iot_client_far.json");
        loc_metadata_path = dataset_path("nf_bot_iot_metadata.txt");
        RAW_CLIENT_SPLIT = true;
        return true;
    }

    return false;
}

using Clock = chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return chrono::duration<double, milli>(end - start).count();
}

static GcaesOprfStats gcaes_oprf_stats_delta(
    const GcaesOprfStats& before,
    const GcaesOprfStats& after
) {
    auto delta_u64 = [](std::uint64_t a, std::uint64_t b) -> std::uint64_t {
        return a >= b ? a - b : 0;
    };
    auto delta_size = [](std::size_t a, std::size_t b) -> std::size_t {
        return a >= b ? a - b : 0;
    };

    GcaesOprfStats delta;
    delta.bytes_sent = delta_u64(after.bytes_sent, before.bytes_sent);
    delta.bytes_recv = delta_u64(after.bytes_recv, before.bytes_recv);
    delta.runtime_ms = std::max(0.0, after.runtime_ms - before.runtime_ms);
    delta.calls = delta_size(after.calls, before.calls);
    delta.blocks = delta_size(after.blocks, before.blocks);
    return delta;
}

static constexpr std::uint64_t WIRE_U16_BYTES = 2;
static constexpr std::uint64_t WIRE_U32_BYTES = 4;
static constexpr std::uint64_t WIRE_VECTOR_LEN_BYTES = 4;

static std::uint64_t wire_vector_bytes(size_t byte_count) {
    return WIRE_VECTOR_LEN_BYTES + static_cast<std::uint64_t>(byte_count);
}
