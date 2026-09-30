#pragma once

#include <cstddef>
#include <cstdint>

void gcaes_oprf_set_server(const char* host, int port);
const char* gcaes_oprf_server_host();
int gcaes_oprf_server_port();
void oprf_set_mechanism(const char* mechanism);
const char* oprf_mechanism();

struct GcaesOprfStats {
    std::uint64_t bytes_sent = 0;
    std::uint64_t bytes_recv = 0;
    double runtime_ms = 0.0;
    std::size_t calls = 0;
    std::size_t blocks = 0;
};

void gcaes_oprf_reset_stats();
GcaesOprfStats gcaes_oprf_stats();
std::uint64_t gcaes_oprf_total_communication_bytes();
double gcaes_oprf_runtime_ms();

void gcaes_prf_eval_blocks(
    const std::uint8_t* input_blocks,
    std::size_t block_count,
    std::uint8_t* output_blocks
);

void oprf_eval_blocks(
    const std::uint8_t* input_blocks,
    std::size_t block_count,
    std::uint8_t* output_blocks
);

void oprf_server_prf_eval_blocks(
    const std::uint8_t* input_blocks,
    std::size_t block_count,
    std::uint8_t* output_blocks
);
