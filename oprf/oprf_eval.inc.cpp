// PRF/OPRF evaluation over 16-byte blocks, either interactive against the OPRF
// server or local, plus the cost accounting for the local path.

struct LocalOprfServerPrfStats {
    double runtime_ms = 0.0;
    std::size_t calls = 0;
    std::size_t blocks = 0;
};

static std::mutex local_oprf_server_prf_stats_mutex;
static LocalOprfServerPrfStats local_oprf_server_prf_accumulated_stats;

static LocalOprfServerPrfStats local_oprf_server_prf_stats() {
    std::lock_guard<std::mutex> lock(local_oprf_server_prf_stats_mutex);
    return local_oprf_server_prf_accumulated_stats;
}

static LocalOprfServerPrfStats local_oprf_server_prf_stats_delta(
    const LocalOprfServerPrfStats& before,
    const LocalOprfServerPrfStats& after
) {
    auto delta_size = [](std::size_t a, std::size_t b) -> std::size_t {
        return a >= b ? a - b : 0;
    };

    LocalOprfServerPrfStats delta;
    delta.runtime_ms = std::max(0.0, after.runtime_ms - before.runtime_ms);
    delta.calls = delta_size(after.calls, before.calls);
    delta.blocks = delta_size(after.blocks, before.blocks);
    return delta;
}

static void add_local_oprf_server_prf_stats(double runtime_ms, std::size_t blocks) {
    std::lock_guard<std::mutex> lock(local_oprf_server_prf_stats_mutex);
    local_oprf_server_prf_accumulated_stats.runtime_ms += runtime_ms;
    local_oprf_server_prf_accumulated_stats.calls += 1;
    local_oprf_server_prf_accumulated_stats.blocks += blocks;
}

static uint64_t mix_domain_table(uint64_t domain, size_t table_idx) {
    uint64_t x = domain ^ (static_cast<uint64_t>(table_idx) + 0x9e3779b97f4a7c15ULL);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static std::array<uint8_t, 16> oprf_input_block(
    uint64_t domain,
    size_t table_idx,
    uint64_t lsh_key
) {
    std::array<uint8_t, 16> block{};
    const uint64_t left = mix_domain_table(domain, table_idx);
    const uint64_t right = lsh_key;
    std::memcpy(block.data(), &left, sizeof(left));
    std::memcpy(block.data() + sizeof(left), &right, sizeof(right));
    return block;
}

static std::vector<std::array<uint8_t, 16>> oprf_blocks(
    const std::vector<std::array<uint8_t, 16>>& inputs
) {
    std::vector<std::array<uint8_t, 16>> outputs(inputs.size());
    if (!inputs.empty()) {
        oprf_eval_blocks( //calls GCAES OPRF server
            inputs.front().data(),
            inputs.size(),
            outputs.front().data()
        );
    }
    return outputs;
}

static bool oprf_uses_adapter_server_prf() {
    return std::strcmp(oprf_mechanism(), "GCAES") != 0;
}

static std::vector<std::array<uint8_t, 16>> oprf_server_prf_blocks( //to-do rename PRF
    const std::vector<std::array<uint8_t, 16>>& inputs
) {
    std::vector<std::array<uint8_t, 16>> outputs(inputs.size());
    if (inputs.empty()) {
        return outputs;
    }

    const auto start = Clock::now();
    if (oprf_uses_adapter_server_prf()) { //used for ECNR | TODO: delete this branch
        oprf_server_prf_eval_blocks(
            inputs.front().data(),
            inputs.size(),
            outputs.front().data()
        );
    } else {
        static const uint8_t zero_key[16] = {}; // Warning : This is a zero key, which is NOT secure for production. Used here only for demonstartion purposes.
        static const AES zero_key_aes(zero_key);

        std::vector<block> plaintexts(inputs.size());
        std::vector<block> ciphertexts(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            plaintexts[i] = toBlock(inputs[i].data());
        }
        zero_key_aes.encryptECBBlocks(
            plaintexts.data(),
            static_cast<uint64_t>(plaintexts.size()),
            ciphertexts.data()
        );
        for (size_t i = 0; i < outputs.size(); ++i) {
            std::memcpy(outputs[i].data(), &ciphertexts[i], outputs[i].size());
        }
    }

    add_local_oprf_server_prf_stats(elapsed_ms(start, Clock::now()), inputs.size());
    return outputs;
}

static std::vector<std::array<uint8_t, 16>> prf_blocks_for_role(
    const std::vector<std::array<uint8_t, 16>>& inputs,
    bool interactive
) {
    return interactive
        ? oprf_blocks(inputs)
        : oprf_server_prf_blocks(inputs);
}

static uint64_t first_u64(const std::array<uint8_t, 16>& block) {
    uint64_t value = 0;
    std::memcpy(&value, block.data(), sizeof(value));
    return value;
}

static uint32_t second_u32_nonzero(const std::array<uint8_t, 16>& block) {
    uint32_t value = 0;
    std::memcpy(&value, block.data() + sizeof(uint64_t), sizeof(value));
    if (value == 0) {
        value = 1;
    }
    return value;
}
