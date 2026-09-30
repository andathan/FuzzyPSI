// Every cuckoo bucket is one BatchPIR row, batched into one multi-query.
static std::unique_ptr<fuzzy_pets_batchpir::RowReader> build_cuckoo_batchpir_reader(
    const CuckooFilter& cuckoo
) {
    const size_t row_bytes = cuckoo.slots_per_bucket() * sizeof(uint32_t);
    return fuzzy_pets_batchpir::make_row_reader(
        cuckoo.bucket_count(),
        row_bytes,
        [&cuckoo](size_t i) { return cuckoo.bucket_row(i); },
        PIR_BATCHPIR_BATCH_SIZE
    );
}
