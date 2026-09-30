#ifndef CUCKOO_FILTER_H
#define CUCKOO_FILTER_H

#include <vector>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

class CuckooFilter {
private:
    size_t num_buckets;
    size_t bucket_size;
    size_t max_kicks;
    size_t attempted_inserts = 0;
    size_t successful_inserts = 0;
    size_t failed_inserts = 0;
    size_t occupied_slots = 0;

    std::vector<std::vector<uint32_t>> table;

    // 0 marks an empty slot, so no stored fingerprint may be 0.
    static uint32_t nonzero_fingerprint(uint32_t fp) {
        return (fp == 0) ? 1 : fp;
    }

    static size_t ceil_div(size_t numerator, size_t denominator) {
        return numerator / denominator + (numerator % denominator != 0 ? 1 : 0);
    }

    // num_buckets is a power of two, so & (num_buckets - 1) reduces mod num_buckets.
    size_t index1(uint64_t key) const {
        return key & (num_buckets - 1);
    }

    // Self-inverse: index2(index2(i1, fp), fp) == i1, which eviction relies on.
    size_t index2(size_t i1, uint32_t fp) const {
        return (i1 ^ std::hash<uint32_t>{}(fp)) & (num_buckets - 1);
    }

    // -----------------------------
    bool insert_fp(uint32_t fp, size_t idx) {
        for (auto& slot : table[idx]) {
            if (slot == 0) {
                slot = fp;
                occupied_slots++;
                return true;
            }
        }
        return false;
    }

    // -----------------------------
    bool contains_fp(uint32_t fp, size_t idx) const {
        for (const auto& slot : table[idx]) {
            if (slot == fp) return true;
        }
        return false;
    }

public:
    struct Lookup {
        uint32_t fp;
        size_t i1;
        size_t i2;
    };

    static size_t next_power_of_two(size_t value) {
        if (value <= 1) {
            return 1;
        }

        size_t power = 1;
        while (power < value) {
            if (power > std::numeric_limits<size_t>::max() / 2) {
                throw std::overflow_error("CuckooFilter bucket count is too large");
            }
            power <<= 1;
        }
        return power;
    }

    // The geometry every filter in this experiment is built with.
    static constexpr size_t default_bucket_size = 4;
    static constexpr size_t default_max_kicks = 500;

    CuckooFilter(size_t size,
                 size_t bucket_size_ = default_bucket_size,
                 size_t max_kicks_ = default_max_kicks)
        : bucket_size(bucket_size_),
          max_kicks(max_kicks_) {

        if (bucket_size == 0) {
            throw std::invalid_argument("CuckooFilter bucket_size must be > 0");
        }

        num_buckets = next_power_of_two(ceil_div(size, bucket_size));

        table.resize(num_buckets, std::vector<uint32_t>(bucket_size, 0));
    }

    // -----------------------------
    void clear() {
        for (auto& bucket : table) {
            std::fill(bucket.begin(), bucket.end(), 0);
        }
        attempted_inserts = 0;
        successful_inserts = 0;
        failed_inserts = 0;
        occupied_slots = 0;
    }

    Lookup lookup_encoded(uint64_t bucket_key, uint32_t fp) const { // Returns the two candidate bucket indices and the fingerprint for a given bucket key and fingerprint.
        fp = nonzero_fingerprint(fp);
        size_t i1 = index1(bucket_key);
        size_t i2 = index2(i1, fp);
        return Lookup{fp, i1, i2};
    }

    // -----------------------------
    // INSERT
    // -----------------------------
    void insert_encoded(uint64_t bucket_key, uint32_t fp) {
        attempted_inserts++;

        Lookup lookup = lookup_encoded(bucket_key, fp);
        fp = lookup.fp;

        if (contains_fp(fp, lookup.i1) || contains_fp(fp, lookup.i2)) {
            successful_inserts++;
            return;
        }

        //try bucket 1
        //try bucket 2
        //if at least one insert is correct, done!
        if (insert_fp(fp, lookup.i1) || insert_fp(fp, lookup.i2)) {
            successful_inserts++;
            return;
        }

        //otherwise evict
        size_t i = (rand() % 2) ? lookup.i1 : lookup.i2;
        bool inserted = false;
        std::vector<std::pair<size_t, size_t>> touched_slots;

        for (size_t kick = 0; kick < max_kicks; kick++) {
            size_t pos = rand() % bucket_size;
            touched_slots.push_back({i, pos});
            std::swap(fp, table[i][pos]);

            i = index2(i, fp);

            if (insert_fp(fp, i)) {
                inserted = true;
                break;
            }
        }
        // Success :)
        if (inserted) {
            successful_inserts++;
        } else { // Failure :( swap back the evicted fingerprints to their original positions
            for (auto it = touched_slots.rbegin(); it != touched_slots.rend(); ++it) {
                std::swap(fp, table[it->first][it->second]);
            }
            failed_inserts++;
        }
    }

    size_t attempted_insert_count() const {
        return attempted_inserts;
    }

    size_t successful_insert_count() const {
        return successful_inserts;
    }

    size_t failed_insert_count() const {
        return failed_inserts;
    }

    double failed_insert_rate() const {
        return attempted_inserts ? double(failed_inserts) / double(attempted_inserts) : 0.0;
    }

    double fill_ratio() const {
        const size_t total = slot_count();
        return total ? double(occupied_slots) / total : 0.0;
    }

    size_t bucket_count() const {
        return num_buckets;
    }

    size_t slots_per_bucket() const {
        return bucket_size;
    }

    size_t slot_count() const {
        return num_buckets * bucket_size;
    }

    size_t fingerprint_size_bits() const {
        return sizeof(uint32_t) * 8;
    }

    size_t total_size_bits() const {
        return slot_count() * fingerprint_size_bits();
    }

    std::vector<uint8_t> bucket_row(size_t idx) const {
        std::vector<uint8_t> row(bucket_size * sizeof(uint32_t), 0);
        const auto& bucket = table.at(idx);

        for (size_t slot_idx = 0; slot_idx < bucket.size(); ++slot_idx) {
            uint32_t fp = bucket[slot_idx];
            size_t off = slot_idx * sizeof(uint32_t);
            row[off + 0] = static_cast<uint8_t>(fp & 0xff);
            row[off + 1] = static_cast<uint8_t>((fp >> 8) & 0xff);
            row[off + 2] = static_cast<uint8_t>((fp >> 16) & 0xff);
            row[off + 3] = static_cast<uint8_t>((fp >> 24) & 0xff);
        }

        return row;
    }

    bool row_contains_fingerprint(const std::vector<uint8_t>& row, uint32_t fp) const {
        if (row.size() < bucket_size * sizeof(uint32_t)) {
            return false;
        }

        fp = nonzero_fingerprint(fp);

        for (size_t slot_idx = 0; slot_idx < bucket_size; ++slot_idx) {
            size_t off = slot_idx * sizeof(uint32_t);
            uint32_t slot =
                static_cast<uint32_t>(row[off + 0]) |
                (static_cast<uint32_t>(row[off + 1]) << 8) |
                (static_cast<uint32_t>(row[off + 2]) << 16) |
                (static_cast<uint32_t>(row[off + 3]) << 24);
            if (slot == fp) return true;
        }

        return false;
    }

    uint64_t read_all_checksum() const {
        uint64_t checksum = 0;

        for (const auto& bucket : table) {
            for (uint32_t slot : bucket) {
                checksum += slot;
            }
        }

        return checksum;
    }

    // -----------------------------
    // MEMBERSHIP
    // -----------------------------
    bool contains_encoded(uint64_t bucket_key, uint32_t fp) const {
        Lookup lookup = lookup_encoded(bucket_key, fp);
        return contains_fp(lookup.fp, lookup.i1) || contains_fp(lookup.fp, lookup.i2);
    }

};

#endif
