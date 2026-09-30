#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

// Order MinHash (OMH) for an ordered sequence of numeric tokens. This follows
// the construction of Marcais et al.: rank uniquified k-mers with an
// independent permutation, retain the ell smallest, then emit them in their
// original sequence order. One independent OMH value is produced per table.
class OrderMinHash {
public:
    OrderMinHash(int max_dim, int tables, int kmer_length, double ell_value,
                 uint64_t seed = 42)
        : max_dim_(max_dim), tables_(tables), kmer_length_(kmer_length), seed_(seed) {
        const double rounded_ell = std::round(ell_value);
        if (max_dim_ <= 0 || tables_ <= 0 || kmer_length_ <= 0 ||
            !std::isfinite(ell_value) || ell_value != rounded_ell || rounded_ell < 2.0) {
            throw std::invalid_argument(
                "OrderMinHash: dim,L,k must be >0 and w (OMH ell) must be an integer >=2"
            );
        }
        ell_ = static_cast<int>(rounded_ell);
        if (kmer_length_ > max_dim_ || ell_ > max_dim_ - kmer_length_ + 1) {
            throw std::invalid_argument(
                "OrderMinHash: require k<=dim and ell<=dim-k+1"
            );
        }
    }

    std::vector<uint64_t> table_keys(const std::vector<double>& sequence) const {
        if (sequence.empty() || static_cast<int>(sequence.size()) > max_dim_) {
            throw std::invalid_argument("OrderMinHash: sequence length must be in [1,dim]");
        }
        if (static_cast<int>(sequence.size()) < kmer_length_ ||
            ell_ > static_cast<int>(sequence.size()) - kmer_length_ + 1) {
            throw std::invalid_argument(
                "OrderMinHash: sequence must contain at least ell k-mers"
            );
        }

        const size_t kmer_count = sequence.size() - static_cast<size_t>(kmer_length_) + 1;
        std::vector<std::vector<uint64_t>> kmers(kmer_count);
        std::vector<uint32_t> occurrences(kmer_count, 0);
        std::map<std::vector<uint64_t>, uint32_t> occurrence_counts;

        for (size_t pos = 0; pos < kmer_count; ++pos) {
            auto& kmer = kmers[pos];
            kmer.reserve(static_cast<size_t>(kmer_length_));
            for (int offset = 0; offset < kmer_length_; ++offset) {
                kmer.push_back(token_bits(sequence[pos + static_cast<size_t>(offset)]));
            }
            occurrences[pos] = occurrence_counts[kmer]++;
        }

        std::vector<uint64_t> keys;
        keys.reserve(static_cast<size_t>(tables_));
        for (int table = 0; table < tables_; ++table) {
            const uint64_t permutation_seed =
                splitmix64(seed_ ^ (static_cast<uint64_t>(table) * 0xd1b54a32d192ed03ULL));

            std::vector<Candidate> candidates;
            candidates.reserve(kmer_count);
            for (size_t pos = 0; pos < kmer_count; ++pos) {
                uint64_t rank = splitmix64(permutation_seed);
                for (uint64_t token : kmers[pos]) {
                    rank = splitmix64(rank ^ token);
                }
                rank = splitmix64(rank ^ static_cast<uint64_t>(occurrences[pos]));
                candidates.push_back(Candidate{pos, occurrences[pos], rank});
            }

            const auto rank_less = [&](const Candidate& left, const Candidate& right) {
                if (left.rank != right.rank) return left.rank < right.rank;
                if (kmers[left.pos] != kmers[right.pos]) {
                    return kmers[left.pos] < kmers[right.pos];
                }
                return left.occurrence < right.occurrence;
            };
            std::partial_sort(
                candidates.begin(),
                candidates.begin() + ell_,
                candidates.end(),
                rank_less
            );
            std::sort(
                candidates.begin(),
                candidates.begin() + ell_,
                [](const Candidate& left, const Candidate& right) {
                    return left.pos < right.pos;
                }
            );

            uint64_t key = splitmix64(
                0x9e3779b97f4a7c15ULL ^ static_cast<uint64_t>(table)
            );
            for (int selected = 0; selected < ell_; ++selected) {
                key = splitmix64(key ^ 0xa0761d6478bd642fULL);
                for (uint64_t token : kmers[candidates[static_cast<size_t>(selected)].pos]) {
                    key = splitmix64(key ^ token);
                }
            }
            keys.push_back(key);
        }
        return keys;
    }

    int get_L() const { return tables_; }
    int get_k() const { return kmer_length_; }
    int get_ell() const { return ell_; }

private:
    struct Candidate {
        size_t pos;
        uint32_t occurrence;
        uint64_t rank;
    };

    static uint64_t token_bits(double value) {
        // Treat the two IEEE representations of zero as the same token.
        if (value == 0.0) return 0;
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    static uint64_t splitmix64(uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    int max_dim_;
    int tables_;
    int kmer_length_;
    int ell_ = 0;
    uint64_t seed_;
};
