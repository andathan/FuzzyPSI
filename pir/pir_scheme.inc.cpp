static const char* active_pir_label() {
    if (USE_PIR_CHECKLIST) {
        return "ChecklistPIR";
    }
    if (USE_PIR_BATCHPIR) {
        return "PIR_BatchPIR";
    }
    return "none";
}

static void print_pir_scheme_for_run(int run, int total_runs) {
    PHASE_LOG << "[PIR] run=" << (run + 1)
         << "/" << total_runs
         << " scheme=" << active_pir_label()
         << "\n";
}

// Per-phase PIR cost of one run, summed over however many readers the scheme uses.
struct PirReaderCosts {
    double server_runtime_ms = 0.0;
    std::uint64_t setup_communication_bytes = 0;
    std::uint64_t query_communication_bytes = 0;
    std::uint64_t response_communication_bytes = 0;

    void add(const fuzzy_pets_pir::RowReader& reader) {
        server_runtime_ms += reader.server_runtime_ms();
        setup_communication_bytes += reader.setup_communication_bytes();
        query_communication_bytes += reader.query_communication_bytes();
        response_communication_bytes += reader.response_communication_bytes();
    }
};
