#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include "pir_punc_psetggm_port.cpp"

int main() {
    constexpr int row_count = 257;
    constexpr int row_bytes = 19;
    std::vector<pir_punc::Row> rows(
        row_count, pir_punc::Row(static_cast<std::size_t>(row_bytes)));
    for (int row = 0; row < row_count; ++row) {
        for (int byte = 0; byte < row_bytes; ++byte) {
            rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(byte)] =
                static_cast<std::uint8_t>((row * 131 + byte * 17 + 29) & 0xff);
        }
    }

    pir_punc::StaticDB db = pir_punc::static_db_from_rows(rows);
    checklist_pir::HintRequest hint_request;
    checklist_pir::HintResponse hint_response =
        checklist_pir::process_hint_request(hint_request, db);
    checklist_pir::Client client(std::move(hint_response));
    checklist_pir::Server left_server(db);
    checklist_pir::Server right_server(db);

    std::array<int, 3> random_case_counts{};
    for (int repetition = 0; repetition < 3; ++repetition) {
        for (int requested = 0; requested < row_count; ++requested) {
            auto [queries, context] = client.query(requested);

            assert(context.random_case >= 0 && context.random_case <= 2);
            ++random_case_counts[static_cast<std::size_t>(context.random_case)];
            assert(!(queries[checklist_pir::kLeft].extra_element == requested &&
                     queries[checklist_pir::kRight].extra_element == requested));

            std::array<checklist_pir::QueryResponse, 2> responses{
                left_server.process(queries[checklist_pir::kLeft]),
                right_server.process(queries[checklist_pir::kRight])
            };
            const pir_punc::Row reconstructed = client.reconstruct(context, responses);
            assert(reconstructed == rows[static_cast<std::size_t>(requested)]);
        }
    }

    assert(random_case_counts[0] > 0);
    assert(random_case_counts[1] > 0);
    assert(random_case_counts[2] > 0);

    std::cout << "ChecklistPIR self-test passed: 771 correct two-server queries "
                 "across all three randomized cases; no request schema contains "
                 "a dedicated target-index field.\n";
    return 0;
}
