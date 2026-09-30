struct ParsedCliArgs {
    bool calibrate_lkw = false;
    bool sweep_filter_size = false;
    bool has_all_lsh_params = false;

    std::string calibration_lkw_arg;
    double calibration_target_fp = 0.035;
    double calibration_target_fn = 0.015;
    bool calibration_stop_on_first_target = true;
    bool calibration_holdout = false;
};

static bool removed_parallel_flag_present(int argc, char* argv[]) {
    return has_flag(argc, argv, "--PARALLEL") ||
           has_flag(argc, argv, "--USE_PARALLEL") ||
           has_flag(argc, argv, "--parallel") ||
           has_flag(argc, argv, "--use_parallel");
}

static bool removed_lowmc_oprf_flag_present(int argc, char* argv[]) {
    return has_flag(argc, argv, "--GCLOWMC_OPRF") ||
           has_flag(argc, argv, "--OPRF_GCLOWMC") ||
           has_flag(argc, argv, "--oprf_gclowmc") ||
           has_flag(argc, argv, "--LOWMC_OPRF") ||
           has_flag(argc, argv, "--OPRF_LOWMC") ||
           has_flag(argc, argv, "--oprf_lowmc");
}

static bool removed_grid_search_flag_present(int argc, char* argv[]) {
    return has_flag(argc, argv, "--GRID_SEARCH") ||
           has_flag(argc, argv, "--grid_search");
}

static bool parse_oprf_config(int argc, char* argv[]) {
    if (removed_lowmc_oprf_flag_present(argc, argv)) {
        cerr << "LOWMC/GCLOWMC OPRF has been removed; use GCAES or ECNR.\n";
        return false;
    }

    USE_OPRF = has_flag(argc, argv, "--OPRF") ||
               has_flag(argc, argv, "--USE_OPRF") ||
               has_flag(argc, argv, "--oprf") ||
               has_flag(argc, argv, "--use_oprf") ||
               has_flag(argc, argv, "--ECNR_OPRF") ||
               has_flag(argc, argv, "--OPRF_ECNR") ||
               has_flag(argc, argv, "--oprf_ecnr") ||
               has_flag(argc, argv, "--GCAES_OPRF") ||
               has_flag(argc, argv, "--OPRF_GCAES") ||
               has_flag(argc, argv, "--oprf_gcaes");

    std::string oprf_mechanism_arg;
    if (get_string_arg(argc, argv, "--oprf_mechanism=", oprf_mechanism_arg) ||
        get_string_arg(argc, argv, "--OPRF_mechanism=", oprf_mechanism_arg) ||
        get_string_arg(argc, argv, "--oprf_type=", oprf_mechanism_arg) ||
        get_string_arg(argc, argv, "--OPRF_type=", oprf_mechanism_arg) ||
        get_string_arg(argc, argv, "--oprf=", oprf_mechanism_arg) ||
        get_string_arg(argc, argv, "--OPRF=", oprf_mechanism_arg)) {
        try {
            USE_OPRF = true;
            oprf_set_mechanism(oprf_mechanism_arg.c_str());
        } catch (const std::exception& ex) {
            cerr << "Invalid OPRF mechanism: " << ex.what() << "\n";
            return false;
        }
    }

    if (has_flag(argc, argv, "--ECNR_OPRF") ||
        has_flag(argc, argv, "--OPRF_ECNR") ||
        has_flag(argc, argv, "--oprf_ecnr")) {
        oprf_set_mechanism("ECNR");
    }
    if (has_flag(argc, argv, "--GCAES_OPRF") ||
        has_flag(argc, argv, "--OPRF_GCAES") ||
        has_flag(argc, argv, "--oprf_gcaes")) {
        oprf_set_mechanism("GCAES");
    }

    const std::string active_oprf_mechanism = oprf_mechanism();
    if (active_oprf_mechanism != "GCAES" && active_oprf_mechanism != "ECNR") {
        cerr << "Invalid OPRF mechanism. Use GCAES or ECNR.\n";
        return false;
    }

    OPRF_SERVER_INTERACTIVE = has_flag(argc, argv, "--server_oprf_interactive") ||
                             has_flag(argc, argv, "--OPRF_server_interactive") ||
                             has_flag(argc, argv, "--oprf_server_interactive");
    if (has_flag(argc, argv, "--server_oprf_local") ||
        has_flag(argc, argv, "--OPRF_server_local") ||
        has_flag(argc, argv, "--oprf_server_local")) {
        OPRF_SERVER_INTERACTIVE = false;
    }

    std::string oprf_addr;
    if (get_string_arg(argc, argv, "--oprf_addr=", oprf_addr) ||
        get_string_arg(argc, argv, "--oprfaddr=", oprf_addr) ||
        get_string_arg(argc, argv, "--gcaes_oprf_addr=", oprf_addr) ||
        get_string_arg(argc, argv, "--ecnr_oprf_addr=", oprf_addr)) {
        const size_t colon = oprf_addr.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= oprf_addr.size()) {
            cerr << "--oprf_addr must be host:port\n";
            return false;
        }
        try {
            const std::string host = oprf_addr.substr(0, colon);
            const int port = std::stoi(oprf_addr.substr(colon + 1));
            gcaes_oprf_set_server(host.c_str(), port);
        } catch (const std::exception& ex) {
            cerr << "Invalid --oprf_addr: " << ex.what() << "\n";
            return false;
        }
    }

    return true;
}

static bool validate_parsed_args(const ParsedCliArgs& args) {
    if (EDIT_DISTANCE && db_preset != "acm_dblp") {
        cerr << "--edit_distance is only valid for the ACM-DBLP use case (--db=acm_dblp)\n";
        return false;
    }
    if (EDIT_DISTANCE && args.calibrate_lkw) {
        cerr << "--edit_distance is not supported with --CALIBRATE_LKW\n";
        return false;
    }
    if (EDIT_DISTANCE) {
        const double ell = std::round(w);
        if (!std::isfinite(w) || w != ell || ell < 2.0 || k <= 0 || k > dim ||
            ell > static_cast<double>(dim - k + 1)) {
            cerr << "For --edit_distance, --k is the OMH k-mer length and --w is an integer "
                 << "OMH ell >=2; require k<=dim and ell<=dim-k+1\n";
            return false;
        }
    }

    const int pir_mode_count =
        (USE_PIR_CHECKLIST ? 1 : 0) +
        (USE_PIR_BATCHPIR ? 1 : 0);
    if (pir_mode_count > 1) {
        cerr << "Choose only one PIR mode: --PIR_checklist or --PIR_BatchPIR\n";
        return false;
    }
    if (USE_OPRF && !(USE_PIR_CHECKLIST || USE_PIR_BATCHPIR)) {
        cerr << "OPRF mode is wired for private PIR query paths; use --OPRF together with a PIR mode\n";
        return false;
    }
    if (USE_BATCH && !(USE_PIR_CHECKLIST || USE_PIR_BATCHPIR)) {
        cerr << "Batched PIR mode requires --PIR_checklist or --PIR_BatchPIR\n";
        return false;
    }
    if (USE_PIR_BATCHPIR && !fuzzy_pets_batchpir::available()) {
        cerr << "PIR_BatchPIR is unavailable: "
             << fuzzy_pets_batchpir::unavailable_reason()
             << "\n";
        return false;
    }
    if (args.calibrate_lkw) {
        if (USE_PIR_CHECKLIST || USE_PIR_BATCHPIR || USE_OPRF || USE_BATCH) {
            cerr << "--CALIBRATE_LKW is direct Cuckoo calibration; do not combine it with PIR, OPRF, or batch flags\n";
            return false;
        }
    }

    return true;
}

static bool parse_cli_args(int argc, char* argv[], ParsedCliArgs& args) {
    if (removed_parallel_flag_present(argc, argv)) {
        cerr << "Parallel mode has been removed; run without parallel flags.\n";
        return false;
    }
    if (removed_lowmc_oprf_flag_present(argc, argv)) {
        cerr << "LOWMC/GCLOWMC OPRF has been removed; use GCAES or ECNR.\n";
        return false;
    }
    if (removed_grid_search_flag_present(argc, argv)) {
        cerr << "GRID_SEARCH has been removed; use --CALIBRATE_LKW for parameter calibration.\n";
        return false;
    }

    string requested_db = "gowalla";
    get_string_arg(argc, argv, "--db=", requested_db);
    get_string_arg(argc, argv, "db=", requested_db);
    if (!apply_db_preset(requested_db)) {
        cerr << "Invalid db preset. Use --db=gowalla, --db=random, --db=random_small, --db=MNIST, --db=FashionMNIST, --db=febrl, --db=nf_bot_iot, --db=acm_dblp, --db=space100m, or --db=space1b\n";
        return false;
    }
    ACM_DBLP_GOLD_SAMPLING = db_preset == "acm_dblp";
    EDIT_DISTANCE = has_flag(argc, argv, "--edit_distance");
    const bool explicit_server_path = get_string_arg(argc, argv, "--server_path=", server_path);
    get_string_arg(argc, argv, "--client_path=", client_path);
    const bool explicit_client_close_path = get_string_arg(argc, argv, "--client_close_path=", CLIENT_CLOSE_PATH);
    const bool explicit_client_far_path = get_string_arg(argc, argv, "--client_far_path=", CLIENT_FAR_PATH);
    get_string_arg(argc, argv, "--raw_client_path=", RAW_CLIENT_PATH);
    get_string_arg(argc, argv, "--space_ids_path=", SPACEV_IDS_PATH);
    get_string_arg(argc, argv, "--space_query_path=", SPACEV_QUERY_PATH);
    get_string_arg(argc, argv, "--space_groundtruth_path=", SPACEV_GROUNDTRUTH_PATH);
    get_string_arg(argc, argv, "--space_distances_path=", SPACEV_DISTANCES_PATH);
    get_double_arg(argc, argv, "--space_close_radius=", SPACEV_CLOSE_RADIUS_L2);
    get_double_arg(argc, argv, "--space_far_min_radius=", SPACEV_FAR_MIN_RADIUS_L2);
    if (SPACEV_DATASET &&
        (!(SPACEV_CLOSE_RADIUS_L2 > 0.0) ||
         !(SPACEV_FAR_MIN_RADIUS_L2 > SPACEV_CLOSE_RADIUS_L2))) {
        cerr << "SpaceV requires --space_close_radius > 0 and "
             << "--space_far_min_radius > --space_close_radius\n";
        return false;
    }
    const bool explicit_metadata_path = get_string_arg(argc, argv, "--metadata_path=", loc_metadata_path);
    if (db_preset == "acm_dblp") {
        const string protocol_suffix = EDIT_DISTANCE ? "text" : "gold";
        if (!explicit_server_path) {
            server_path = dataset_path(
                "acm_dblp_protocol/server_" + protocol_suffix + ".json"
            );
        }
        if (!explicit_client_close_path) {
            CLIENT_CLOSE_PATH = dataset_path(
                "acm_dblp_protocol/client_close_" + protocol_suffix + ".json"
            );
        }
        if (!explicit_client_far_path) {
            CLIENT_FAR_PATH = dataset_path(
                "acm_dblp_protocol/client_far_" + protocol_suffix + ".json"
            );
        }
        if (!explicit_metadata_path) {
            loc_metadata_path = dataset_path(
                "acm_dblp_protocol/metadata_" + protocol_suffix + ".txt"
            );
        }
    }
    if (explicit_server_path) {
        APPEND_RANDOM_SERVER = false;
        APPEND_AUGMENTED_SERVER = false;
        APPEND_DISTRIBUTION_SERVER = false;
    }
    if (explicit_client_close_path || explicit_client_far_path ||
        has_flag(argc, argv, "--use_stored_client_split") ||
        has_flag(argc, argv, "--stored_client_split") ||
        has_flag(argc, argv, "--no_generate_client_from_server")) {
        GENERATE_CLIENT_FROM_SERVER = false;
    }

    const bool requested_calibrate_lkw =
        has_flag(argc, argv, "--CALIBRATE_LKW") ||
        has_flag(argc, argv, "--calibrate_lkw");
    if (requested_calibrate_lkw &&
        !explicit_server_path &&
        !explicit_client_close_path &&
        !explicit_client_far_path &&
        !explicit_metadata_path) {
        string calibration_prefix;
        if (db_preset == "MNIST") {
            calibration_prefix = "mnist";
        } else if (db_preset == "FashionMNIST") {
            calibration_prefix = "fashionmnist";
        } else if (db_preset == "gowalla") {
            calibration_prefix = "gowalla";
        } else if (db_preset == "febrl") {
            calibration_prefix = "febrl";
        }

        if (!calibration_prefix.empty()) {
            const string calibration_dir = DATASET_DIR + "/calibrations/";
            const string calibration_server_path =
                calibration_dir + calibration_prefix + "_calibration_server.json";
            const string calibration_close_path =
                calibration_dir + calibration_prefix + "_calibration_client_close.json";
            const string calibration_far_path =
                calibration_dir + calibration_prefix + "_calibration_client_far.json";
            const string calibration_metadata_path =
                calibration_dir + calibration_prefix + "_calibration_metadata.txt";
            ifstream server_check(calibration_server_path);
            ifstream close_check(calibration_close_path);
            ifstream far_check(calibration_far_path);
            ifstream metadata_check(calibration_metadata_path);
            if (server_check && close_check && far_check && metadata_check) {
                server_path = calibration_server_path;
                CLIENT_CLOSE_PATH = calibration_close_path;
                CLIENT_FAR_PATH = calibration_far_path;
                loc_metadata_path = calibration_metadata_path;
            }
        }
    }

    int debug_mode = 0;
    get_int_arg(argc, argv, "--debug=", debug_mode);
    get_int_arg(argc, argv, "debug=", debug_mode);
    DEBUG_PHASES = debug_mode != 0 || has_flag(argc, argv, "--debug");

    init_config();
    if (EDIT_DISTANCE && db_preset == "acm_dblp") {
        // Maximum accepted UTF-8 record length; actual rows remain variable-length.
        dim = 4096;
    }

    args.calibrate_lkw = requested_calibrate_lkw;
    args.sweep_filter_size = has_flag(argc, argv, "--SWEEP_FILTER_SIZE") ||
                             has_flag(argc, argv, "--sweep_filter_size");
    if (has_flag(argc, argv, "--CA_only") ||
        has_flag(argc, argv, "--ca_only") ||
        has_flag(argc, argv, "--CARDINALITY_only") ||
        has_flag(argc, argv, "--cardinality_only")) {
        cerr << "--CA_only (cardinality-only / PiCardSum) has been removed along with "
             << "the OpenFHE dependency.\n";
        return false;
    }

    args.calibration_stop_on_first_target =
        !(has_flag(argc, argv, "--calibration_no_early_stop") ||
          has_flag(argc, argv, "--calibration_full_grid"));
    args.calibration_holdout =
        has_flag(argc, argv, "--calibration_holdout") ||
        has_flag(argc, argv, "--CALIBRATION_HOLDOUT") ||
        has_flag(argc, argv, "--calibration_heldout") ||
        has_flag(argc, argv, "--CALIBRATION_HELDOUT");
    get_string_arg(argc, argv, "--calibration_lkw=", args.calibration_lkw_arg);
    get_double_arg(argc, argv, "--calibration_target_fp=", args.calibration_target_fp);
    get_double_arg(argc, argv, "--calibration_target_fn=", args.calibration_target_fn);
    CALIBRATION_HOLDOUT = args.calibration_holdout;
    CALIBRATION_HOLDOUT_USE_HELD_OUT = args.calibrate_lkw && args.calibration_holdout;

    if (has_flag(argc, argv, "--PIR_double_dummy") ||
        has_flag(argc, argv, "--USE_PIR_double_dummy") ||
        has_flag(argc, argv, "--pir_double_dummy") ||
        has_flag(argc, argv, "--use_pir_double_dummy")) {
        cerr << "--PIR_double_dummy (plaintext test stub, not PIR) has been removed. "
             << "Use --PIR_checklist for ChecklistPIR.\n";
        return false;
    }
    if (has_flag(argc, argv, "--PIR_double") ||
        has_flag(argc, argv, "--USE_PIR_double") ||
        has_flag(argc, argv, "--pir_double") ||
        has_flag(argc, argv, "--use_pir_double") ||
        has_flag(argc, argv, "--PIR_double_new") ||
        has_flag(argc, argv, "--USE_PIR_double_new") ||
        has_flag(argc, argv, "--pir_double_new") ||
        has_flag(argc, argv, "--use_pir_double_new")) {
        cerr << "--PIR_double has been renamed to --PIR_checklist.\n";
        return false;
    }
    USE_PIR_CHECKLIST = has_flag(argc, argv, "--PIR_checklist") ||
                        has_flag(argc, argv, "--USE_PIR_checklist") ||
                        has_flag(argc, argv, "--pir_checklist") ||
                        has_flag(argc, argv, "--use_pir_checklist") ||
                        has_flag(argc, argv, "--PIR_Checklist");
    if (has_flag(argc, argv, "--PIR_single") ||
        has_flag(argc, argv, "--USE_PIR_single") ||
        has_flag(argc, argv, "--pir_single") ||
        has_flag(argc, argv, "--use_pir_single") ||
        has_flag(argc, argv, "--PIR_simple") ||
        has_flag(argc, argv, "--USE_PIR_simple") ||
        has_flag(argc, argv, "--pir_simple") ||
        has_flag(argc, argv, "--use_pir_simple")) {
        cerr << "--PIR_single/--PIR_simple (FrodoPIR) has been removed. "
             << "Use --PIR_checklist or --PIR_BatchPIR instead.\n";
        return false;
    }
    USE_PIR_BATCHPIR = has_flag(argc, argv, "--PIR_BatchPIR") ||
                       has_flag(argc, argv, "--USE_PIR_BatchPIR") ||
                       has_flag(argc, argv, "--PIR_batchpir") ||
                       has_flag(argc, argv, "--pir_batchpir") ||
                       has_flag(argc, argv, "--use_pir_batchpir");
    if (db_preset == "acm_dblp" &&
        !requested_calibrate_lkw &&
        !USE_PIR_CHECKLIST &&
        !USE_PIR_BATCHPIR) {
        USE_PIR_BATCHPIR = true;
    }

    if (!parse_oprf_config(argc, argv)) {
        return false;
    }
    if (db_preset == "acm_dblp" && !requested_calibrate_lkw && !USE_OPRF) {
        USE_OPRF = true;
        oprf_set_mechanism("GCAES");
    }

    USE_BATCH = has_flag(argc, argv, "--BATCH") ||
                has_flag(argc, argv, "--USE_BATCH") ||
                has_flag(argc, argv, "--batch") ||
                has_flag(argc, argv, "--use_batch");

    get_size_arg(argc, argv, "--filter_size=", filter_size);
    get_size_arg(argc, argv, "--pir_batchpir_batch_size=", PIR_BATCHPIR_BATCH_SIZE);
    if (has_flag(argc, argv, "--cuckoo_legacy_without_resizing")) {
        cerr << "--cuckoo_legacy_without_resizing has been removed; the Cuckoo filter "
             << "always sizes itself by resizing during construction.\n";
        return false;
    }
    if (has_flag(argc, argv, "--cuckoo_canonical_no_oprf_cache")) {
        cerr << "--cuckoo_canonical_no_oprf_cache has been renamed to "
             << "--cuckoo_canonical_no_prf_cache: the Server rebuilds with its own PRF, "
             << "and only evaluates it obliviously under --server_oprf_interactive.\n";
        return false;
    }
    CUCKOO_CANONICAL_NO_PRF_CACHE =
        has_flag(argc, argv, "--cuckoo_canonical_no_prf_cache");
    if (CUCKOO_CANONICAL_NO_PRF_CACHE && !USE_OPRF) {
        cerr << "--cuckoo_canonical_no_prf_cache requires --OPRF\n";
        return false;
    }

    get_double_arg(argc, argv, "--synthetic_client_close_radius=", SYNTHETIC_CLIENT_CLOSE_RADIUS_DEG);
    get_double_arg(argc, argv, "--synthetic_client_far_min_radius=", SYNTHETIC_CLIENT_FAR_MIN_RADIUS_DEG);
    get_double_arg(argc, argv, "--augmented_server_jitter=", AUGMENTED_SERVER_JITTER_DEG);
    get_double_arg(argc, argv, "--distribution_server_noise_scale=", DISTRIBUTION_SERVER_NOISE_SCALE);
    get_string_arg(argc, argv, "--append_random_server_path=", APPEND_RANDOM_SERVER_PATH);

    if (has_flag(argc, argv, "--generate_client_from_server")) {
        GENERATE_CLIENT_FROM_SERVER = true;
        CLIENT_CLOSE_PATH = "(generated-from-sampled-server)";
        CLIENT_FAR_PATH = "(generated-far-region)";
    }
    if (has_flag(argc, argv, "--append_random_server")) {
        APPEND_RANDOM_SERVER = true;
    }
    if (has_flag(argc, argv, "--append_augmented_server")) {
        APPEND_AUGMENTED_SERVER = true;
    }
    if (has_flag(argc, argv, "--append_distribution_server") ||
        has_flag(argc, argv, "--append_same_distribution_server") ||
        has_flag(argc, argv, "--append_regression_server") ||
        has_flag(argc, argv, "--append_linear_regression_server")) {
        APPEND_DISTRIBUTION_SERVER = true;
        APPEND_RANDOM_SERVER = false;
        APPEND_AUGMENTED_SERVER = false;
    }
    PORTABLE_DATASET_SAMPLING =
        has_flag(argc, argv, "--portable_sampling") ||
        has_flag(argc, argv, "--portable_dataset_sampling");
    PORTABLE_LSH =
        has_flag(argc, argv, "--portable_lsh") ||
        has_flag(argc, argv, "--portable_e2lsh");
    PIR_BATCHPIR_BATCH_SIZE = std::max<size_t>(1, PIR_BATCHPIR_BATCH_SIZE);
    if (USE_PIR_BATCHPIR) {
        USE_BATCH = true;
    }

    get_int_arg(argc, argv, "--server_size=", N);
    get_int_arg(argc, argv, "--num_runs=", NUM_RUNS_PSI);
    get_int_arg(argc, argv, "--num_search_runs=", NUM_RUNS_SEARCH);
    get_int_arg(argc, argv, "--dataset_seed=", DATASET_SEED);
    if (DATASET_SEED < 0) {
        cerr << "--dataset_seed must be nonnegative\n";
        return false;
    }
    if (get_int_arg(argc, argv, "--client_size=", CLIENT_SIZE_SEARCH)) {
        CLIENT_SIZES_LIST = {CLIENT_SIZE_SEARCH};
    }
    if (args.calibrate_lkw && !args.calibration_holdout && !SPACEV_DATASET) {
        try {
            N = static_cast<int>(count_server_json_entries(server_path));
        } catch (const std::exception& ex) {
            cerr << "Could not count Server entries for calibration: " << ex.what() << "\n";
            return false;
        }
        if (N <= 0) {
            cerr << "Server dataset is empty; cannot calibrate.\n";
            return false;
        }
    }

    const bool has_L_arg = get_int_arg(argc, argv, "--L=", L);
    const bool has_k_arg = get_int_arg(argc, argv, "--k=", k);
    const bool has_w_arg = get_double_arg(argc, argv, "--w=", w);
    if (EDIT_DISTANCE) {
        // Focused ACM-DBLP title/year calibration: preserve every explicit value.
        if (!has_L_arg) L = 12;
        if (!has_k_arg) k = 8;
        if (!has_w_arg) w = 5;
    }
    args.has_all_lsh_params =
        (has_L_arg && has_k_arg && has_w_arg) || db_preset == "acm_dblp";

    return validate_parsed_args(args);
}
