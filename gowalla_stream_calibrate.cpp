#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsh/lsh_e2lsh.h"

using Clock = std::chrono::steady_clock;
using Point = std::array<double, 2>;
using json = nlohmann::json;

struct Candidate {
    int L;
    int k;
    double w;
    E2LSH lsh;
    std::unordered_set<std::uint64_t> keys;
    std::vector<std::uint64_t> scratch;

    Candidate(int L_, int k_, double w_, std::size_t reserve)
        : L(L_), k(k_), w(w_), lsh(2, L_, k_, w_, 42) {
        keys.reserve(reserve);
        scratch.reserve(static_cast<std::size_t>(L));
    }
};

static std::vector<std::array<double, 3>> parse_candidates(const std::string& raw) {
    std::vector<std::array<double, 3>> out;
    std::stringstream all(raw);
    std::string item;
    while (std::getline(all, item, ',')) {
        std::replace(item.begin(), item.end(), '/', ':');
        std::stringstream one(item);
        std::string l, k, w;
        if (!std::getline(one, l, ':') || !std::getline(one, k, ':') ||
            !std::getline(one, w, ':')) {
            throw std::runtime_error("bad candidate: " + item);
        }
        out.push_back({std::stod(l), std::stod(k), std::stod(w)});
    }
    return out;
}

static std::string arg_value(int argc, char** argv, const std::string& prefix,
                             const std::string& fallback) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    }
    return fallback;
}

int main(int argc, char** argv) {
    try {
        const std::string server_path = arg_value(
            argc, argv, "--server-path=", "datasets/gowalla_server.json");
        const std::uint64_t server_size = std::stoull(arg_value(
            argc, argv, "--server-size=", "1073741824"));
        const std::size_t client_size = std::stoull(arg_value(
            argc, argv, "--client-size=", "8192"));
        const std::uint32_t seed = static_cast<std::uint32_t>(std::stoul(arg_value(
            argc, argv, "--dataset-seed=", "1")));
        const std::size_t key_reserve = std::stoull(arg_value(
            argc, argv, "--key-reserve=", "8000000"));
        const std::uint64_t progress_every = std::stoull(arg_value(
            argc, argv, "--progress-every=", "10000000"));
        const std::size_t chunk_size = std::stoull(arg_value(
            argc, argv, "--chunk-size=", "1000000"));
        const double jitter = std::stod(arg_value(
            argc, argv, "--jitter=", "0.002"));
        const std::string candidate_raw = arg_value(
            argc, argv, "--candidates=",
            "3:20:0.05,6:25:0.05,4:25:0.06");

        std::ifstream input(server_path);
        if (!input) throw std::runtime_error("cannot open " + server_path);
        json source;
        input >> source;
        if (!source.is_object() || source.empty()) {
            throw std::runtime_error("Gowalla Server JSON must be a nonempty object");
        }

        std::vector<Point> base;
        base.reserve(source.size());
        for (auto it = source.begin(); it != source.end(); ++it) {
            const auto& value = it.value();
            if (!value.is_array() || value.size() != 2) {
                throw std::runtime_error("Gowalla points must have two coordinates");
            }
            base.push_back({value[0].get<double>(), value[1].get<double>()});
        }
        source = json();
        if (server_size < base.size()) {
            throw std::runtime_error("streaming calibrator expects Server >= Gowalla base");
        }

        std::vector<std::unique_ptr<Candidate>> candidates;
        for (const auto& spec : parse_candidates(candidate_raw)) {
            candidates.push_back(std::make_unique<Candidate>(
                static_cast<int>(spec[0]), static_cast<int>(spec[1]), spec[2], key_reserve));
        }

        std::mt19937 rng(seed);
        std::vector<std::size_t> base_order(base.size());
        for (std::size_t i = 0; i < base_order.size(); ++i) base_order[i] = i;
        std::shuffle(base_order.begin(), base_order.end(), rng);

        // Select query source positions up front. This gives the same uniform
        // source distribution without retaining every generated Server point.
        std::mt19937 query_source_rng(seed ^ 0x9e3779b9U);
        std::uniform_int_distribution<std::uint64_t> pick_position(0, server_size - 1);
        const std::size_t source_count = client_size;
        std::unordered_map<std::uint64_t, std::vector<std::size_t>> wanted;
        wanted.reserve(source_count * 2 + 1);
        for (std::size_t i = 0; i < source_count; ++i) {
            wanted[pick_position(query_source_rng)].push_back(i);
        }
        std::vector<Point> query_sources(source_count);

        std::uniform_real_distribution<double> unit(0.0, 1.0);
        constexpr double pi = 3.14159265358979323846;
        const auto started = Clock::now();
        std::vector<Point> chunk;
        chunk.reserve(chunk_size);
        std::uint64_t generated = 0;

        auto process_chunk = [&]() {
            std::vector<std::thread> workers;
            workers.reserve(candidates.size());
            for (auto& candidate_ptr : candidates) {
                workers.emplace_back([&chunk, candidate = candidate_ptr.get()]() {
                    std::vector<double> point(2);
                    for (const Point& value : chunk) {
                        point[0] = value[0];
                        point[1] = value[1];
                        candidate->lsh.table_keys_into(point, candidate->scratch);
                        candidate->keys.insert(
                            candidate->scratch.begin(), candidate->scratch.end());
                    }
                });
            }
            for (auto& worker : workers) worker.join();
            chunk.clear();
        };

        auto append_point = [&](double x, double y) {
            auto selected = wanted.find(generated);
            if (selected != wanted.end()) {
                for (std::size_t slot : selected->second) query_sources[slot] = {x, y};
            }
            chunk.push_back({x, y});
            ++generated;
            if (chunk.size() == chunk_size) process_chunk();
            if (progress_every && generated % progress_every == 0) {
                const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
                std::cout << "[stream progress] generated=" << generated << "/" << server_size
                          << " percent=" << std::fixed << std::setprecision(2)
                          << (100.0 * static_cast<double>(generated) / server_size)
                          << " seconds=" << std::setprecision(1) << seconds
                          << std::defaultfloat << std::setprecision(9);
                for (const auto& candidate : candidates) {
                    std::cout << " keys_" << candidate->L << "_" << candidate->k << "_"
                              << candidate->w << "=" << candidate->keys.size();
                }
                std::cout << "\n" << std::flush;
            }
        };

        for (std::size_t idx : base_order) {
            append_point(base[idx][0], base[idx][1]);
        }
        const std::uint64_t extra = server_size - generated;
        for (std::uint64_t i = 0; i < extra; ++i) {
            const Point& origin = base[static_cast<std::size_t>(i % base.size())];
            const double angle = unit(rng) * 2.0 * pi;
            const double distance = jitter * std::sqrt(unit(rng));
            append_point(origin[0] + distance * std::sin(angle),
                         origin[1] + distance * std::cos(angle));
        }
        if (!chunk.empty()) process_chunk();

        std::mt19937 client_rng(seed ^ 0x85ebca6bU);
        std::uniform_real_distribution<double> client_unit(0.0, 1.0);
        std::vector<Point> clients;
        std::vector<bool> close;
        clients.reserve(client_size);
        close.reserve(client_size);
        const std::size_t close_count = client_size / 2;
        for (std::size_t i = 0; i < client_size; ++i) {
            const double angle = client_unit(client_rng) * 2.0 * pi;
            const bool is_close = i < close_count;
            const double distance = is_close
                ? 0.005 * std::sqrt(client_unit(client_rng))
                : 1.0 + client_unit(client_rng);
            clients.push_back({
                query_sources[i][0] + distance * std::sin(angle),
                query_sources[i][1] + distance * std::cos(angle)});
            close.push_back(is_close);
        }

        std::cout << "L,k,w,fp,fn,total_error,FP,TN,TP,FN,unique_lsh_keys\n";
        std::vector<double> point(2);
        for (auto& candidate : candidates) {
            std::uint64_t fp = 0, tn = 0, tp = 0, fn = 0;
            for (std::size_t i = 0; i < clients.size(); ++i) {
                point[0] = clients[i][0];
                point[1] = clients[i][1];
                candidate->lsh.table_keys_into(point, candidate->scratch);
                bool reported = false;
                for (std::uint64_t key : candidate->scratch) {
                    if (candidate->keys.find(key) != candidate->keys.end()) {
                        reported = true;
                        break;
                    }
                }
                if (close[i]) (reported ? tp : fn)++;
                else (reported ? fp : tn)++;
            }
            const double fp_rate = static_cast<double>(fp) / (fp + tn);
            const double fn_rate = static_cast<double>(fn) / (tp + fn);
            std::cout << std::defaultfloat << std::setprecision(9)
                      << candidate->L << ',' << candidate->k << ',' << candidate->w << ','
                      << std::setprecision(9) << fp_rate << ',' << fn_rate << ','
                      << (fp_rate + fn_rate) << ',' << fp << ',' << tn << ',' << tp << ','
                      << fn << ',' << candidate->keys.size() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gowalla_stream_calibrate: " << error.what() << '\n';
        return 1;
    }
}
