#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pir/batchpir/batchpir_row_reader.h"
#include "cuckoo_filter.h"
#include "lsh/lsh_e2lsh.h"
#include "oprf/droidcrypto_adapter.h"
#include "Utils/AES.h"
#include "Utils/utils.h"
#define PIR_PUNC_NO_DEMO_MAIN
#include "pir/checklist/pir_punc_psetggm_port.cpp"

using namespace std;

#include "test_fuzzypsi_parts/globals_and_utils.inc.cpp"
#include "oprf/oprf_eval.inc.cpp"
#include "test_fuzzypsi_parts/cuckoo_prf_keys.inc.cpp"
#include "pir/checklist/checklist_readers.inc.cpp"
#include "pir/batchpir/batchpir_readers.inc.cpp"
#include "pir/pir_membership.inc.cpp"

namespace {


struct Example {
    std::string name;
    std::vector<double> point;
    bool expected;
};

enum class Stage {
    E2LSH = 1,
    Cuckoo = 2,
    Oprf = 3,
    Construction = 4,
    BatchPir = 5,
    ChecklistPir = 6
};

void check(bool condition, const std::string& label) {
    if (!condition) {
        throw std::runtime_error("check failed: " + label);
    }
}

void wait_for_enter() {
    std::cout << "\nPress Enter to continue..." << std::flush;
    std::string ignored;
    std::getline(std::cin, ignored);
    std::cout << "\n\n";
}

std::string keys_string(const std::vector<std::uint64_t>& keys) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i != 0) out << ", ";
        out << keys[i];
    }
    return out.str() + "]";
}

std::string hex_block(const std::array<std::uint8_t, 16>& block) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < block.size(); ++i) {
        out << std::setw(2) << static_cast<unsigned>(block[i]);
    }
    return out.str();
}

std::size_t collision_count(
    const std::vector<std::uint64_t>& left,
    const std::vector<std::uint64_t>& right
) {
    const std::size_t count = std::min(left.size(), right.size());
    std::size_t collisions = 0;
    for (std::size_t i = 0; i < count; ++i) {
        collisions += left[i] == right[i] ? 1 : 0;
    }
    return collisions;
}

Stage parse_stage(const std::string& value) {
    if (value == "e2lsh") return Stage::E2LSH;
    if (value == "cuckoo") return Stage::Cuckoo;
    if (value == "oprf") return Stage::Oprf;
    if (value == "construction" || value == "paper") return Stage::Construction;
    if (value == "batchpir") return Stage::BatchPir;
    if (value == "checklistpir" || value == "checklist" || value == "all") {
        return Stage::ChecklistPir;
    }
    throw std::invalid_argument(
        "--through must be e2lsh, cuckoo, oprf, construction, batchpir, or all"
    );
}

} // namespace

int main(int argc, char** argv) {
    try {
        Stage through = Stage::ChecklistPir;
        std::string host = "127.0.0.1";
        int port = 50051;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: " << argv[0]
                          << " [--through=e2lsh|cuckoo|oprf|construction|batchpir|checklistpir]"
                          << " [--oprf-addr=HOST:PORT] [--debug]\n";
                return 0;
            }
            if (arg.rfind("--through=", 0) == 0) {
                through = parse_stage(arg.substr(std::strlen("--through=")));
            } else if (arg.rfind("--oprf-addr=", 0) == 0) {
                const std::string address = arg.substr(std::strlen("--oprf-addr="));
                const std::size_t colon = address.rfind(':');
                if (colon == std::string::npos) throw std::invalid_argument("invalid OPRF address");
                host = address.substr(0, colon);
                port = std::stoi(address.substr(colon + 1));
            } else if (arg == "--debug") {
                DEBUG_PHASES = true;
            } else {
                throw std::invalid_argument("unknown argument: " + arg);
            }
        }

        const std::vector<Example> server = {
            {"A", {10.0, 10.0}, true},
            {"B", {20.0, 20.0}, true},
            {"C", {30.0, 30.0}, true},
        };
        const std::vector<Example> queries = {
            {"C-near", {10.05, 9.95}, true},
            {"C-far", {80.0, 80.0}, false},
        };
        constexpr int lsh_L = 8;
        constexpr int lsh_k = 2;
        constexpr double lsh_w = 4.0;
        E2LSH lsh(2, lsh_L, lsh_k, lsh_w, 42, true);

        std::cout << "FuzzyPSI: Demo\n"
                  << "Server Points: A=(10,10), B=(20,20), C=(30,30)\n"
                  << "Client Points: C-near=(10.05,9.95), C-far=(80,80)\n\n";
        wait_for_enter();

        std::cout << "TEST: E2LSH | Main File: lsh/lsh_e2lsh.h | Called function: lsh.table_keys()\n"
                  << "  Parameters: L=" << lsh_L
                  << " k=" << lsh_k
                  << " w=" << lsh_w << "\n";
        const auto a_keys = lsh.table_keys(server[0].point);
        std::cout << "  lsh.table_keys(server[0].point) = "
                  << keys_string(a_keys) << "\n --------- \n";
        for (const auto& query : queries) {
            const auto keys = lsh.table_keys(query.point);
            const auto collisions = collision_count(a_keys, keys);
            std::cout << "  lsh.table_keys(" << query.name << ") = "
                      << keys_string(keys) << "\n"
                      << "  Matching table keys with A = " << collisions
                      << "/" << lsh_L << "\n ---------- \n";
            check(query.expected ? collisions > 0 : collisions == 0,
                  query.name + " has the expected E2LSH behavior");
        }
        wait_for_enter();
        if (through == Stage::E2LSH) {
            std::cout << "\nRequested demonstration stages completed successfully.\n";
            return 0;
        }

        std::cout << "\nTEST: Cuckoo filter |  Main File: cuckoo_filter.h | Aux Files: test_fuzzypsi_parts/cuckoo_prf_keys.inc.cpp (Cuckoo - E2LSH- PRF integration)\n"
                  << "         Called functions: build_cuckoo_canonical_prf_range() , lookup filter.contains_encoded() \n";
        std::vector<std::vector<double>> resize_server;
        for (const auto& item : server) resize_server.push_back(item.point);
        CuckooFilter raw_filter(4);
        const bool requested_debug = DEBUG_PHASES;
        DEBUG_PHASES = true;
        build_cuckoo_canonical_prf_range(
            raw_filter, resize_server, lsh, 0, resize_server.size()
        );
        DEBUG_PHASES = requested_debug;

        const auto a_encoded = cuckoo_prf_encoded_keys(server[0].point, lsh, false);
        std::cout << "  Example entries inserted for server[0] (after PRF):\n";
        for (std::size_t table = 0; table < 3; ++table) {
            const std::uint64_t key = a_keys[table];
            const auto& encoded = a_encoded[table];
            const auto lookup = raw_filter.lookup_encoded(encoded.bucket, encoded.tag);
            std::cout << "    table " << (table + 1)
                      << ": LSH key=" << key
                      << " bucket key=" << encoded.bucket
                      << " fingerprint/tag=" << lookup.fp
                      << " candidate buckets=[" << lookup.i1
                      << ", " << lookup.i2 << "]\n";
        }

        std::cout << "  Membership examples:\n";
        for (std::size_t table = 0; table < 3; ++table) {
            const std::uint64_t key = a_keys[table];
            const bool found = raw_filter.contains_encoded(
                a_encoded[table].bucket, a_encoded[table].tag
            );
            std::cout << "    Check membership of " << key << " -> "
                      << (found ? "FOUND" : "NOT FOUND")
                      << "  [server[0], table " << (table + 1) << "]\n";
            check(found, "inserted Cuckoo key for server[0], table " +
                         std::to_string(table + 1));
        }

        const std::vector<std::pair<std::uint64_t, std::string>> absent_examples = {
            {a_keys[0] + 1, "server[0] table 1 key + 1"},
            {987654321012ULL, "not inserted"},
            {424242424242ULL, "not inserted"},
        };
        for (std::size_t absent_index = 0; absent_index < absent_examples.size(); ++absent_index) {
            const auto& [key, description] = absent_examples[absent_index];
            const std::size_t table = absent_index == 0 ? 0 : absent_index % lsh_L;
            const auto blocks = prf_blocks_for_role(
                {oprf_input_block(PRF_CUCKOO_DOMAIN, table, key)}, false
            );
            const std::uint64_t bucket = first_u64(blocks.front());
            const std::uint32_t tag = second_u32_nonzero(blocks.front());
            const bool found = raw_filter.contains_encoded(bucket, tag);
            std::cout << "    Check membership of " << key << " -> "
                      << (found ? "FOUND" : "NOT FOUND")
                      << "  [" << description << "]\n";
            check(!found, "absent Cuckoo key " + std::to_string(key));
        }
        wait_for_enter();
        if (through == Stage::Cuckoo) {
            std::cout << "\nRequested demonstration stages completed successfully.\n";
            return 0;
        }

        std::cout << "\n TEST: GCAES OPRF |  Main File: oprf/... | \n"
                  << "  Connecting to " << host << ":" << port << "\n";
        oprf_set_mechanism("GCAES");
        gcaes_oprf_set_server(host.c_str(), port);
        gcaes_oprf_reset_stats();

        CuckooFilter protected_filter(4);
        build_cuckoo_canonical_prf_range(
            protected_filter, resize_server, lsh, 0, resize_server.size()
        );

        const std::vector<Example> private_queries = queries;
        std::vector<std::vector<double>> private_query_points;
        for (const auto& query : private_queries) private_query_points.push_back(query.point);
        const auto communication_before = gcaes_oprf_stats();
        const auto query_encoded = cuckoo_prf_encoded_key_batches_range(
            private_query_points, lsh, 0, private_query_points.size(),
            "Cuckoo OPRF query LSH bucket lookup", true
        );
        const auto communication_after = gcaes_oprf_stats();
        const auto call_communication = gcaes_oprf_stats_delta(
            communication_before, communication_after
        );

        std::cout << "  One batched GCAES call: client_points="
                  << private_queries.size()
                  << " blocks=" << call_communication.blocks << "\n";
        for (std::size_t query_index = 0;
             query_index < private_queries.size();
             ++query_index) {
            const auto& query = private_queries[query_index];
            const auto& interactive = query_encoded[query_index];
            const auto reference = cuckoo_prf_encoded_keys(query.point, lsh, false);
            check(interactive.size() == reference.size(), query.name + " OPRF returned every block");
            bool all_equal = true;
            for (std::size_t i = 0; i < interactive.size(); ++i) {
                all_equal = all_equal &&
                    interactive[i].bucket == reference[i].bucket &&
                    interactive[i].tag == reference[i].tag;
            }
            std::cout << "  OPRF-derived Cuckoo entries for " << query.name << ":\n";
            const auto query_lsh_keys = lsh.table_keys(query.point);
            for (std::size_t table = 0; table < interactive.size(); ++table) {
                std::cout << "    table " << (table + 1)
                          << ": LSH key=" << query_lsh_keys[table]
                          << " -> bucket key=" << interactive[table].bucket
                          << " tag=" << interactive[table].tag
                          << "\n";
            }
            check(all_equal, query.name + " interactive OPRF encoding equals experiment server PRF encoding");
            // The encoded client/server entries were compared above. Use the
            // experiment's non-PIR membership function to check this fixture.
            check(cuckoo_membership_prf(protected_filter, query.point, lsh) == query.expected,
                  query.name + " PRF-keyed Cuckoo result matches expectation");
        }
        std::cout << "  Batched OPRF communication:\n"
                  << "    client sent     " << call_communication.bytes_sent
                  << " bytes ("
                  << static_cast<double>(call_communication.bytes_sent) / 1024.0
                  << " KiB)\n"
                  << "    client received " << call_communication.bytes_recv
                  << " bytes ("
                  << static_cast<double>(call_communication.bytes_recv) / (1024.0 * 1024.0)
                  << " MiB)\n"
                  << "  Verify communication on server logs: \n"
                  << "    client received = server Base+Online sent\n"
                  << "    client sent     = server Base+Online recv\n";
        check(call_communication.calls == 1, "both client points used one OPRF call");
        check(call_communication.blocks == private_queries.size() * lsh_L,
              "batched OPRF sent one block per query and LSH table");
        check(call_communication.bytes_sent > 0 && call_communication.bytes_recv > 0,
              "batched OPRF exchanged data in both directions");
        wait_for_enter();
        if (through == Stage::Oprf) {
            std::cout << "\nRequested demonstration stages completed successfully.\n";
            return 0;
        }

        std::cout
            << "\n TEST: Complete pipeline: \n"
            << "\n **** SERVER - Side ***** \n"
            << "  Shared functions used by ./test (cached build, GCAES, BatchPIR):\n"
            << "    build_cuckoo_canonical_prf_range()\n"
            << "      -> cache_cuckoo_prf_entries_range()\n"
            << "      -> cuckoo_prf_encoded_key_batches_range()\n"
            << "         -> E2LSH::table_keys(server point)\n"
            << "         -> prf_blocks_for_role(interactive=false)\n"
            << "         -> oprf_server_prf_blocks()\n"
            << "         -> AES PRF output: bucket key + fingerprint/tag\n"
            << "      -> cuckoo_canonical_initial_slots()\n"
            << "      -> build_cuckoo_by_resizing()\n"
            << "         -> CuckooFilter::insert_encoded()\n";

        std::vector<std::vector<double>> paper_server_points;
        for (const auto& item : server) paper_server_points.push_back(item.point);
        CuckooFilter paper_filter(4);
        const bool construction_requested_debug = DEBUG_PHASES;
        DEBUG_PHASES = false;
        build_cuckoo_canonical_prf_range(
            paper_filter,
            paper_server_points,
            lsh,
            0,
            paper_server_points.size()
        );
        DEBUG_PHASES = construction_requested_debug;

        std::cout << "  Server[0] example:\n"
                  << "    1) E2LSH values = " << keys_string(a_keys) << "\n";
        for (std::size_t table = 0; table < a_keys.size(); ++table) {
            const auto prf_outputs = prf_blocks_for_role(
                {oprf_input_block(PRF_CUCKOO_DOMAIN, table, a_keys[table])},
                false
            );
            const auto& prf_output = prf_outputs.front();
            const std::uint64_t bucket_key = first_u64(prf_output);
            const std::uint32_t tag = second_u32_nonzero(prf_output);
            const auto lookup = paper_filter.lookup_encoded(bucket_key, tag);
            const bool inserted = paper_filter.contains_encoded(bucket_key, tag);
            std::cout << "    LSH # " << (table + 1) << ":\n"
                      << "      1) LSH value       = " << a_keys[table] << "\n"
                      << "      2) PRF output      = " << hex_block(prf_output) << "\n"
                      << "      3) bucket key      = " << bucket_key << "\n"
                      << "         fingerprint/tag = " << tag << "\n"
                      << "         Cuckoo buckets  = [" << lookup.i1
                      << ", " << lookup.i2 << "]\n";
            check(inserted, "server[0] PRF entry was inserted into exact Cuckoo build");
        }

        std::cout << "**** CLIENT - SIDE ****\n"
                  << "   Shared functions used by ./test (cached build, GCAES, batched PIR):\n"
            << "    parallel_cuckoo_prf_lookup_batches()\n"
            << "      -> cuckoo_prf_encoded_key_batches_range(interactive=true)\n"
            << "      -> CuckooFilter::lookup_encoded()\n"
            << "    build_cuckoo_batchpir_reader()\n"
            << "      -> fuzzy_pets_batchpir::make_row_reader()\n"
            << "      -> BatchPirRowReader constructed with Cuckoo bucket rows\n"
            << "    batched_pir_memberships_for_reader()\n"
            << "      -> cuckoo_membership_pir_batch_lookups()\n"
            << "      -> RowReader::read_rows(candidate bucket indices)\n"
            << "      -> BatchPirRowReader::fetch_batch()\n"
            << "      -> [EXTERNAL BATCHPIR LIBRARY vectorized_batchpir ]\n"
            << "         BatchPIRClient::create_queries()\n"
            << "         BatchPIRServer::generate_response()\n"
            << "         BatchPIRClient::decode_responses_chunks()\n";
        if (through == Stage::Construction) {
            wait_for_enter();
            std::cout << "\nRequested demonstration stages completed successfully.\n";
            return 0;
        }
        check(fuzzy_pets_batchpir::available(), "BatchPIR was compiled with Microsoft SEAL");
        PIR_BATCHPIR_BATCH_SIZE = 8;
        USE_OPRF = true;
        USE_BATCH = true;
        USE_PIR_CHECKLIST = false;
        USE_PIR_BATCHPIR = true;

        // Use the experiment's complete client preparation path for this filter.
        const auto client_prf_cuckoo_lookups = parallel_cuckoo_prf_lookup_batches(
            paper_filter, private_query_points, lsh, true
        );
        for (std::size_t i = 0; i < private_queries.size(); ++i) {
            std::cout << "     " << private_queries[i].name << " OPRF lookups:\n";
            std::size_t table = 0;
            for (const auto& lookup : client_prf_cuckoo_lookups[i]) {
                std::cout << "       table " << (++table)
                          << ": bucket_key=" << query_encoded[i][table - 1].bucket
                          << " tag=" << lookup.fp
                          << " -> candidate buckets=[" << lookup.i1
                          << ", " << lookup.i2 << "]\n";
            }
        }

        // These are the same two functions called by experiment_runner.inc.cpp.
        const bool batchpir_requested_debug = DEBUG_PHASES;
        DEBUG_PHASES = true;
        auto reader = build_cuckoo_batchpir_reader(paper_filter);
        const auto private_results = batched_pir_memberships_for_reader(
            paper_filter,
            private_query_points,
            lsh,
            client_prf_cuckoo_lookups,
            *reader
        );
        DEBUG_PHASES = batchpir_requested_debug;
        for (std::size_t i = 0; i < private_queries.size(); ++i) {
            const bool private_result = private_results[i];
            std::cout << "  " << private_queries[i].name
                      << " BatchPIR membership="
                      << (private_result ? "FOUND" : "NOT FOUND") << "\n";
            check(private_result == private_queries[i].expected,
                  private_queries[i].name + " BatchPIR result matches the demo fixture");
        }
        std::cout << "  Serialized Microsoft SEAL traffic produced by that path:\n"
                  << "    setup bytes=" << reader->setup_communication_bytes()
                  << " query bytes=" << reader->query_communication_bytes()
                  << " response bytes=" << reader->response_communication_bytes() << "\n";
        check(reader->query_communication_bytes() > 0, "BatchPIR generated encrypted queries");
        check(reader->response_communication_bytes() > 0, "BatchPIR generated encrypted answers");
        wait_for_enter();
        if (through == Stage::BatchPir) {
            std::cout << "\nRequested demonstration stages completed successfully.\n";
            return 0;
        }

        std::cout
            << "\n TEST: ChecklistPIR | Main Folder: pir/checklist \n"
            << "  The same C-near and C-far OPRF-derived candidate buckets are used from above.\n"
            << "  Shared functions used by ./test --PIR_checklist --batch:\n"
            << "    build_cuckoo_checklist_reader()\n"
            << "      -> ChecklistPirRowReader(Cuckoo bucket rows)\n"
            << "      -> checklist_pir::process_hint_request()\n"
            << "      -> checklist_pir::Client(hints)\n"
            << "    batched_pir_memberships_for_reader()\n"
            << "      -> cuckoo_membership_pir_batch_lookups()\n"
            << "      -> ChecklistPirRowReader::read_rows(candidate bucket indices)\n"
            << "         -> checklist_pir::Client::query() creates two query shares\n"
            << "         -> left checklist_pir::Server::process(left share)\n"
            << "         -> right checklist_pir::Server::process(right share)\n"
            << "         -> checklist_pir::Client::reconstruct(two responses)\n";
        USE_PIR_BATCHPIR = false;
        USE_PIR_CHECKLIST = true;
        auto checklist_reader = build_cuckoo_checklist_reader(paper_filter);
        const auto checklist_results = batched_pir_memberships_for_reader(
            paper_filter,
            private_query_points,
            lsh,
            client_prf_cuckoo_lookups,
            *checklist_reader
        );
        for (std::size_t i = 0; i < private_queries.size(); ++i) {
            const bool private_result = checklist_results[i];
            std::cout << "  " << private_queries[i].name
                      << " ChecklistPIR membership="
                      << (private_result ? "FOUND" : "NOT FOUND") << "\n";
            check(private_result == private_queries[i].expected,
                  private_queries[i].name +
                      " ChecklistPIR result matches the demo fixture");
        }
        std::cout << "  Serialized ChecklistPIR traffic produced by that path:\n"
                  << "    hint/setup bytes="
                  << checklist_reader->setup_communication_bytes()
                  << " query-share bytes="
                  << checklist_reader->query_communication_bytes()
                  << " two-server response bytes="
                  << checklist_reader->response_communication_bytes() << "\n";
        check(checklist_reader->query_communication_bytes() > 0,
              "ChecklistPIR generated private query shares");
        check(checklist_reader->response_communication_bytes() > 0,
              "ChecklistPIR generated two-server answers");
        wait_for_enter();

        std::cout << "\n Demo Done :D \n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nDEMO FAILED: " << error.what() << "\n";
        return 1;
    }
}
