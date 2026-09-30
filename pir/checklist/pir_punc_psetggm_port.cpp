#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <openssl/rand.h>

#include "Utils/AES.h"
#include "psetggm/answer.h"
#include "psetggm/pset_ggm.h"

namespace pir_punc {

using Row = std::vector<std::uint8_t>;

struct StaticDB {
    int numRows = 0;
    int rowLen = 0;
    std::vector<std::uint8_t> flatDb;
};

StaticDB static_db_from_rows(const std::vector<Row>& rows) {
    StaticDB db;
    db.numRows = static_cast<int>(rows.size());
    db.rowLen = rows.empty() ? 0 : static_cast<int>(rows.front().size());
    db.flatDb.resize(static_cast<size_t>(db.numRows) * static_cast<size_t>(db.rowLen));

    for (int row_idx = 0; row_idx < db.numRows; ++row_idx) {
        if (static_cast<int>(rows[static_cast<size_t>(row_idx)].size()) != db.rowLen) {
            throw std::invalid_argument("pir_punc::static_db_from_rows: unequal row lengths");
        }
        std::memcpy(
            db.flatDb.data() + static_cast<size_t>(row_idx) * static_cast<size_t>(db.rowLen),
            rows[static_cast<size_t>(row_idx)].data(),
            static_cast<size_t>(db.rowLen)
        );
    }

    return db;
}

} // namespace pir_punc

// C++ port of contact-discovery/pir.Punc (ChecklistPIR). Each online query is
// split between two non-colluding servers; no request carries the target index.
namespace checklist_pir {

using Row = pir_punc::Row;
using StaticDB = pir_punc::StaticDB;

constexpr int kSecurityParameter = 128;
constexpr int kLeft = 0;
constexpr int kRight = 1;

struct HintRequest {
    int num_hints_multiplier = static_cast<int>(kSecurityParameter * std::log(2.0));
};

struct SetKey {
    std::uint32_t id = 0;
    std::uint32_t shift = 0;
};

struct PuncturableSet {
    SetKey key;
    int universe_size = 0;
    int set_size = 0;
    std::array<std::uint8_t, 16> seed{};
    std::vector<std::uint64_t> elements;
};

struct PuncturedSet {
    std::vector<std::uint8_t> keys;
    int hole = 0;
    std::uint32_t shift = 0;
    int universe_size = 0;
    int set_size = 0; // Number of elements after puncturing.
};

struct HintResponse {
    int num_rows = 0;
    int row_len = 0;
    int set_size = 0;
    std::uint32_t generator_start = 0;
    std::array<std::uint8_t, 16> set_generator_key{};
    std::vector<Row> hints;
};

struct QueryRequest {
    PuncturedSet punctured_set;
    int extra_element = 0;
};

struct QueryResponse {
    Row answer;
    Row extra_element;
};

struct QueryContext {
    int random_case = 0;
    int set_index = -1;
};

static void random_bytes(std::uint8_t* out, std::size_t len) {
    if (len > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        RAND_bytes(out, static_cast<int>(len)) != 1) {
        throw std::runtime_error("ChecklistPIR: RAND_bytes failed");
    }
}

static std::uint64_t random_below(std::uint64_t upper_bound) {
    if (upper_bound == 0) {
        throw std::invalid_argument("ChecklistPIR: random upper bound must be positive");
    }
    const std::uint64_t reject_below = (std::uint64_t{0} - upper_bound) % upper_bound;
    for (;;) {
        std::uint64_t value = 0;
        random_bytes(reinterpret_cast<std::uint8_t*>(&value), sizeof(value));
        if (value >= reject_below) {
            return value % upper_bound;
        }
    }
}

static void put_u32_le(std::uint8_t* out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8);
    out[2] = static_cast<std::uint8_t>(value >> 16);
    out[3] = static_cast<std::uint8_t>(value >> 24);
}

static std::uint32_t get_u32_le(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(in[0]) |
           (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) |
           (static_cast<std::uint32_t>(in[3]) << 24);
}

static std::uint64_t get_u64_le(const std::uint8_t* in) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | in[i];
    }
    return value;
}

static int arithmetic_mod(std::int64_t value, int modulus) {
    std::int64_t result = value % modulus;
    return static_cast<int>(result < 0 ? result + modulus : result);
}

static void xor_into(Row& destination, const Row& source) {
    if (destination.size() != source.size()) {
        throw std::invalid_argument("ChecklistPIR: row lengths differ during XOR");
    }
    for (std::size_t i = 0; i < destination.size(); ++i) {
        destination[i] ^= source[i];
    }
}

class SetGenerator {
public:
    SetGenerator(
        const std::array<std::uint8_t, 16>& master_key,
        std::uint32_t start_id,
        int universe_size,
        int set_size
    ) : next_id_(start_id),
        universe_size_(universe_size),
        set_size_(set_size),
        aes_(master_key.data()),
        workspace_(workspace_size(
            static_cast<unsigned int>(universe_size),
            static_cast<unsigned int>(set_size))) {
        if (universe_size_ < 2 || set_size_ < 2 || set_size_ > universe_size_) {
            throw std::invalid_argument("ChecklistPIR: invalid universe/set size");
        }
        generator_ = pset_ggm_init(
            static_cast<unsigned int>(universe_size_),
            static_cast<unsigned int>(set_size_),
            workspace_.data());
    }

    SetGenerator(const SetGenerator&) = delete;
    SetGenerator& operator=(const SetGenerator&) = delete;

    std::uint32_t next_id() const { return next_id_; }

    PuncturableSet generate_no_shift() {
        PuncturableSet set = generate_base();
        const auto derived = derive(0xBB, set.key.id);
        set.key.shift = get_u32_le(derived.data()) % static_cast<std::uint32_t>(set_size_);
        return set;
    }

    PuncturableSet generate() {
        PuncturableSet set = generate_no_shift();
        apply_shift(set);
        return set;
    }

    PuncturableSet generate_with(int value) {
        PuncturableSet set = generate_base();
        const auto derived = derive(0xBB, set.key.id);
        const std::size_t position = static_cast<std::size_t>(
            get_u64_le(derived.data()) % static_cast<std::uint64_t>(set_size_));
        set.key.shift = static_cast<std::uint32_t>(arithmetic_mod(
            static_cast<std::int64_t>(value) - static_cast<std::int64_t>(set.elements[position]),
            universe_size_));
        apply_shift(set);
        return set;
    }

    PuncturableSet evaluate(const SetKey& key) {
        PuncturableSet set;
        set.key = key;
        set.universe_size = universe_size_;
        set.set_size = set_size_;
        set.seed = derive(0xAA, key.id);
        set.elements.resize(static_cast<std::size_t>(set_size_));
        pset_ggm_eval(generator_, set.seed.data(), set.elements.data());
        apply_shift(set);
        return set;
    }

    PuncturedSet puncture(const PuncturableSet& set, int value) {
        auto position = std::find(
            set.elements.begin(), set.elements.end(), static_cast<std::uint64_t>(value));
        if (position == set.elements.end()) {
            throw std::runtime_error("ChecklistPIR: punctured value is absent from set");
        }
        const auto hole = static_cast<unsigned int>(position - set.elements.begin());
        PuncturedSet result;
        result.hole = static_cast<int>(hole);
        result.shift = set.key.shift;
        result.universe_size = set.universe_size;
        result.set_size = set.set_size - 1;
        result.keys.resize(pset_buffer_size(generator_));
        pset_ggm_punc(generator_, set.seed.data(), hole, result.keys.data());
        return result;
    }

private:
    std::array<std::uint8_t, 16> derive(std::uint8_t domain, std::uint32_t id) const {
        alignas(16) std::array<std::uint8_t, 16> input{};
        alignas(16) std::array<std::uint8_t, 16> output{};
        input[0] = domain;
        put_u32_le(input.data() + 1, id);
        const block plaintext = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(input.data()));
        const block ciphertext = aes_.encryptECB(plaintext);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(output.data()), ciphertext);
        return output;
    }

    PuncturableSet generate_base() {
        PuncturableSet set;
        set.universe_size = universe_size_;
        set.set_size = set_size_;
        set.elements.resize(static_cast<std::size_t>(set_size_));
        for (;;) {
            set.key = SetKey{next_id_++, 0};
            set.seed = derive(0xAA, set.key.id);
            pset_ggm_eval(generator_, set.seed.data(), set.elements.data());
            std::unordered_set<std::uint64_t> unique(
                set.elements.begin(), set.elements.end());
            if (unique.size() == set.elements.size()) {
                return set;
            }
        }
    }

    void apply_shift(PuncturableSet& set) const {
        if (set.key.shift == 0) {
            return;
        }
        for (std::uint64_t& value : set.elements) {
            value = (value + set.key.shift) % static_cast<std::uint64_t>(universe_size_);
        }
    }

    std::uint32_t next_id_;
    int universe_size_;
    int set_size_;
    AES aes_;
    std::vector<std::uint8_t> workspace_;
    generator* generator_ = nullptr;
};

static HintResponse process_hint_request(const HintRequest& request, const StaticDB& db) {
    if (db.numRows < 2 || db.rowLen <= 0 ||
        db.flatDb.size() != static_cast<std::size_t>(db.numRows) * db.rowLen) {
        throw std::invalid_argument("ChecklistPIR: database must have at least two equal nonempty rows");
    }
    HintResponse response;
    response.num_rows = db.numRows;
    response.row_len = db.rowLen;
    response.set_size = std::max(2, static_cast<int>(std::llround(std::sqrt(db.numRows))));
    response.set_size = std::min(response.set_size, db.numRows);
    random_bytes(response.set_generator_key.data(), response.set_generator_key.size());
    SetGenerator generator(
        response.set_generator_key, 0, response.num_rows, response.set_size);
    const int num_hints = std::max(
        1, request.num_hints_multiplier * response.num_rows / response.set_size);
    response.hints.assign(
        static_cast<std::size_t>(num_hints), Row(static_cast<std::size_t>(db.rowLen), 0));
    for (Row& hint : response.hints) {
        PuncturableSet set = generator.generate();
        for (std::uint64_t row_index : set.elements) {
            const std::size_t offset = static_cast<std::size_t>(row_index) * db.rowLen;
            for (int byte = 0; byte < db.rowLen; ++byte) {
                hint[static_cast<std::size_t>(byte)] ^=
                    db.flatDb[offset + static_cast<std::size_t>(byte)];
            }
        }
    }
    response.generator_start = generator.next_id();
    return response;
}

// A server receives only one punctured-set request.  In a deployment, construct
// one instance in each of two administrative domains and do not share logs/state.
class Server {
public:
    explicit Server(const StaticDB& db) : db_(db) {}

    QueryResponse process(const QueryRequest& request) const {
        const PuncturedSet& set = request.punctured_set;
        if (request.extra_element < 0 || request.extra_element >= db_.numRows ||
            set.universe_size != db_.numRows || set.set_size < 1 ||
            set.hole < 0 || set.hole > set.set_size || set.keys.empty()) {
            throw std::invalid_argument("ChecklistPIR server: malformed query");
        }
        QueryResponse response;
        response.answer.assign(static_cast<std::size_t>(db_.rowLen), 0);
        answer(
            set.keys.data(),
            static_cast<unsigned int>(set.hole),
            static_cast<unsigned int>(set.universe_size),
            static_cast<unsigned int>(set.set_size),
            set.shift,
            db_.flatDb.data(),
            static_cast<unsigned int>(db_.flatDb.size()),
            static_cast<unsigned int>(db_.rowLen),
            static_cast<unsigned int>(db_.rowLen),
            response.answer.data());
        const std::size_t offset =
            static_cast<std::size_t>(request.extra_element) * db_.rowLen;
        response.extra_element.assign(
            db_.flatDb.begin() + static_cast<std::ptrdiff_t>(offset),
            db_.flatDb.begin() + static_cast<std::ptrdiff_t>(offset + db_.rowLen));
        return response;
    }

private:
    const StaticDB& db_;
};

class Client {
public:
    explicit Client(HintResponse response)
        : num_rows_(response.num_rows),
          row_len_(response.row_len),
          set_size_(response.set_size),
          hints_(std::move(response.hints)),
          original_generator_(std::make_unique<SetGenerator>(
              response.set_generator_key, 0, num_rows_, set_size_)) {
        initialize_sets(response.generator_start);
    }

    std::pair<std::array<QueryRequest, 2>, QueryContext> query(int requested_index) {
        if (requested_index < 0 || requested_index >= num_rows_) {
            throw std::out_of_range("ChecklistPIR client: row index out of range");
        }
        QueryContext context;
        context.set_index = find_index(requested_index);
        if (context.set_index < 0) {
            throw std::runtime_error("query index not covered by hints");
        }
        PuncturableSet old_set = evaluate_set(context.set_index);
        PuncturedSet left;
        PuncturedSet right;
        int extra_left = 0;
        int extra_right = 0;
        const std::uint64_t coin = random_below(static_cast<std::uint64_t>(num_rows_));
        context.random_case = coin < static_cast<std::uint64_t>(set_size_ - 1)
            ? 1
            : (coin < static_cast<std::uint64_t>(2 * (set_size_ - 1)) ? 2 : 0);

        if (context.random_case == 0) {
            PuncturableSet new_set = update_generator_->generate_with(requested_index);
            extra_left = random_member_except(new_set, requested_index);
            extra_right = random_member_except(old_set, requested_index);
            left = update_generator_->puncture(new_set, requested_index);
            right = generator_for_set(context.set_index).puncture(old_set, requested_index);
            replace_set(context.set_index, new_set);
        } else if (context.random_case == 1) {
            PuncturableSet new_set = update_generator_->generate_with(requested_index);
            extra_right = random_member_except(new_set, requested_index);
            extra_left = random_member_except(new_set, extra_right);
            left = update_generator_->puncture(new_set, extra_right);
            right = update_generator_->puncture(new_set, requested_index);
        } else {
            PuncturableSet new_set = update_generator_->generate_with(requested_index);
            extra_left = random_member_except(new_set, requested_index);
            extra_right = random_member_except(new_set, extra_left);
            left = update_generator_->puncture(new_set, requested_index);
            right = update_generator_->puncture(new_set, extra_left);
        }

        // QueryRequest deliberately has no requested_index field.  As in the
        // upstream protocol, an ExtraElem cover index may equal the target in
        // one randomized case; its distribution is what prevents a server
        // from learning whether that cover index is the target.
        return {{{QueryRequest{std::move(left), extra_left},
                   QueryRequest{std::move(right), extra_right}}}, context};
    }

    Row reconstruct(
        const QueryContext& context,
        const std::array<QueryResponse, 2>& responses
    ) {
        if (context.set_index < 0 ||
            context.set_index >= static_cast<int>(hints_.size())) {
            throw std::invalid_argument("ChecklistPIR client: invalid query context");
        }
        Row output(static_cast<std::size_t>(row_len_), 0);
        if (context.random_case == 0) {
            xor_into(output, hints_[static_cast<std::size_t>(context.set_index)]);
            xor_into(output, responses[kRight].answer);
            Row& hint = hints_[static_cast<std::size_t>(context.set_index)];
            std::fill(hint.begin(), hint.end(), 0);
            xor_into(hint, responses[kLeft].answer);
            xor_into(hint, output);
        } else if (context.random_case == 1) {
            xor_into(output, responses[kLeft].answer);
            xor_into(output, responses[kRight].answer);
            xor_into(output, responses[kRight].extra_element);
        } else if (context.random_case == 2) {
            xor_into(output, responses[kLeft].answer);
            xor_into(output, responses[kRight].answer);
            xor_into(output, responses[kLeft].extra_element);
        } else {
            throw std::invalid_argument("ChecklistPIR client: invalid random case");
        }
        return output;
    }

private:
    void initialize_sets(std::uint32_t expected_generator_end) {
        sets_.resize(hints_.size());
        index_to_set_.assign(static_cast<std::size_t>(num_rows_), -1);
        occupied_.assign(static_cast<std::size_t>(num_rows_), false);
        for (std::size_t set_index = 0; set_index < hints_.size(); ++set_index) {
            PuncturableSet set = original_generator_->generate_no_shift();
            sets_[set_index] = set.key;
            for (std::uint64_t unshifted : set.elements) {
                const int shifted = static_cast<int>(
                    (unshifted + set.key.shift) % static_cast<std::uint64_t>(num_rows_));
                if (!occupied_[static_cast<std::size_t>(shifted)]) {
                    occupied_[static_cast<std::size_t>(shifted)] = true;
                    index_to_set_[static_cast<std::size_t>(shifted)] =
                        static_cast<int>(set_index);
                }
            }
        }
        if (original_generator_->next_id() != expected_generator_end) {
            throw std::runtime_error("ChecklistPIR: hint/client generator transcripts differ");
        }
        std::array<std::uint8_t, 16> fresh_key{};
        random_bytes(fresh_key.data(), fresh_key.size());
        update_generator_ = std::make_unique<SetGenerator>(
            fresh_key, expected_generator_end, num_rows_, set_size_);
    }

    SetGenerator& generator_for_set(int set_index) {
        return sets_[static_cast<std::size_t>(set_index)].id < original_generator_->next_id()
            ? *original_generator_
            : *update_generator_;
    }

    PuncturableSet evaluate_set(int set_index) {
        return generator_for_set(set_index).evaluate(sets_[static_cast<std::size_t>(set_index)]);
    }

    int find_index(int index) {
        if (occupied_[static_cast<std::size_t>(index)]) {
            return index_to_set_[static_cast<std::size_t>(index)];
        }
        for (std::size_t reverse = sets_.size(); reverse-- > 0;) {
            PuncturableSet set = evaluate_set(static_cast<int>(reverse));
            if (std::find(set.elements.begin(), set.elements.end(),
                    static_cast<std::uint64_t>(index)) != set.elements.end()) {
                return static_cast<int>(reverse);
            }
        }
        return -1;
    }

    int random_member_except(const PuncturableSet& set, int excluded) const {
        for (;;) {
            const std::size_t position = static_cast<std::size_t>(
                random_below(set.elements.size()));
            const int value = static_cast<int>(set.elements[position]);
            if (value != excluded) {
                return value;
            }
        }
    }

    void replace_set(int set_index, const PuncturableSet& replacement) {
        const PuncturableSet old = evaluate_set(set_index);
        for (std::uint64_t value : old.elements) {
            const std::size_t index = static_cast<std::size_t>(value);
            if (occupied_[index] && index_to_set_[index] == set_index) {
                occupied_[index] = false;
            }
        }
        sets_[static_cast<std::size_t>(set_index)] = replacement.key;
        for (std::uint64_t value : replacement.elements) {
            const std::size_t index = static_cast<std::size_t>(value);
            if (!occupied_[index]) {
                occupied_[index] = true;
                index_to_set_[index] = set_index;
            }
        }
    }

    int num_rows_;
    int row_len_;
    int set_size_;
    std::vector<Row> hints_;
    std::vector<SetKey> sets_;
    std::vector<int> index_to_set_;
    std::vector<bool> occupied_;
    std::unique_ptr<SetGenerator> original_generator_;
    std::unique_ptr<SetGenerator> update_generator_;
};

} // namespace checklist_pir
