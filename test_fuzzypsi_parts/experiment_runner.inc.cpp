static uint64_t debug_mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static void debug_hash_combine(uint64_t& h, uint64_t value) {
    h ^= debug_mix64(value + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

static uint64_t debug_hash_string(const string& value) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : value) {
        h ^= static_cast<uint64_t>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

static uint64_t debug_hash_double(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t debug_hash_row(const vector<double>& row) {
    uint64_t h = 0x6a09e667f3bcc909ULL;
    debug_hash_combine(h, static_cast<uint64_t>(row.size()));
    for (double value : row) {
        debug_hash_combine(h, debug_hash_double(value));
    }
    return h;
}

static uint64_t debug_hash_rows(const vector<vector<double>>& rows) {
    uint64_t h = 0xbb67ae8584caa73bULL;
    debug_hash_combine(h, static_cast<uint64_t>(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        debug_hash_combine(h, static_cast<uint64_t>(i));
        debug_hash_combine(h, debug_hash_row(rows[i]));
    }
    return h;
}

struct ExactLocationHash {
    size_t operator()(const vector<double>& location) const {
        size_t seed = location.size();
        for (double coordinate : location) {
            const size_t coordinate_hash = std::hash<double>{}(coordinate);
            seed ^= coordinate_hash + 0x9e3779b9U + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

static size_t count_exact_location_matches(
    const vector<vector<double>>& server_locations,
    const vector<vector<double>>& client_locations
) {
    const unordered_set<vector<double>, ExactLocationHash> exact_server_locations(
        server_locations.begin(),
        server_locations.end()
    );

    size_t matches = 0;
    for (const auto& client_location : client_locations) {
        if (exact_server_locations.contains(client_location)) {
            ++matches;
        }
    }
    return matches;
}

static bool is_gowalla_dataset() {
    return db_preset.rfind("gowalla", 0) == 0;
}

static uint64_t debug_hash_ids(const vector<string>& ids) {
    uint64_t h = 0x3c6ef372fe94f82bULL;
    debug_hash_combine(h, static_cast<uint64_t>(ids.size()));
    for (size_t i = 0; i < ids.size(); ++i) {
        debug_hash_combine(h, static_cast<uint64_t>(i));
        debug_hash_combine(h, debug_hash_string(ids[i]));
    }
    return h;
}

static uint64_t debug_hash_labels(const vector<bool>& labels) {
    uint64_t h = 0xa54ff53a5f1d36f1ULL;
    debug_hash_combine(h, static_cast<uint64_t>(labels.size()));
    for (size_t i = 0; i < labels.size(); ++i) {
        debug_hash_combine(h, static_cast<uint64_t>(i));
        debug_hash_combine(h, labels[i] ? 1ULL : 0ULL);
    }
    return h;
}

static void debug_print_sample_signature(
    uint32_t run_seed,
    int client_size,
    int run_index,
    const vector<string>& server_ids,
    const vector<vector<double>>& server_imgs,
    const vector<vector<double>>& client_imgs,
    const vector<bool>& client_is_close
) {
    if (!DEBUG_PHASES) {
        return;
    }

    size_t close_count = 0;
    for (bool is_close : client_is_close) {
        if (is_close) {
            ++close_count;
        }
    }
    const size_t far_count = client_is_close.size() - close_count;

    std::ios_base::fmtflags old_flags = cout.flags();
    cout << "[debug sample] run=" << (run_index + 1)
         << " seed=" << run_seed
         << " requested_client_size=" << client_size
         << " server_count=" << server_imgs.size()
         << " server_rows_hash=0x" << std::hex << debug_hash_rows(server_imgs)
         << " server_ids_hash=0x" << debug_hash_ids(server_ids)
         << std::dec << "\n";
    cout << "[debug sample] client_count=" << client_imgs.size()
         << " close=" << close_count
         << " far=" << far_count
         << " client_rows_hash=0x" << std::hex << debug_hash_rows(client_imgs)
         << " client_labels_hash=0x" << debug_hash_labels(client_is_close)
         << std::dec << "\n";

    const size_t server_preview = std::min<size_t>(5, server_imgs.size());
    for (size_t i = 0; i < server_preview; ++i) {
        cout << "[debug sample server " << i << "] id="
             << (i < server_ids.size() ? server_ids[i] : string("(missing)"))
             << " row_hash=0x" << std::hex << debug_hash_row(server_imgs[i])
             << std::dec << "\n";
    }

    const size_t client_preview = std::min<size_t>(10, client_imgs.size());
    for (size_t i = 0; i < client_preview; ++i) {
        cout << "[debug sample client " << i << "] label="
             << (i < client_is_close.size() && client_is_close[i] ? "close" : "far")
             << " row_hash=0x" << std::hex << debug_hash_row(client_imgs[i])
             << std::dec << "\n";
    }
    cout.flags(old_flags);
    cout << std::flush;
}

static uint64_t debug_hash_lsh_keys(
    const vector<vector<double>>& rows,
    E2LSH& lsh,
    size_t limit
) {
    uint64_t h = 0x510e527fade682d1ULL;
    const size_t count = std::min(limit, rows.size());
    debug_hash_combine(h, static_cast<uint64_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const auto keys = lsh.table_keys(rows[i]);
        debug_hash_combine(h, static_cast<uint64_t>(i));
        debug_hash_combine(h, static_cast<uint64_t>(keys.size()));
        for (uint64_t key : keys) {
            debug_hash_combine(h, key);
        }
    }
    return h;
}

static void debug_print_lsh_signature(
    E2LSH& lsh,
    const vector<vector<double>>& server_imgs,
    const vector<vector<double>>& client_imgs
) {
    if (!DEBUG_PHASES) {
        return;
    }

    std::ios_base::fmtflags old_flags = cout.flags();
    cout << "[debug lsh] ";
    if (lsh.uses_order_minhash()) {
        cout << "family=OrderMinHash ";
    }
    cout << "portable_lsh=" << (PORTABLE_LSH ? "true" : "false")
         << " server_first10_keys_hash=0x" << std::hex
         << debug_hash_lsh_keys(server_imgs, lsh, 10)
         << " client_first10_keys_hash=0x"
         << debug_hash_lsh_keys(client_imgs, lsh, 10)
         << std::dec << "\n";
    cout.flags(old_flags);
    cout << std::flush;
}

//Our fuzzy PSI Protocol
int fuzzypsi(
    int L,
    int k,
    double w,
    string /*mode*/
) {
    double fp_rate_sum = 0.0, fn_rate_sum = 0.0;

    vector<double> fp_rates, fn_rates;
    vector<int> client_sizes;

    PHASE_LOG << "Running for " << NUM_RUNS_PSI
         << " runs per |client| with dataset_seed=" << DATASET_SEED
         << " (run seed = dataset_seed + run_index)" << endl;
    // LSH & filter
    PHASE_LOG << "USING DATA DIMENSIONS: " << dim << "\n";


    // Select OMH only for an explicitly requested ACM-DBLP edit-distance run.
    E2LSH lsh(dim, L, k, w, 42, PORTABLE_LSH, EDIT_DISTANCE);


    //initialize filters; the canonical build below replaces this placeholder
    CuckooFilter cuckoo(CuckooFilter::default_bucket_size);

    vector<string> server_ids;
    vector<vector<double>> server_imgs, client_imgs;
    vector<bool> client_is_close;

    // Run for each given client size (we usually test |client|=1000 but we can sweep over different sizes as well e.g. 10000)
    for (int client_size : CLIENT_SIZES_LIST) 
    {
        fp_rate_sum = 0.0;
        fn_rate_sum = 0.0;
        double total_runtime_ms_sum = 0.0;
        double client_runtime_ms_sum = 0.0;
        double server_runtime_ms_sum = 0.0;
        double server_offline_runtime_ms_sum = 0.0;
        double server_online_runtime_ms_sum = 0.0;
        double filter_build_runtime_ms_sum = 0.0;
        double pir_setup_runtime_ms_sum = 0.0;
        double oprf_runtime_ms_sum = 0.0;
        long double communication_bytes_sum = 0.0L;
        long double pir_setup_communication_bytes_sum = 0.0L;
        long double pir_query_communication_bytes_sum = 0.0L;
        long double pir_response_communication_bytes_sum = 0.0L;
        long double oprf_communication_bytes_sum = 0.0L;
        long double oprf_client_to_server_bytes_sum = 0.0L;
        long double oprf_server_to_client_bytes_sum = 0.0L;
        long double oprf_calls_sum = 0.0L;
        long double oprf_blocks_sum = 0.0L;
        double local_oprf_server_prf_runtime_ms_sum = 0.0;
        long double local_oprf_server_prf_calls_sum = 0.0L;
        long double local_oprf_server_prf_blocks_sum = 0.0L;
        long double batchpir_num_buckets_sum = 0.0L;
        long double batchpir_max_bucket_size_sum = 0.0L;
        long double batchpir_first_dimension_size_sum = 0.0L;
        long double batchpir_server_count_sum = 0.0L;
        long double fp_count_sum = 0.0L;
        long double tn_count_sum = 0.0L;
        long double tp_count_sum = 0.0L;
        long double fn_count_sum = 0.0L;
        long double exact_location_match_count_sum = 0.0L;
        long double cuckoo_failed_sum = 0.0L;
        long double cuckoo_fail_rate_sum = 0.0L;
        long double cuckoo_resize_count_sum = 0.0L;
        long double cuckoo_cached_entries_sum = 0.0L;
        long double cuckoo_estimated_unique_entries_sum = 0.0L;
        long double cuckoo_prf_build_passes_sum = 0.0L;
        long double cuckoo_initial_slots_sum = 0.0L;
        long double cuckoo_final_slots_sum = 0.0L;
        long double cuckoo_final_buckets_sum = 0.0L;
        long double cuckoo_final_size_bits_sum = 0.0L;

        for (int run = 0; run < NUM_RUNS_PSI; run++) {
            const uint32_t run_seed = dataset_seed_for_run(run);
            mt19937 run_rng(run_seed);
            PHASE_LOG << "[dataset seed] client_size=" << client_size
                 << " run=" << (run + 1) << "/" << NUM_RUNS_PSI
                 << " seed=" << run_seed << "\n" << std::flush;

            cout << "\n[RUN: " << (run + 1) << "/" << NUM_RUNS_PSI << "]\n"
                 << "Phase: Creating dataset\n" << std::flush;

            /************************
            Step 1: Load Databases
            ************************/
            if (REUSE_DATASET_BETWEEN_GRID_AND_TEST == 1) { //in case we want to run the exact same sampling of datasets for parameter optimization and experiment run (less privacy but better utility)
            PHASE_LOG << "[phase] Dataset reuse start\n" << std::flush;
            // Build once and reuse it for this run.
            server_ids = fixed_ds.server_ids;
            server_imgs = fixed_ds.server_imgs;
            client_imgs = fixed_ds.client_imgs;
            client_is_close = fixed_ds.client_is_close;
            PHASE_LOG << "[phase] Dataset reuse done server=" << server_imgs.size()
                 << " client=" << client_imgs.size() << "\n" << std::flush;
            }
            else{
            // load new db
            PHASE_LOG << "[phase] Dataset load start server_size=" << N
                 << " client_size=" << client_size
                 << " server_path=" << server_path
                 << "\n" << std::flush;
            PHASE_LOG << "[phase] load_server_from_json call start\n" << std::flush;
            load_server_from_json(server_path, N, run_rng, server_ids, server_imgs);
            PHASE_LOG << "[phase] load_server_from_json call done server=" << server_imgs.size()
                 << " ids=" << server_ids.size() << "\n" << std::flush;
            /*
            std::cout << "\n[Sanity] Server samples:\n";
            for (int i = 0; i < std::min(5, (int)server_imgs.size()); i++) {
                std::cout << "Server[" << i << "]: ";
                for (auto v : server_imgs[i]) std::cout << v << " ";
                std::cout << "\n";
            }
            */
            

            PHASE_LOG << "[phase] load_client_from_json_for_server call start\n" << std::flush;
            load_client_from_json_for_server(
                server_ids,
                server_imgs,
                client_size,
                run_rng,
                client_imgs,
                client_is_close
            );
            PHASE_LOG << "[phase] load_client_from_json_for_server call done client=" << client_imgs.size()
                 << " labels=" << client_is_close.size() << "\n" << std::flush;
            
            /*
            std::cout << "\n[Sanity] Client samples:\n";
            for (int i = 0; i < std::min(10, (int)client_imgs.size()); i++) {
                std::cout << "Client[" << i << "] ("
                          << (client_is_close[i] ? "close" : "far")
                          << "): ";

                for (auto v : client_imgs[i]) std::cout << v << " ";
                std::cout << "\n";
            
            }
            */
           }

            if (is_gowalla_dataset()) {
                exact_location_match_count_sum += count_exact_location_matches(
                    server_imgs,
                    client_imgs
                );
            }

            debug_print_sample_signature(
                run_seed,
                client_size,
                run,
                server_ids,
                server_imgs,
                client_imgs,
                client_is_close
            );
            debug_print_lsh_signature(lsh, server_imgs, client_imgs);

            PHASE_LOG << "[phase] Dataset ready server=" << server_imgs.size()
                 << " client=" << client_imgs.size()
                 << " filter_size=" << filter_size
                 << "\n" << std::flush;
            PHASE_LOG << "[phase] Timed runtime starts after dataset sampling; sampling is excluded\n"
                 << std::flush;

            //debug_print_dataset(server_ids, server_imgs, client_imgs, client_is_close, client_relates_to, 10);
                
                //if (run == 0 && client_size == 10) {
                    //    print_images("Server images (from server.bin)", server);
                    //}


            cout << "Phase: Building filter\n" << std::flush;

             /************************
            Step 2:  Create Filter
            ************************/
            const GcaesOprfStats oprf_stats_before_run = gcaes_oprf_stats();
            const LocalOprfServerPrfStats local_oprf_server_prf_stats_before_run = local_oprf_server_prf_stats();
            auto t_start = chrono::high_resolution_clock::now();
            std::unique_ptr<ChecklistPirRowReader> checklist_reader;
            std::unique_ptr<fuzzy_pets_batchpir::RowReader> pir_batchpir_reader;
            double pir_setup_time_ms_for_run = 0.0;
            CuckooCanonicalBuildStats cuckoo_canonical_stats_for_run;
            print_pir_scheme_for_run(run, NUM_RUNS_PSI);

            PHASE_LOG << "[phase] Filter build start server=" << server_imgs.size()
                 << "\n" << std::flush;
            cuckoo_canonical_stats_for_run =
                CUCKOO_CANONICAL_NO_PRF_CACHE
                    ? build_cuckoo_canonical_recomputing_prf_range(
                          cuckoo, server_imgs, lsh, 0, server_imgs.size()
                      )
                    : build_cuckoo_canonical_prf_range(
                          cuckoo, server_imgs, lsh, 0, server_imgs.size()
                      );
            PHASE_LOG << "[phase] Cuckoo canonical build with resizing done"
                 << " cached_entries=" << cuckoo_canonical_stats_for_run.cached_entries;
            if (!CUCKOO_CANONICAL_NO_PRF_CACHE) {
                PHASE_LOG << " estimated_unique_entries="
                     << cuckoo_canonical_stats_for_run.estimated_unique_entries;
            }
            PHASE_LOG
                 << " initial_slots=" << cuckoo_canonical_stats_for_run.initial_slots
                 << " final_slots=" << cuckoo_canonical_stats_for_run.final_slots
                 << " final_buckets=" << cuckoo_canonical_stats_for_run.final_buckets
                 << " resize_count=" << cuckoo_canonical_stats_for_run.resize_count
                 << "\n" << std::flush;
            auto t_end = chrono::high_resolution_clock::now();
            chrono::duration<double, milli> dt = t_end - t_start;
            PHASE_LOG << "[phase] Filter build done seconds=" << (dt.count() / 1000.0)
                 << "\n" << std::flush;


            //PIR setup: creates PIR readers
            if (USE_PIR_CHECKLIST || USE_PIR_BATCHPIR) {
                cout << "Phase: Setting up PIR\n" << std::flush;
            }
            if (USE_PIR_CHECKLIST) {
                const size_t checklist_rows = cuckoo.bucket_count();
                PHASE_LOG << "[phase] PIR setup start mode=" << active_pir_label()
                     << " rows=" << checklist_rows << "\n" << std::flush;
                auto checklist_setup_start = chrono::high_resolution_clock::now();
                checklist_reader = build_cuckoo_checklist_reader(cuckoo);
                auto checklist_setup_end = chrono::high_resolution_clock::now();
                chrono::duration<double, milli> checklist_setup_dt = checklist_setup_end - checklist_setup_start;
                pir_setup_time_ms_for_run += checklist_setup_dt.count();
                PHASE_LOG << "[phase] PIR setup done mode=" << active_pir_label() << " seconds="
                     << (checklist_setup_dt.count() / 1000.0) << "\n" << std::flush;
            }
            else if (USE_PIR_BATCHPIR) {
                PHASE_LOG << "[phase] PIR setup start mode=PIR_BatchPIR\n" << std::flush;
                auto pir_batchpir_setup_start = chrono::high_resolution_clock::now();
                pir_batchpir_reader = build_cuckoo_batchpir_reader(cuckoo);
                auto pir_batchpir_setup_end = chrono::high_resolution_clock::now();
                chrono::duration<double, milli> pir_batchpir_setup_dt =
                    pir_batchpir_setup_end - pir_batchpir_setup_start;
                pir_setup_time_ms_for_run += pir_batchpir_setup_dt.count();
                PHASE_LOG << "[phase] PIR setup done mode=PIR_BatchPIR seconds="
                     << (pir_batchpir_setup_dt.count() / 1000.0) << "\n" << std::flush;
            }

            //if (run == NUM_RUNS_PSI-1) {
            //    debug_print_dataset(server_ids, server_imgs, client_imgs, client_is_close, client_relates_to, 10);
            //}


            int FP = 0, TN = 0, TP = 0, FN = 0;

            cout << "Phase: Running membership queries\n" << std::flush;
            auto q_start = chrono::high_resolution_clock::now();
            std::vector<std::vector<CuckooFilter::Lookup>> client_prf_cuckoo_lookups;

            // OPRF setup
            if (USE_OPRF) {
                PHASE_LOG << "[phase] Client OPRF lookup precompute start client=" << client_imgs.size()
                     << "\n" << std::flush;
                client_prf_cuckoo_lookups =
                    parallel_cuckoo_prf_lookup_batches(cuckoo, client_imgs, lsh);
                PHASE_LOG << "[phase] Client OPRF lookup precompute done\n" << std::flush;
            }

             /************************
            Step 3: Membership Query 
            ************************/
            std::vector<bool> batched_reported;

            //PIR modes
            if (USE_BATCH && (USE_PIR_CHECKLIST)) {
                PHASE_LOG << "[phase] Batched membership query start mode=" << active_pir_label()
                     << " client="
                     << client_imgs.size() << "\n" << std::flush;
                batched_reported = batched_pir_memberships_for_reader(
                    cuckoo,
                    client_imgs,
                    lsh,
                    client_prf_cuckoo_lookups,
                    *checklist_reader
                );
                PHASE_LOG << "[phase] Batched membership query done mode=" << active_pir_label()
                     << " results="
                     << batched_reported.size() << "\n" << std::flush;
            } else if (USE_BATCH && USE_PIR_BATCHPIR) {
                PHASE_LOG << "[phase] Batched membership query start mode=PIR_BatchPIR client="
                     << client_imgs.size() << "\n" << std::flush;
                batched_reported = batched_pir_memberships_for_reader(
                    cuckoo,
                    client_imgs,
                    lsh,
                    client_prf_cuckoo_lookups,
                    *pir_batchpir_reader
                );
                PHASE_LOG << "[phase] Batched membership query done mode=PIR_BatchPIR results="
                     << batched_reported.size() << "\n" << std::flush;
            }
             /************************
            Step 4: Membership Evaluation 
            ************************/


            PHASE_LOG << "[phase] Membership evaluation start client=" << client_imgs.size()
                 << " use_batch=" << (USE_BATCH ? "true" : "false")
                 << "\n" << std::flush;


            for (size_t i = 0; i < client_imgs.size(); i++) {
                bool is_close = client_is_close[i]; //take gorund truth
                bool is_far = !is_close;

                bool reported = false;
                if (USE_BATCH && (USE_PIR_CHECKLIST || USE_PIR_BATCHPIR)) {
                    reported = batched_reported[i]; //take reported value
                } else if (USE_PIR_CHECKLIST) {
                    const auto lookups = USE_OPRF
                        ? client_prf_cuckoo_lookups[i]
                        : cuckoo_prf_lookups(cuckoo, client_imgs[i], lsh, USE_OPRF);
                    reported = cuckoo_membership_pir_lookups(
                        cuckoo,
                        lookups,
                        *checklist_reader
                    );
                } else {
                    reported = cuckoo_membership_prf(cuckoo, client_imgs[i], lsh);
                }
                //comapare and form statistics
                if (is_far) {
                    if (reported) FP++;
                    else TN++;
                }
                if (is_close) {
                    if (reported) TP++;
                    else FN++;
                }
            }

            /************************
            Step 5: Timing
            ************************/
            auto q_end = chrono::high_resolution_clock::now();
            chrono::duration<double, milli> q_dt = q_end - q_start;
            PHASE_LOG << "[phase] Membership evaluation done seconds=" << (q_dt.count() / 1000.0)
                 << " FP=" << FP
                 << " TN=" << TN
                 << " TP=" << TP
                 << " FN=" << FN
                 << "\n" << std::flush;
            PirReaderCosts pir_costs_for_run;
            if (USE_PIR_CHECKLIST) {
                pir_costs_for_run.add(*checklist_reader);
            } else if (USE_PIR_BATCHPIR && pir_batchpir_reader) {
                pir_costs_for_run.add(*pir_batchpir_reader);
                batchpir_num_buckets_sum +=
                    static_cast<long double>(pir_batchpir_reader->batchpir_num_buckets());
                batchpir_max_bucket_size_sum +=
                    static_cast<long double>(pir_batchpir_reader->batchpir_max_bucket_size());
                batchpir_first_dimension_size_sum +=
                    static_cast<long double>(pir_batchpir_reader->batchpir_first_dimension_size());
                batchpir_server_count_sum +=
                    static_cast<long double>(pir_batchpir_reader->batchpir_server_count());
            }
            const double pir_server_answer_time_ms_for_run = std::max(
                0.0,
                std::min(pir_costs_for_run.server_runtime_ms, q_dt.count())
            );
            const std::uint64_t pir_setup_communication_bytes_for_run =
                pir_costs_for_run.setup_communication_bytes;
            const std::uint64_t pir_query_communication_bytes_for_run =
                pir_costs_for_run.query_communication_bytes;
            const std::uint64_t pir_response_communication_bytes_for_run =
                pir_costs_for_run.response_communication_bytes;
            const GcaesOprfStats oprf_stats_for_run =
                gcaes_oprf_stats_delta(oprf_stats_before_run, gcaes_oprf_stats());
            const LocalOprfServerPrfStats local_oprf_server_prf_stats_for_run =
                local_oprf_server_prf_stats_delta(
                    local_oprf_server_prf_stats_before_run,
                    local_oprf_server_prf_stats()
                );
            const std::uint64_t oprf_communication_bytes_for_run =
                oprf_stats_for_run.bytes_sent + oprf_stats_for_run.bytes_recv;
            if (USE_OPRF) {
                PHASE_LOG << "[phase] Interactive " << oprf_mechanism()
                     << " OPRF measured calls="
                     << oprf_stats_for_run.calls
                     << " blocks=" << oprf_stats_for_run.blocks
                     << " runtime_s=" << (oprf_stats_for_run.runtime_ms / 1000.0)
                     << " comm_mb="
                     << (static_cast<double>(oprf_communication_bytes_for_run) / 1'000'000.0)
                     << "\n" << std::flush;
                if (!OPRF_SERVER_INTERACTIVE) {
                    PHASE_LOG << "[phase] Server local " << oprf_mechanism()
                         << " PRF measured calls="
                         << local_oprf_server_prf_stats_for_run.calls
                         << " blocks=" << local_oprf_server_prf_stats_for_run.blocks
                         << " runtime_s="
                         << (local_oprf_server_prf_stats_for_run.runtime_ms / 1000.0)
                         << "\n" << std::flush;
                }
            }

            const double total_runtime_ms_for_run =
                dt.count() + pir_setup_time_ms_for_run + q_dt.count();
            const double client_runtime_ms_for_run =
                std::max(0.0, q_dt.count() - pir_server_answer_time_ms_for_run);
            const double server_runtime_ms_for_run =
                dt.count() + pir_setup_time_ms_for_run + pir_server_answer_time_ms_for_run;
            const double server_offline_runtime_ms_for_run =
                dt.count() + pir_setup_time_ms_for_run;
            const double server_online_runtime_ms_for_run =
                pir_server_answer_time_ms_for_run;
            const std::uint64_t total_communication_bytes_for_run =
                pir_setup_communication_bytes_for_run +
                pir_query_communication_bytes_for_run +
                pir_response_communication_bytes_for_run +
                oprf_communication_bytes_for_run;

            total_runtime_ms_sum += total_runtime_ms_for_run;
            client_runtime_ms_sum += client_runtime_ms_for_run;
            server_runtime_ms_sum += server_runtime_ms_for_run;
            server_offline_runtime_ms_sum += server_offline_runtime_ms_for_run;
            server_online_runtime_ms_sum += server_online_runtime_ms_for_run;
            filter_build_runtime_ms_sum += dt.count();
            pir_setup_runtime_ms_sum += pir_setup_time_ms_for_run;
            oprf_runtime_ms_sum += oprf_stats_for_run.runtime_ms;
            communication_bytes_sum += static_cast<long double>(total_communication_bytes_for_run);
            pir_setup_communication_bytes_sum +=
                static_cast<long double>(pir_setup_communication_bytes_for_run);
            pir_query_communication_bytes_sum +=
                static_cast<long double>(pir_query_communication_bytes_for_run);
            pir_response_communication_bytes_sum +=
                static_cast<long double>(pir_response_communication_bytes_for_run);
            oprf_communication_bytes_sum +=
                static_cast<long double>(oprf_communication_bytes_for_run);
            oprf_client_to_server_bytes_sum +=
                static_cast<long double>(oprf_stats_for_run.bytes_sent);
            oprf_server_to_client_bytes_sum +=
                static_cast<long double>(oprf_stats_for_run.bytes_recv);
            oprf_calls_sum += static_cast<long double>(oprf_stats_for_run.calls);
            oprf_blocks_sum += static_cast<long double>(oprf_stats_for_run.blocks);
            local_oprf_server_prf_runtime_ms_sum +=
                local_oprf_server_prf_stats_for_run.runtime_ms;
            local_oprf_server_prf_calls_sum +=
                static_cast<long double>(local_oprf_server_prf_stats_for_run.calls);
            local_oprf_server_prf_blocks_sum +=
                static_cast<long double>(local_oprf_server_prf_stats_for_run.blocks);

            const double fp_rate = (FP + TN) ? double(FP) / double(FP + TN) : 0.0;
            const double fn_rate = (TP + FN) ? double(FN) / double(TP + FN) : 0.0;

            fp_rate_sum += fp_rate;
            fn_rate_sum += fn_rate;
            fp_count_sum += static_cast<long double>(FP);
            tn_count_sum += static_cast<long double>(TN);
            tp_count_sum += static_cast<long double>(TP);
            fn_count_sum += static_cast<long double>(FN);
            cuckoo_resize_count_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.resize_count
            );
            cuckoo_cached_entries_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.cached_entries
            );
            cuckoo_estimated_unique_entries_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.estimated_unique_entries
            );
            cuckoo_prf_build_passes_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.prf_build_passes
            );
            cuckoo_initial_slots_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.initial_slots
            );
            cuckoo_final_slots_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.final_slots
            );
            cuckoo_final_buckets_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.final_buckets
            );
            cuckoo_final_size_bits_sum += static_cast<long double>(
                cuckoo_canonical_stats_for_run.final_slots
            ) * sizeof(uint32_t) * 8;
            cuckoo_failed_sum += static_cast<long double>(cuckoo.failed_insert_count());
            cuckoo_fail_rate_sum += static_cast<long double>(cuckoo.failed_insert_rate());
           /*
            cout << "[run] client_size=" << client_size
                 << " | FP=" << FP << " TN=" << TN
                 << " TP=" << TP << " FN=" << FN
                 << " | fp_rate=" << fp_rate
                 << " fn_rate=" << fn_rate
                 << "\n";
           */
        }

        const double avg_fp = fp_rate_sum / NUM_RUNS_PSI;
        const double avg_fn = fn_rate_sum / NUM_RUNS_PSI;
        const double avg_total_runtime_s = (total_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_client_runtime_s = (client_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_server_runtime_s = (server_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_server_offline_runtime_s =
            (server_offline_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_server_online_runtime_s =
            (server_online_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_filter_build_runtime_s =
            (filter_build_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_pir_setup_runtime_s =
            (pir_setup_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_oprf_runtime_s =
            (oprf_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const long double average_filter_size_bits =
            cuckoo_final_size_bits_sum / NUM_RUNS_PSI;
        const double filter_size_mb =
            static_cast<double>(average_filter_size_bits) / 8.0 / 1'000'000.0;
        const double avg_communication_mb =
            static_cast<double>((communication_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_pir_setup_communication_mb =
            static_cast<double>((pir_setup_communication_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_pir_query_communication_mb =
            static_cast<double>((pir_query_communication_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_pir_response_communication_mb =
            static_cast<double>((pir_response_communication_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_oprf_communication_mb =
            static_cast<double>((oprf_communication_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_oprf_client_to_server_mb =
            static_cast<double>((oprf_client_to_server_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_oprf_server_to_client_mb =
            static_cast<double>((oprf_server_to_client_bytes_sum / NUM_RUNS_PSI) / 1'000'000.0L);
        const double avg_oprf_calls =
            static_cast<double>(oprf_calls_sum / NUM_RUNS_PSI);
        const double avg_oprf_blocks =
            static_cast<double>(oprf_blocks_sum / NUM_RUNS_PSI);
        const double avg_local_oprf_server_prf_runtime_s =
            (local_oprf_server_prf_runtime_ms_sum / NUM_RUNS_PSI) / 1000.0;
        const double avg_local_oprf_server_prf_calls =
            static_cast<double>(local_oprf_server_prf_calls_sum / NUM_RUNS_PSI);
        const double avg_local_oprf_server_prf_blocks =
            static_cast<double>(local_oprf_server_prf_blocks_sum / NUM_RUNS_PSI);
        const double avg_batchpir_num_buckets =
            static_cast<double>(batchpir_num_buckets_sum / NUM_RUNS_PSI);
        const double avg_batchpir_max_bucket_size =
            static_cast<double>(batchpir_max_bucket_size_sum / NUM_RUNS_PSI);
        const double avg_batchpir_first_dimension_size =
            static_cast<double>(batchpir_first_dimension_size_sum / NUM_RUNS_PSI);
        const double avg_batchpir_server_count =
            static_cast<double>(batchpir_server_count_sum / NUM_RUNS_PSI);
        const double avg_fp_count = static_cast<double>(fp_count_sum / NUM_RUNS_PSI);
        const double avg_tn_count = static_cast<double>(tn_count_sum / NUM_RUNS_PSI);
        const double avg_tp_count = static_cast<double>(tp_count_sum / NUM_RUNS_PSI);
        const double avg_fn_count = static_cast<double>(fn_count_sum / NUM_RUNS_PSI);
        const double avg_exact_location_match_count =
            static_cast<double>(exact_location_match_count_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_failed = static_cast<double>(cuckoo_failed_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_fail_rate = static_cast<double>(cuckoo_fail_rate_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_resize_count =
            static_cast<double>(cuckoo_resize_count_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_cached_entries =
            static_cast<double>(cuckoo_cached_entries_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_estimated_unique_entries =
            static_cast<double>(cuckoo_estimated_unique_entries_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_prf_build_passes =
            static_cast<double>(cuckoo_prf_build_passes_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_initial_slots =
            static_cast<double>(cuckoo_initial_slots_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_final_slots =
            static_cast<double>(cuckoo_final_slots_sum / NUM_RUNS_PSI);
        const double avg_cuckoo_final_buckets =
            static_cast<double>(cuckoo_final_buckets_sum / NUM_RUNS_PSI);
        const double avg_num_correct = avg_tp_count + avg_fn_count;
        const double avg_num_matched = avg_tp_count + avg_fp_count;
        const double gold_precision = avg_num_matched > 0.0
            ? avg_tp_count / avg_num_matched
            : 0.0;
        const double gold_recall = avg_num_correct > 0.0
            ? avg_tp_count / avg_num_correct
            : 0.0;

        if (db_preset == "acm_dblp") {
            cout << "\n[EVALUATION] labels=DBLP-ACM_gold_record_pairs"
                 << " source=original_text_records"
                 << " embedding_threshold_used=false\n";
        }
        cout << "\n*****\n"
             << "Results (avg over runs):\n"
             << "*****\n"
             << "Server Size: " << N << "\n"
             << "Client Size: " << client_size << "\n"
             << "FP Rate: " << avg_fp << "\n"
             << "FN Rate: " << avg_fn << "\n"
             << "Total Runtime: " << avg_total_runtime_s << " s\n"
             << "Total Communication Cost: " << avg_communication_mb << " MB\n"
             << "\nExtra Logs:\n"
             << "  L: " << L << "\n"
             << "  k: " << k << "\n"
             << "  w: " << w << "\n"
             << "  Filter Size: "
             << filter_size_mb << " MB"
             << "\n"
             << "  FP Count: "
             << avg_fp_count
             << "\n"
             << "  TN Count: "
             << avg_tn_count
             << "\n"
             << "  TP Count: "
             << avg_tp_count
             << "\n"
             << "  FN Count: "
             << avg_fn_count
             << "\n";
        if (is_gowalla_dataset()) {
            cout << "  Exact PSI Match Count (100% identical locations): "
                 << avg_exact_location_match_count
                 << "\n";
        }
        cout << (db_preset == "acm_dblp"
                     ? "  Gold-standard Precision (DBLP-ACM record pairs): "
                     : "")
             << (db_preset == "acm_dblp" ? std::to_string(gold_precision) : "")
             << (db_preset == "acm_dblp" ? "\n" : "")
             << (db_preset == "acm_dblp"
                     ? "  Gold-standard Recall (DBLP-ACM record pairs): "
                     : "")
             << (db_preset == "acm_dblp" ? std::to_string(gold_recall) : "")
             << (db_preset == "acm_dblp" ? "\n" : "")
             << "  Cuckoo Failed Inserts: "
             << avg_cuckoo_failed
             << "\n"
             << "  Cuckoo Fail Rate: "
             << avg_cuckoo_fail_rate
             << "\n"
             << "  Num Correct: "
             << avg_num_correct
             << "\n"
             << "  Num Matched: "
             << avg_num_matched
             << "\n"
             << "  Client Runtime (query generation + decode/results): "
             << avg_client_runtime_s << " s"
             << "\n"
             << "  Server Runtime (filter construction + PIR setup/answers): "
             << avg_server_runtime_s << " s"
             << "\n"
             << "  Server Offline Runtime (filter construction + PIR setup): "
             << avg_server_offline_runtime_s << " s"
             << "\n"
             << "  Server Online Runtime (PIR answers): "
             << avg_server_online_runtime_s << " s"
             << "\n"
             << "  Filter Build Runtime: "
             << avg_filter_build_runtime_s << " s"
             << "\n"
             << "  PIR Setup Runtime: "
             << avg_pir_setup_runtime_s << " s"
             << "\n";
        cout << "  Avg Cuckoo Resize Count: "
             << avg_cuckoo_resize_count
             << "\n"
             << "  Avg Cuckoo Cached Entries: "
             << avg_cuckoo_cached_entries
             << "\n"
             << "  Avg Cuckoo Initial Slots: "
             << avg_cuckoo_initial_slots
             << "\n"
             << "  Avg Cuckoo Final Slots: "
             << avg_cuckoo_final_slots
             << "\n"
             << "  Avg Cuckoo Final Buckets: "
             << avg_cuckoo_final_buckets
             << "\n";
        if (CUCKOO_CANONICAL_NO_PRF_CACHE) {
            cout << "  Avg Cuckoo PRF Build Attempts: "
                 << avg_cuckoo_prf_build_passes
                 << "\n";
        } else {
            cout << "  Avg Entries after LSH compression: "
                 << avg_cuckoo_estimated_unique_entries
                 << "\n";
        }
        if (USE_OPRF) {
            cout << "  Interactive " << oprf_mechanism()
                 << " OPRF Runtime (included in total): "
                 << avg_oprf_runtime_s << " s"
                 << "\n"
                 << "  Interactive " << oprf_mechanism()
                 << " OPRF Communication Cost (included in total): "
                 << avg_oprf_communication_mb << " MB"
                 << "\n"
                 << "  Interactive " << oprf_mechanism()
                 << " OPRF Client -> Server Communication: "
                 << avg_oprf_client_to_server_mb << " MB"
                 << "\n"
                 << "  Interactive " << oprf_mechanism()
                 << " OPRF Server -> Client Communication: "
                 << avg_oprf_server_to_client_mb << " MB"
                 << "\n"
                 << "  Interactive " << oprf_mechanism()
                 << " OPRF Calls: "
                 << avg_oprf_calls
                 << "\n"
                 << "  Interactive " << oprf_mechanism()
                 << " OPRF Blocks: "
                 << avg_oprf_blocks
                 << "\n";
            if (!OPRF_SERVER_INTERACTIVE) {
                cout << "  Server Local " << oprf_mechanism()
                     << " PRF Runtime (included in filter build): "
                     << avg_local_oprf_server_prf_runtime_s << " s"
                     << "\n"
                     << "  Server Local " << oprf_mechanism()
                     << " PRF Communication Cost: 0 MB"
                     << "\n"
                     << "  Server Local " << oprf_mechanism()
                     << " PRF Calls: "
                     << avg_local_oprf_server_prf_calls
                     << "\n"
                     << "  Server Local " << oprf_mechanism()
                     << " PRF Blocks: "
                     << avg_local_oprf_server_prf_blocks
                     << "\n";
            }
        }
        if (USE_PIR_CHECKLIST || USE_PIR_BATCHPIR || USE_OPRF) {
            cout << "  PIR Query Communication Cost (Client -> Server): "
                 << avg_pir_query_communication_mb << " MB"
                 << "\n"
                 << "  PIR Answer Communication Cost (Server -> Client): "
                 << avg_pir_response_communication_mb << " MB"
                 << "\n"
                 << "  PIR Setup Exchange Cost (metadata/keys, not DB build): "
                 << avg_pir_setup_communication_mb << " MB"
                 << "\n";
        }
        if (USE_PIR_BATCHPIR) {
            cout << "  BatchPIR Num Buckets: "
                 << avg_batchpir_num_buckets
                 << "\n"
                 << "  BatchPIR Max Bucket Size: "
                 << avg_batchpir_max_bucket_size
                 << "\n"
                 << "  BatchPIR First Dimension Size: "
                 << avg_batchpir_first_dimension_size
                 << "\n"
                 << "  BatchPIR Server Count: "
                 << avg_batchpir_server_count
                 << "\n";
        }
    }

    return 0;
}
