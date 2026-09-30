// Cuckoo membership queries over any PIR scheme. Both candidate buckets of every
// lookup are read, so a hit reveals no bucket.
static bool cuckoo_membership_pir_lookups(
    const CuckooFilter& cuckoo,
    const std::vector<CuckooFilter::Lookup>& lookups,
    fuzzy_pets_pir::RowReader& reader
) {
    std::vector<size_t> row_indices;
    row_indices.reserve(lookups.size() * 2);
    for (const auto& lookup : lookups) {
        row_indices.push_back(lookup.i1);
        row_indices.push_back(lookup.i2);
    }

    std::vector<pir_punc::Row> rows = reader.read_rows(row_indices);
    if (rows.size() != row_indices.size()) {
        throw std::runtime_error("Cuckoo PIR response size mismatch");
    }

    for (size_t lookup_idx = 0; lookup_idx < lookups.size(); ++lookup_idx) {
        const CuckooFilter::Lookup& lookup = lookups[lookup_idx];
        const pir_punc::Row& row1 = rows[2 * lookup_idx];
        const pir_punc::Row& row2 = rows[2 * lookup_idx + 1];
        if (cuckoo.row_contains_fingerprint(row1, lookup.fp) ||
            cuckoo.row_contains_fingerprint(row2, lookup.fp)) {
            return true;
        }
    }

    return false;
}

static size_t remember_unique_pir_row(
    size_t row_idx,
    std::vector<size_t>& unique_rows,
    std::unordered_map<size_t, size_t>& unique_pos_by_row
) {
    auto found = unique_pos_by_row.find(row_idx);
    if (found != unique_pos_by_row.end()) {
        return found->second;
    }

    const size_t pos = unique_rows.size();
    unique_pos_by_row.emplace(row_idx, pos);
    unique_rows.push_back(row_idx);
    return pos;
}

static std::vector<std::vector<CuckooFilter::Lookup>> cuckoo_lookup_batches(
    const CuckooFilter& cuckoo,
    const std::vector<std::vector<double>>& imgs,
    E2LSH& lsh
) {
    std::vector<std::vector<CuckooFilter::Lookup>> out(imgs.size());
    parallel_for_indices(imgs.size(), [&](size_t i) {
        out[i] = cuckoo_prf_lookups(cuckoo, imgs[i], lsh, false);
    }, "Cuckoo query bucket lookup");
    return out;
}

template <typename T>
static void require_batch_count(
    const std::vector<std::vector<T>>& batches,
    size_t expected,
    const char* label
) {
    if (batches.size() != expected) {
        throw std::runtime_error(std::string(label) + ": batch count does not match Client item count");
    }
}

struct BatchedCuckooLookup {
    uint32_t fp = 0;
    size_t row1_pos = 0;
    size_t row2_pos = 0;
};

// Each distinct bucket is fetched once, so repeated candidates cost one row read.
static std::vector<bool> cuckoo_membership_pir_batch_lookups(
    const CuckooFilter& cuckoo,
    const std::vector<std::vector<CuckooFilter::Lookup>>& lookup_batches,
    fuzzy_pets_pir::RowReader& reader
) {
    std::vector<std::vector<BatchedCuckooLookup>> planned_lookups(lookup_batches.size());
    std::vector<size_t> unique_row_indices;
    std::unordered_map<size_t, size_t> unique_pos_by_row;
    //prepare PIR requests and deduplicate rows: PIR asks to read only unique rows (so we do not request the same position twice)
    for (size_t item_idx = 0; item_idx < lookup_batches.size(); ++item_idx) {
        planned_lookups[item_idx].reserve(lookup_batches[item_idx].size());
        for (const auto& lookup : lookup_batches[item_idx]) {
            planned_lookups[item_idx].push_back(BatchedCuckooLookup{
                lookup.fp,
                remember_unique_pir_row(lookup.i1, unique_row_indices, unique_pos_by_row),
                remember_unique_pir_row(lookup.i2, unique_row_indices, unique_pos_by_row)
            });
        }
    }
    //read PIR rows
    std::vector<pir_punc::Row> rows = reader.read_rows(unique_row_indices);
    if (rows.size() != unique_row_indices.size()) {
        throw std::runtime_error("Cuckoo PIR batch response size mismatch");
    }

    //check if any match across L tables
    std::vector<bool> reported(lookup_batches.size(), false);
    for (size_t item_idx = 0; item_idx < planned_lookups.size(); ++item_idx) {
        for (const auto& lookup : planned_lookups[item_idx]) {
            if (cuckoo.row_contains_fingerprint(rows[lookup.row1_pos], lookup.fp) ||
                cuckoo.row_contains_fingerprint(rows[lookup.row2_pos], lookup.fp)) {
                reported[item_idx] = true;
                break;
            }
        }
    }

    return reported;
}

static std::vector<bool> batched_pir_memberships_for_reader(
    const CuckooFilter& cuckoo,
    const std::vector<std::vector<double>>& client_imgs,
    E2LSH& lsh,
    const std::vector<std::vector<CuckooFilter::Lookup>>& client_prf_cuckoo_lookups,
    fuzzy_pets_pir::RowReader& reader
) {
    if (USE_OPRF) {
        require_batch_count(client_prf_cuckoo_lookups, client_imgs.size(), "Cuckoo OPRF PIR batch");
        return cuckoo_membership_pir_batch_lookups(cuckoo, client_prf_cuckoo_lookups, reader);
    }

    std::vector<std::vector<CuckooFilter::Lookup>> lookup_batches =
        cuckoo_lookup_batches(cuckoo, client_imgs, lsh);
    return cuckoo_membership_pir_batch_lookups(cuckoo, lookup_batches, reader);
}
