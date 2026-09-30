// Derives cuckoo bucket and tag from LSH keys through the PRF/OPRF, and builds
// the filter from them.
static constexpr size_t OPRF_INSERT_BATCH_SIZE = 4096;
static constexpr uint64_t PRF_CUCKOO_DOMAIN = 0x4355434b4f4f5f31ULL; // "CUCKOO_1"

struct CuckooPrfEncoded {
    uint64_t bucket;
    uint32_t tag;
};


static std::vector<CuckooPrfEncoded> cuckoo_prf_encoded_keys(
    const std::vector<double>& img,
    const E2LSH& lsh,
    bool interactive = true
) {
    auto keys = lsh.table_keys(img);
    std::vector<CuckooPrfEncoded> encoded;
    encoded.reserve(keys.size());

    std::vector<std::array<uint8_t, 16>> inputs;
    inputs.reserve(keys.size());
    for (size_t table_idx = 0; table_idx < keys.size(); ++table_idx) {
        inputs.push_back(oprf_input_block(PRF_CUCKOO_DOMAIN, table_idx, keys[table_idx]));
    }

    const auto outputs = prf_blocks_for_role(inputs, interactive);
    size_t output_idx = 0;
    for (size_t table_idx = 0; table_idx < keys.size(); ++table_idx) {
        const auto& output = outputs[output_idx++];
        encoded.push_back(CuckooPrfEncoded{
            first_u64(output),
            second_u32_nonzero(output)
        });
    }

    return encoded;
}

static std::vector<std::vector<CuckooPrfEncoded>> cuckoo_prf_encoded_key_batches_range(
    const std::vector<std::vector<double>>& imgs,
    const E2LSH& lsh,
    size_t start,
    size_t count,
    const char* label,
    bool interactive = true
) {
    // Compute the LSH keys for each input point sequentially (tried parallel in the past no additional benefit)
    std::vector<std::vector<uint64_t>> raw_batches(count);
    parallel_for_indices(count, [&](size_t offset) {
        raw_batches[offset] = lsh.table_keys(imgs[start + offset]);
    }, label);

    size_t total_keys = 0;
    for (const auto& keys : raw_batches) {
        total_keys += keys.size();
    }

    // Create a list (inputs) of all the LSH keys for all the points, and then call the PRF on them.
    std::vector<std::array<uint8_t, 16>> inputs;
    inputs.reserve(total_keys);
    for (const auto& keys : raw_batches) {
        for (size_t table_idx = 0; table_idx < keys.size(); ++table_idx) {
            inputs.push_back(oprf_input_block(PRF_CUCKOO_DOMAIN, table_idx, keys[table_idx]));
        }
    }

    //call OPRF/PRF and add them to Cuckoo Filter
    //flag interactive=True -> OPRF is called (client-side query phase)
    //      False -> PRF is called (server-side setup phase)
    //first 64 bytes = bucket number
    //last 32 bytes = fingerprint/tag
    const auto outputs = prf_blocks_for_role(inputs, interactive);
    std::vector<std::vector<CuckooPrfEncoded>> out(count);
    size_t output_idx = 0;
    for (size_t offset = 0; offset < count; ++offset) {
        out[offset].reserve(raw_batches[offset].size());
        for (size_t table_idx = 0; table_idx < raw_batches[offset].size(); ++table_idx) {
            const auto& output = outputs[output_idx++];
            out[offset].push_back(CuckooPrfEncoded{
                first_u64(output),
                second_u32_nonzero(output)
            });
        }
    }
    return out;
}

struct CuckooCanonicalBuildStats {
    size_t cached_entries = 0;
    size_t estimated_unique_entries = 0;
    size_t prf_build_passes = 0;
    size_t initial_slots = 0;
    size_t final_slots = 0;
    size_t final_buckets = 0;
    size_t resize_count = 0;
};

class CuckooDistinctEstimator {
private:
    static constexpr unsigned precision = 16;
    static constexpr size_t register_count = size_t{1} << precision;
    std::array<uint8_t, register_count> registers{};

    static uint64_t mix64(uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

public:
    void add_prf_entry(const CuckooPrfEncoded& encoded) {
        const uint64_t tag = static_cast<uint64_t>(encoded.tag);
        add_hash(mix64(encoded.bucket ^ (tag << 32) ^ tag));
    }

    size_t estimate() const {
        long double inverse_sum = 0.0L;
        size_t zero_registers = 0;
        for (uint8_t rank : registers) {
            inverse_sum += std::ldexp(1.0L, -static_cast<int>(rank));
            zero_registers += rank == 0 ? 1 : 0;
        }

        const long double m = static_cast<long double>(register_count);
        const long double alpha = 0.7213L / (1.0L + 1.079L / m);
        long double estimate = alpha * m * m / inverse_sum;
        if (estimate <= 2.5L * m && zero_registers != 0) {
            estimate = m * std::log(m / static_cast<long double>(zero_registers));
        }
        if (estimate >= static_cast<long double>(std::numeric_limits<size_t>::max())) {
            throw std::overflow_error("Cuckoo distinct-entry estimate overflow");
        }
        return std::max<size_t>(1, static_cast<size_t>(std::ceil(estimate)));
    }

private:
    void add_hash(uint64_t hash) {
        const size_t index = static_cast<size_t>(hash >> (64 - precision));
        const uint64_t remaining = hash << precision;
        const unsigned max_rank = 64 - precision + 1;
        const unsigned rank = remaining == 0
            ? max_rank
            : std::min<unsigned>(std::countl_zero(remaining) + 1, max_rank);
        registers[index] = std::max(registers[index], static_cast<uint8_t>(rank));
    }
};

struct CuckooCachedEntries {
    std::vector<CuckooPrfEncoded> entries;
    size_t estimated_unique_entries = 0;
};

static size_t cuckoo_canonical_initial_slots(size_t entry_count) {
    static constexpr size_t bucket_size = CuckooFilter::default_bucket_size;
    static constexpr long double target_load = 0.85L;

    const long double required_buckets_real =
        static_cast<long double>(entry_count) /
        (static_cast<long double>(bucket_size) * target_load);
    if (required_buckets_real >
        static_cast<long double>(std::numeric_limits<size_t>::max())) {
        throw std::overflow_error("Cuckoo canonical resizing initial size overflow");
    }

    const size_t required_buckets = std::max<size_t>(
        1,
        static_cast<size_t>(std::ceil(required_buckets_real))
    );
    const size_t buckets = CuckooFilter::next_power_of_two(required_buckets);
    if (buckets > std::numeric_limits<size_t>::max() / bucket_size) {
        throw std::overflow_error("Cuckoo canonical resizing slot count overflow");
    }
    return buckets * bucket_size;
}

static CuckooCachedEntries cache_cuckoo_prf_entries_range(
    const std::vector<std::vector<double>>& imgs,
    const E2LSH& lsh,
    size_t range_start,
    size_t range_count,
    bool interactive = OPRF_SERVER_INTERACTIVE
) {
    const size_t range_end = std::min(imgs.size(), range_start + range_count);
    const size_t image_count = range_end - range_start;
    const size_t keys_per_image = static_cast<size_t>(lsh.get_L());
    if (keys_per_image != 0 &&
        image_count > std::numeric_limits<size_t>::max() / keys_per_image) {
        throw std::overflow_error("Cuckoo PRF cache entry count overflow");
    }
    CuckooCachedEntries cached;
    cached.entries.reserve(image_count * keys_per_image);
    CuckooDistinctEstimator distinct;

    for (size_t start = range_start; start < range_end; start += OPRF_INSERT_BATCH_SIZE) {
        const size_t count = std::min(OPRF_INSERT_BATCH_SIZE, range_end - start);
        std::vector<std::vector<CuckooPrfEncoded>> batches =
            cuckoo_prf_encoded_key_batches_range(
                imgs,
                lsh,
                start,
                count,
                nullptr,
                interactive
            );
        for (auto& batch : batches) {
            for (const CuckooPrfEncoded& encoded : batch) {
                distinct.add_prf_entry(encoded);
            }
            cached.entries.insert(
                cached.entries.end(),
                std::make_move_iterator(batch.begin()),
                std::make_move_iterator(batch.end())
            );
        }
    }
    cached.estimated_unique_entries = distinct.estimate();
    return cached;
}

// Doubles the table until every entry inserts without a failure. `feed_entries`
// is called once per attempt and must pass each entry to the sink it receives,
// stopping as soon as the sink returns false.
template <typename FeedEntries>
static void build_cuckoo_by_resizing(
    CuckooFilter& output,
    CuckooCanonicalBuildStats& stats,
    const char* resize_note,
    FeedEntries feed_entries
) {
    size_t candidate_slots = stats.initial_slots;

    for (;;) {
        CuckooFilter candidate(candidate_slots);
        bool failed = false;
        feed_entries(candidate_slots, [&](const CuckooPrfEncoded& encoded) {
            const size_t failures_before = candidate.failed_insert_count();
            candidate.insert_encoded(encoded.bucket, encoded.tag);
            failed = candidate.failed_insert_count() != failures_before;
            return !failed;
        });

        if (!failed) {
            stats.final_slots = candidate.slot_count();
            stats.final_buckets = candidate.bucket_count();
            PHASE_LOG << "** Cuckoo build succeeded"
                 << " final_slots=" << stats.final_slots
                 << " final_buckets=" << stats.final_buckets
                 << " resizes=" << stats.resize_count
                 << " build_passes=" << stats.prf_build_passes
                 << "\n" << std::flush;
            output = std::move(candidate);
            return;
        }

        stats.resize_count++;
        if (candidate.slot_count() > std::numeric_limits<size_t>::max() / 2) {
            throw std::overflow_error("Cuckoo resizing slot count overflow");
        }
        const size_t previous_slots = candidate.slot_count();
        candidate_slots = previous_slots * 2;
        PHASE_LOG << "** Cuckoo full, resizing"
             << " resize_count=" << stats.resize_count
             << " previous_slots=" << previous_slots
             << " next_slots=" << candidate_slots
             << " " << resize_note
             << "\n" << std::flush;
    }
}

static CuckooCanonicalBuildStats build_cuckoo_canonical_prf_range(
    CuckooFilter& output,
    const std::vector<std::vector<double>>& imgs,
    const E2LSH& lsh,
    size_t range_start,
    size_t range_count,
    bool interactive = OPRF_SERVER_INTERACTIVE
) {
    // We will cache the stored values; in case we want to resize the Cuckoo Filter we will not need to recompute the PRF values
    const CuckooCachedEntries cached =
        cache_cuckoo_prf_entries_range(imgs, lsh, range_start, range_count, interactive);

    CuckooCanonicalBuildStats stats;
    stats.cached_entries = cached.entries.size();
    stats.estimated_unique_entries = cached.estimated_unique_entries;
    stats.initial_slots = cuckoo_canonical_initial_slots(cached.estimated_unique_entries);
    // Every resize replays the same cached entries, so the PRF runs once.
    stats.prf_build_passes = 1;

    const std::string resize_note = "cached_entries=" + std::to_string(stats.cached_entries);
    // Build Cuckoo filter
    build_cuckoo_by_resizing(
        output,
        stats,
        resize_note.c_str(),
        [&](size_t /*candidate_slots*/, auto&& sink) {
            for (const CuckooPrfEncoded& encoded : cached.entries) {
                if (!sink(encoded)) {
                    return;
                }
            }
        }
    );
    return stats;
}

static CuckooCanonicalBuildStats build_cuckoo_canonical_recomputing_prf_range(
    CuckooFilter& output,
    const std::vector<std::vector<double>>& imgs,
    const E2LSH& lsh,
    size_t range_start,
    size_t range_count
) {
    const size_t range_end = std::min(imgs.size(), range_start + range_count);
    const size_t image_count = range_end - range_start;
    const size_t keys_per_image = static_cast<size_t>(lsh.get_L());
    if (keys_per_image != 0 &&
        image_count > std::numeric_limits<size_t>::max() / keys_per_image) {
        throw std::overflow_error("Cuckoo resizing PRF entry count overflow");
    }
    const size_t expected_entries = image_count * keys_per_image;

    CuckooCanonicalBuildStats stats;
    stats.cached_entries = 0;
    stats.initial_slots = cuckoo_canonical_initial_slots(expected_entries);

    PHASE_LOG << "** Cuckoo (canonical build with resizing, no PRF cache) starting"
         << " expected_entries=" << expected_entries
         << " initial_slots=" << stats.initial_slots
         << "\n" << std::flush;

    build_cuckoo_by_resizing(
        output,
        stats,
        "PRF_will_be_recomputed=true",
        [&](size_t candidate_slots, auto&& sink) {
            // Nothing is cached, so each attempt re-derives every key from the PRF.
            stats.prf_build_passes++;
            PHASE_LOG << "** Cuckoo recomputing PRF"
                 << " build_attempt=" << stats.prf_build_passes
                 << " candidate_slots=" << candidate_slots
                 << "\n" << std::flush;

            for (size_t start = range_start; start < range_end; start += OPRF_INSERT_BATCH_SIZE) {
                const size_t count = std::min(OPRF_INSERT_BATCH_SIZE, range_end - start);
                const std::vector<std::vector<CuckooPrfEncoded>> batches =
                    cuckoo_prf_encoded_key_batches_range(
                        imgs,
                        lsh,
                        start,
                        count,
                        "Cuckoo resizing recomputed PRF insert LSH bucket lookup",
                        OPRF_SERVER_INTERACTIVE
                    );
                for (const auto& batch : batches) {
                    for (const CuckooPrfEncoded& encoded : batch) {
                        if (!sink(encoded)) {
                            return;
                        }
                    }
                }
            }
        }
    );
    return stats;
}

static std::vector<CuckooFilter::Lookup> cuckoo_prf_lookups(
    const CuckooFilter& cuckoo,
    const std::vector<double>& img,
    const E2LSH& lsh,
    bool interactive = true
) {
    std::vector<CuckooFilter::Lookup> lookups;
    const auto encoded_keys = cuckoo_prf_encoded_keys(img, lsh, interactive);
    lookups.reserve(encoded_keys.size());
    for (const auto& encoded : encoded_keys) {
        const uint64_t bucket = encoded.bucket;
        const uint32_t tag = encoded.tag;
        lookups.push_back(cuckoo.lookup_encoded(bucket, tag));
    }

    return lookups;
}

// Calibration keys the filter with the local PRF, so it never needs an OPRF server.
static bool cuckoo_membership_prf(
    const CuckooFilter& cuckoo,
    const std::vector<double>& img,
    const E2LSH& lsh
) {
    for (const auto& encoded : cuckoo_prf_encoded_keys(img, lsh, false)) {
        if (cuckoo.contains_encoded(encoded.bucket, encoded.tag)) {
            return true;
        }
    }

    return false;
}

static std::vector<std::vector<CuckooFilter::Lookup>> parallel_cuckoo_prf_lookup_batches(
    const CuckooFilter& cuckoo,
    const std::vector<std::vector<double>>& imgs,
    const E2LSH& lsh,
    bool interactive = true
) {
    std::vector<std::vector<CuckooFilter::Lookup>> out(imgs.size());
    const auto encoded_batches = cuckoo_prf_encoded_key_batches_range(
        imgs,
        lsh,
        0,
        imgs.size(),
        "Cuckoo OPRF query LSH bucket lookup",
        interactive
    );
    parallel_for_indices(imgs.size(), [&](size_t i) {
        out[i].reserve(encoded_batches[i].size());
        for (const auto& encoded : encoded_batches[i]) {
            out[i].push_back(cuckoo.lookup_encoded(encoded.bucket, encoded.tag));
        }
    }, "Cuckoo OPRF query bucket lookup");
    return out;
}
