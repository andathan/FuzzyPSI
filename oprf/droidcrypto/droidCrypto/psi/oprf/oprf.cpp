#include <droidCrypto/psi/PhasedPSIClient.h>
#include <droidCrypto/PRNG.h>
#include <droidCrypto/psi/ECNRPSIClient.h>
#include <droidCrypto/psi/OPRFAESPSIClient.h>
#include <droidCrypto/psi/tools/ECNRPRF.h>
#include <droidCrypto/ChannelWrapper.h>
#include "oprf.h"

extern "C" {
}

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <vector>



void getRandomElements(int num_elements, bool first, std::vector<droidCrypto::block> &elements) {
    droidCrypto::SecureRandom rnd;
    // First element is generated the same for client and server
    // first = true should only be set for CF Creation, not for Updates
    size_t i_start = 0;
    if  (first) {
        elements.push_back(droidCrypto::toBlock((const uint8_t*)"ffffffff88888888"));
        i_start = 1;   
    }
    for(int i = i_start; i < num_elements; i++) {
        elements.push_back(rnd.randBlock());
    }
}

void doECNR_OPRF(int num_elements, bool first, char* s_addr, int s_port, uint8_t* ptr) {
    std::vector<droidCrypto::block> elements;
    getRandomElements(num_elements, first, elements);

    droidCrypto::CSocketChannel chan(s_addr, s_port, false);
    droidCrypto::ECNRPSIClient client(chan);
    client.doOPRF(elements, ptr);
}

void doECNR_OPRF_Blocks_Stats(
    const uint8_t* input_blocks,
    int num_elements,
    const char* s_addr,
    int s_port,
    uint8_t* ptr,
    ecnr_oprf_stats_t* stats
) {
    if (stats != nullptr) {
        stats->bytes_sent = 0;
        stats->bytes_recv = 0;
        stats->runtime_ms = 0.0;
    }
    if (input_blocks == nullptr || ptr == nullptr || num_elements <= 0) {
        return;
    }

    const auto start = std::chrono::high_resolution_clock::now();

    std::vector<droidCrypto::block> elements;
    elements.reserve(static_cast<size_t>(num_elements));
    for (int i = 0; i < num_elements; i++) {
        elements.push_back(droidCrypto::toBlock(input_blocks + static_cast<size_t>(i) * 16));
    }

    droidCrypto::CSocketChannel chan(s_addr, static_cast<uint16_t>(s_port), false);
    droidCrypto::ECNRPSIClient client(chan);
    client.Base(elements.size());

    const uint64_t base_bytes_sent = chan.getBytesSent();
    const uint64_t base_bytes_recv = chan.getBytesRecv();

    std::vector<uint8_t> ecnr_outputs(static_cast<size_t>(num_elements) * 33);
    client.OnlineOPRF(elements, ecnr_outputs.data());

    const uint64_t online_bytes_sent = chan.getBytesSent();
    const uint64_t online_bytes_recv = chan.getBytesRecv();
    const auto end = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_elements; i++) {
        std::memcpy(
            ptr + static_cast<size_t>(i) * 16,
            ecnr_outputs.data() + static_cast<size_t>(i) * 33,
            16
        );
    }

    if (stats != nullptr) {
        stats->bytes_sent = base_bytes_sent + online_bytes_sent;
        stats->bytes_recv = base_bytes_recv + online_bytes_recv;
        stats->runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
    }
}

void doECNR_OPRF_Blocks(const uint8_t* input_blocks, int num_elements, const char* s_addr, int s_port, uint8_t* ptr) {
    doECNR_OPRF_Blocks_Stats(input_blocks, num_elements, s_addr, s_port, ptr, nullptr);
}

void doECNR_PRF_Blocks_Stats(
    const uint8_t* input_blocks,
    int num_elements,
    uint8_t* ptr,
    ecnr_oprf_stats_t* stats
) {
    if (stats != nullptr) {
        stats->bytes_sent = 0;
        stats->bytes_recv = 0;
        stats->runtime_ms = 0.0;
    }
    if (input_blocks == nullptr || ptr == nullptr || num_elements <= 0) {
        return;
    }

    const auto start = std::chrono::high_resolution_clock::now();
    droidCrypto::PRNG prng = droidCrypto::PRNG::getTestPRNG();
    droidCrypto::ECNRPRF prf(prng, 128);
    std::array<uint8_t, 33> ecnr_output{};

    for (int i = 0; i < num_elements; i++) {
        const droidCrypto::block input =
            droidCrypto::toBlock(input_blocks + static_cast<size_t>(i) * 16);
        prf.prf(input).toBytes(ecnr_output.data());
        std::memcpy(ptr + static_cast<size_t>(i) * 16, ecnr_output.data(), 16);
    }

    const auto end = std::chrono::high_resolution_clock::now();
    if (stats != nullptr) {
        stats->runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
    }
}


void doGCAES_OPRF(int num_elements, bool first, char* s_addr, int s_port, uint8_t* ptr) {
    std::vector<droidCrypto::block> elements;
    getRandomElements(num_elements, first, elements);

    droidCrypto::CSocketChannel chan(s_addr, s_port, false);
    droidCrypto::OPRFAESPSIClient client(chan);
    client.doOPRF(elements, ptr);
}

void doGCAES_OPRF_Blocks_Stats(
    const uint8_t* input_blocks,
    int num_elements,
    const char* s_addr,
    int s_port,
    uint8_t* ptr,
    gcaes_oprf_stats_t* stats
) {
    if (stats != nullptr) {
        stats->bytes_sent = 0;
        stats->bytes_recv = 0;
        stats->runtime_ms = 0.0;
    }
    if (input_blocks == nullptr || ptr == nullptr || num_elements <= 0) {
        return;
    }

    const auto start = std::chrono::high_resolution_clock::now();

    std::vector<droidCrypto::block> elements;
    elements.reserve(static_cast<size_t>(num_elements));
    for (int i = 0; i < num_elements; i++) {
        elements.push_back(droidCrypto::toBlock(input_blocks + static_cast<size_t>(i) * 16));
    }

    droidCrypto::CSocketChannel chan(s_addr, static_cast<uint16_t>(s_port), false);
    droidCrypto::OPRFAESPSIClient client(chan);
    client.Base(elements.size());

    const uint64_t base_bytes_sent = chan.getBytesSent();
    const uint64_t base_bytes_recv = chan.getBytesRecv();

    client.OnlineOPRF(elements, ptr);

    const uint64_t online_bytes_sent = chan.getBytesSent();
    const uint64_t online_bytes_recv = chan.getBytesRecv();
    const auto end = std::chrono::high_resolution_clock::now();

    if (stats != nullptr) {
        stats->bytes_sent = base_bytes_sent + online_bytes_sent;
        stats->bytes_recv = base_bytes_recv + online_bytes_recv;
        stats->runtime_ms = std::chrono::duration<double, std::milli>(end - start).count();
    }
}

void doGCAES_OPRF_Blocks(const uint8_t* input_blocks, int num_elements, const char* s_addr, int s_port, uint8_t* ptr) {
    doGCAES_OPRF_Blocks_Stats(input_blocks, num_elements, s_addr, s_port, ptr, nullptr);
}

