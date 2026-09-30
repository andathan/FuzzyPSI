#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fuzzy_pets_pir {

using Row = std::vector<std::uint8_t>;

// Every PIR scheme exposes the filter as rows and reports what reading them cost.
class RowReader {
public:
    virtual ~RowReader() = default;
    virtual Row read_row(size_t idx) = 0;
    virtual std::vector<Row> read_rows(const std::vector<size_t>& indices) = 0;
    virtual double client_runtime_ms() const { return 0.0; }
    virtual double server_runtime_ms() const { return 0.0; }
    virtual std::uint64_t setup_communication_bytes() const { return 0; }
    virtual std::uint64_t query_communication_bytes() const { return 0; }
    virtual std::uint64_t response_communication_bytes() const { return 0; }
};

} // namespace fuzzy_pets_pir
