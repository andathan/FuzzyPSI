// ChecklistPIR row reader over a pir_punc StaticDB.
static std::uint64_t checklist_hint_request_wire_bytes(
    const checklist_pir::HintRequest&
) {
    return WIRE_U32_BYTES;
}

static std::uint64_t checklist_hint_response_wire_bytes(
    const checklist_pir::HintResponse& response
) {
    std::uint64_t hint_bytes = 0;
    for (const auto& hint : response.hints) {
        hint_bytes += wire_vector_bytes(hint.size());
    }
    return 4 * WIRE_U32_BYTES + response.set_generator_key.size() + hint_bytes;
}

static std::uint64_t checklist_query_wire_bytes(
    const checklist_pir::QueryRequest& query
) {
    return WIRE_U32_BYTES + // randomized ExtraElem cover index
           wire_vector_bytes(query.punctured_set.keys.size()) +
           4 * WIRE_U32_BYTES;
}

static std::uint64_t checklist_response_wire_bytes(
    const checklist_pir::QueryResponse& response
) {
    return wire_vector_bytes(response.answer.size()) +
           wire_vector_bytes(response.extra_element.size());
}

static pir_punc::StaticDB checklist_pad_singleton_db(pir_punc::StaticDB db) {
    if (db.numRows == 1) {
        db.flatDb.insert(db.flatDb.end(), db.flatDb.begin(), db.flatDb.end());
        db.numRows = 2;
    }
    return db;
}

class ChecklistPirRowReader : public fuzzy_pets_pir::RowReader {
public:
    explicit ChecklistPirRowReader(pir_punc::StaticDB&& db)
        : db_(checklist_pad_singleton_db(std::move(db))),
          left_server_(db_), right_server_(db_) {
        rebuild_client();
    }

    pir_punc::Row read_row(size_t idx) override {
        if (idx > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::out_of_range("ChecklistPirRowReader: row index exceeds int limit");
        }
        static constexpr int max_attempts = 8;
        for (int attempt = 0; attempt < max_attempts; ++attempt) {
            try {
                return read_row_once(static_cast<int>(idx));
            } catch (const std::runtime_error& error) {
                if (std::string(error.what()) != "query index not covered by hints" ||
                    attempt + 1 == max_attempts) {
                    throw;
                }
                rebuild_client();
            }
        }
        throw std::runtime_error("ChecklistPirRowReader: unreachable retry state");
    }

    std::vector<pir_punc::Row> read_rows(const std::vector<size_t>& indices) override {
        std::vector<pir_punc::Row> rows;
        rows.reserve(indices.size());
        for (size_t idx : indices) {
            rows.push_back(read_row(idx));
        }
        return rows;
    }

    double client_runtime_ms() const override { return client_runtime_ms_; }
    double server_runtime_ms() const override { return server_runtime_ms_; }
    std::uint64_t setup_communication_bytes() const override { return setup_communication_bytes_; }
    std::uint64_t query_communication_bytes() const override { return query_communication_bytes_; }
    std::uint64_t response_communication_bytes() const override { return response_communication_bytes_; }

private:
    void rebuild_client() {
        checklist_pir::HintRequest request;
        setup_communication_bytes_ += checklist_hint_request_wire_bytes(request);
        checklist_pir::HintResponse response =
            checklist_pir::process_hint_request(request, db_);
        setup_communication_bytes_ += checklist_hint_response_wire_bytes(response);
        client_ = std::make_unique<checklist_pir::Client>(std::move(response));
    }

    pir_punc::Row read_row_once(int idx) {
        auto client_start = Clock::now();
        auto [queries, context] = client_->query(idx);
        for (const auto& query : queries) {
            query_communication_bytes_ += checklist_query_wire_bytes(query);
        }
        client_runtime_ms_ += elapsed_ms(client_start, Clock::now());

        // These are intentionally distinct server calls.  A real deployment
        // must place them in non-colluding administrative domains.
        auto server_start = Clock::now();
        std::array<checklist_pir::QueryResponse, 2> responses{
            left_server_.process(queries[checklist_pir::kLeft]),
            right_server_.process(queries[checklist_pir::kRight])
        };
        server_runtime_ms_ += elapsed_ms(server_start, Clock::now());
        for (const auto& response : responses) {
            response_communication_bytes_ += checklist_response_wire_bytes(response);
        }

        client_start = Clock::now();
        pir_punc::Row row = client_->reconstruct(context, responses);
        client_runtime_ms_ += elapsed_ms(client_start, Clock::now());
        return row;
    }

    pir_punc::StaticDB db_;
    checklist_pir::Server left_server_;
    checklist_pir::Server right_server_;
    std::unique_ptr<checklist_pir::Client> client_;
    double client_runtime_ms_ = 0.0;
    double server_runtime_ms_ = 0.0;
    std::uint64_t setup_communication_bytes_ = 0;
    std::uint64_t query_communication_bytes_ = 0;
    std::uint64_t response_communication_bytes_ = 0;
};

// Every cuckoo bucket is one row of a single ChecklistPIR database, so a query
// hides the target among all buckets.
static std::unique_ptr<ChecklistPirRowReader> build_cuckoo_checklist_reader(
    const CuckooFilter& cuckoo
) {
    const size_t row_count = cuckoo.bucket_count();
    if (row_count == 0) {
        throw std::invalid_argument("build_cuckoo_checklist_reader: cuckoo filter has no buckets");
    }
    if (row_count > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::out_of_range(
            "build_cuckoo_checklist_reader: bucket count exceeds the ChecklistPIR int row limit"
        );
    }

    const pir_punc::Row first_row = cuckoo.bucket_row(0);
    pir_punc::StaticDB db;
    db.numRows = static_cast<int>(row_count);
    db.rowLen = static_cast<int>(first_row.size());
    db.flatDb.resize(row_count * first_row.size());
    std::memcpy(db.flatDb.data(), first_row.data(), first_row.size());
    for (size_t i = 1; i < row_count; ++i) {
        const pir_punc::Row row = cuckoo.bucket_row(i);
        if (row.size() != first_row.size()) {
            throw std::runtime_error("build_cuckoo_checklist_reader: cuckoo rows have unequal length");
        }
        std::memcpy(db.flatDb.data() + i * first_row.size(), row.data(), row.size());
    }

    return std::make_unique<ChecklistPirRowReader>(std::move(db));
}
