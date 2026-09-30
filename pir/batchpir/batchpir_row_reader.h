#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pir/pir_row_reader.h"

namespace fuzzy_pets_batchpir {

using Row = fuzzy_pets_pir::Row;
using RowBuilder = std::function<Row(size_t)>;

bool available();
std::string unavailable_reason();

// Adds the BatchPIR cuckoo-encoding parameters the experiment reports.
class RowReader : public fuzzy_pets_pir::RowReader {
public:
    virtual size_t batchpir_num_buckets() const { return 0; }
    virtual size_t batchpir_max_bucket_size() const { return 0; }
    virtual size_t batchpir_first_dimension_size() const { return 0; }
    virtual size_t batchpir_server_count() const { return 0; }
};

std::unique_ptr<RowReader> make_row_reader(
    size_t total_rows,
    size_t row_bytes,
    RowBuilder row_builder,
    size_t batch_size
);

} // namespace fuzzy_pets_batchpir
