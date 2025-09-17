// main.cpp
// Adds an in-memory API to take terms directly (or term->expansions) and return matches,
// while keeping the original file-driven path intact. Uses Option B: LSH::query(...) is const.

#include "LSH.h"
#include "ReadFile.h"
#include "NGram.h"
#include "Memory_Usage.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tbb/concurrent_unordered_map.h>

using nlohmann::json;

// ---------------------- Shared helpers & types ----------------------

static const std::unordered_set<std::string> kStopWords = {
    "about","all","any","as","but","can","choice","extra","for","free","from","good",
    "i","if","in","inch","into","is","like","more","none","not","of","on","one",
    "optional","other","pieces","plus","possibly","removed","size","such","the","to",
    "up","use","very","weight","with","you","your"
};

static std::vector<std::string> filter_string(const std::string& input) {
    std::istringstream iss(input);
    std::vector<std::string> out;
    std::string token, cleaned;
    while (iss >> token) {
        cleaned.clear();
        for (unsigned char ch : token) {
            if (std::isalpha(ch) || std::isspace(ch)) {
                cleaned.push_back(std::tolower(ch));
            }
        }
        if (cleaned.empty()) continue;
        if (!kStopWords.count(cleaned)) out.push_back(std::move(cleaned));
    }
    return out;
}

static inline bool looks_single_token(const std::string& s) {
    return filter_string(s).size() <= 1;
}

// Re-join filtered tokens into a single normalized string
static std::string join_filtered_tokens(const std::vector<std::string>& toks) {
    std::string norm;
    norm.reserve(64);
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i) norm.push_back(' ');
        norm += toks[i];
    }
    return norm;
}

struct Match {
    std::string id;     // ontology node id
    std::string label;  // ontology node label
    double score;       // optional; 1.0 if not computed
};

struct BuiltOntology {
    LSH lsh;
    int n = 3;
    // key used by LSH is a normalized string (whatever parseJson produced)
    std::unordered_map<std::string, std::pair<std::string, std::string>> index;

    BuiltOntology(const std::string& ontologyPath, int band=25, int hash_funcs=100, int n_grams=3)
    : lsh(band, hash_funcs), n(n_grams)
    {
        // parseJson fills index with: normalized_text -> (id,label)
        tbb::concurrent_unordered_map<std::string, std::string> dummy_inverted_index;
        json j = process_json(ontologyPath);
        std::vector<std::string> ontos = parseJson(j, index, dummy_inverted_index);

        const std::string bin_filename = get_base_filename(ontologyPath) + ".bin";
        if (file_exists(bin_filename)) {
            lsh.load_from_disk(bin_filename);
        } else {
            for (const auto& s : ontos) {
                lsh.insert(text_to_ngrams(s, n), s);  // index with character n-grams
            }
            lsh.save_to_disk(bin_filename);
        }
    }
};

// ---------------------- In-memory matching APIs ----------------------

std::unordered_map<std::string, std::vector<Match>>
match_terms_in_memory(BuiltOntology& BO,
                      const std::vector<std::string>& terms,
                      double thresh_single = 0.90,
                      double thresh_multi  = 0.50)
{
    const size_t max_threads = std::max<size_t>(1, std::thread::hardware_concurrency());
    std::unordered_map<std::string, std::vector<Match>> out;
    std::mutex out_mx;

    auto worker = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const std::string& term = terms[i];
            auto filtered = filter_string(term);
            if (filtered.empty()) continue;

            // Query must use the SAME n-gram scheme as indexing (character n-grams)
            const std::string norm = join_filtered_tokens(filtered);
            const double thr = looks_single_token(term) ? thresh_single : thresh_multi;
            auto grams = text_to_ngrams(norm, BO.n);
            auto cand  = BO.lsh.query(grams, thr);  // vector<string> normalized ontology strings (unordered_set)

            std::vector<Match> matches;
            matches.reserve(cand.size());
            for (const auto& v : cand) {
                auto it = BO.index.find(v);
                if (it != BO.index.end()) {
                    matches.push_back(Match{it->second.first, it->second.second, 1.0});
                }
            }
            if (!matches.empty()) {
                std::lock_guard<std::mutex> lk(out_mx);
                out[term] = std::move(matches);
            }
        }
    };

    const size_t N = terms.size();
    const size_t chunk = (N + max_threads - 1) / max_threads;
    std::vector<std::thread> threads;
    threads.reserve(max_threads);
    for (size_t t = 0; t < max_threads; ++t) {
        const size_t b = t * chunk, e = std::min(b + chunk, N);
        if (b < e) threads.emplace_back(worker, b, e);
    }
    for (auto& th : threads) th.join();
    return out;
}

std::unordered_map<std::string, std::vector<Match>>
match_terms_with_expansions_in_memory(BuiltOntology& BO,
    const std::unordered_map<std::string, std::vector<std::string>>& term_to_expansions,
    double thresh_single = 0.90, double thresh_multi = 0.50)
{
    const size_t max_threads = std::max<size_t>(1, std::thread::hardware_concurrency());

    // term -> set(normalized ontology string). We emulate a set via concurrent_unordered_map<string,char>.
    tbb::concurrent_unordered_map<std::string,
        tbb::concurrent_unordered_map<std::string, char>> acc;

    // Flatten work: (owner term, variant string)
    std::vector<std::pair<std::string, std::string>> work;
    work.reserve(term_to_expansions.size() * 4);
    for (const auto& kv : term_to_expansions) {
        const auto& owner = kv.first;
        for (const auto& exp : kv.second) {
            work.emplace_back(owner, exp);
        }
    }

    auto worker = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const auto& owner   = work[i].first;
            const auto& variant = work[i].second;

            auto filtered = filter_string(variant);
            if (filtered.empty()) continue;

            const std::string norm = join_filtered_tokens(filtered);
            const double thr = looks_single_token(variant) ? thresh_single : thresh_multi;
            auto grams = text_to_ngrams(norm, BO.n);
            auto cand  = BO.lsh.query(grams, thr);

            auto& dst = acc[owner];
            for (const auto& v : cand) dst.emplace(v, 1);
        }
    };

    const size_t N = work.size();
    const size_t chunk = (N + max_threads - 1) / max_threads;
    std::vector<std::thread> threads;
    threads.reserve(max_threads);
    for (size_t t = 0; t < max_threads; ++t) {
        const size_t b = t * chunk, e = std::min(b + chunk, N);
        if (b < e) threads.emplace_back(worker, b, e);
    }
    for (auto& th : threads) th.join();

    // Materialize to final result with id/label
    std::unordered_map<std::string, std::vector<Match>> out;
    out.reserve(acc.size());
    for (auto& kv : acc) {
        std::vector<Match> matches;
        matches.reserve(kv.second.size());
        for (const auto& kv2 : kv.second) {
            const auto& v = kv2.first;
            auto it = BO.index.find(v);
            if (it != BO.index.end()) {
                matches.push_back(Match{it->second.first, it->second.second, 1.0});
            }
        }
        if (!matches.empty()) out.emplace(kv.first, std::move(matches));
    }
    return out;
}

static json matches_to_json(const std::unordered_map<std::string, std::vector<Match>>& m) {
    json j;
    for (const auto& [term, vec] : m) {
        json arr = json::array();
        for (const auto& mm : vec) {
            arr.push_back({{"id", mm.id}, {"label", mm.label}, {"score", mm.score}});
        }
        j[term] = std::move(arr);
    }
    return j;
}

// ---------------------- Original file-driven matching (preserved) ----------------------

void match_file_mode(const std::string& ontologyPath,
                     const std::string& ingredientPath,
                     const std::string& outputPath,
                     int hash_funcs = 100, int band = 25, int n = 3)
{
    auto t0 = std::chrono::high_resolution_clock::now();

    // Build ontology index + LSH
    BuiltOntology BO(ontologyPath, band, hash_funcs, n);

    // Load candidate rows: processCSV returns map key -> vector<string>
    // where vector[0] is the main string, vector[1..] are expansions / possibles
    std::unordered_map<std::string, std::vector<std::string>> csv = processCSV(ingredientPath, /*minCols*/1);

    // Build an "expansions" map keyed by candidate key.
    std::unordered_map<std::string, std::vector<std::string>> expansions;
    expansions.reserve(csv.size());
    for (auto& [key, vec] : csv) {
        if (vec.empty()) continue;
        std::vector<std::string> ex;
        ex.reserve(std::max<size_t>(1, vec.size()));
        // include the primary string itself plus any extra columns as variants
        ex.push_back(vec[0]);
        if (vec.size() > 1) ex.insert(ex.end(), vec.begin() + 1, vec.end());
        expansions.emplace(key, std::move(ex));
    }

    // Run in-memory matching over all variants and union back per key
    auto grouped = match_terms_with_expansions_in_memory(BO, expansions, /*thresh_single=*/0.90, /*thresh_multi=*/0.50);

    // Write text output to match your previous format
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "Failed to open " << outputPath << "\n";
        return;
    }
    for (auto& [key, matches] : grouped) {
        out << key << "\n";
        for (auto& m : matches) {
            out << "(" << m.id << " " << m.label << "), ";
        }
        out << "\n";
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(t1 - t0).count();
    std::cerr << "Finished file mode in " << secs << "s\n";
}

// ---------------------- JSON (stdin/stdout) in-memory mode ----------------------

static void usage() {
    std::cerr <<
        "Usage:\n"
        "  A) File mode (original):\n"
        "     ./EntityMatching <ontology.json> <candidates.csv> <output.txt>\n"
        "  B) JSON mode (new, stdin->stdout):\n"
        "     ./EntityMatching --json <ontology.json> < options.json > results.json\n\n"
        "  options.json schema (one of):\n"
        "    {\"terms\": [\"t1\",\"t2\"], \"n\":3, \"band\":25, \"hash_funcs\":100,\n"
        "     \"thresh_single\":0.9, \"thresh_multi\":0.5}\n\n"
        "    {\"expansions\": {\"key\":[\"v1\",\"v2\"]},\n"
        "     \"n\":3, \"band\":25, \"hash_funcs\":100, \"thresh_single\":0.9, \"thresh_multi\":0.5}\n";
}

int main(int argc, char** argv) {
    // Mode B: JSON over stdin/stdout
    if (argc >= 3 && std::string(argv[1]) == "--json") {
        const std::string ontologyPath = argv[2];

        // Read entire stdin into a string
        std::istreambuf_iterator<char> begin(std::cin), end;
        std::string payload(begin, end);
        if (payload.empty()) {
            std::cerr << "Error: no JSON provided on stdin.\n";
            usage();
            return 2;
        }

        json cfg;
        try {
            cfg = json::parse(payload);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << "\n";
            return 2;
        }

        const int n          = cfg.value("n", 3);
        const int band       = cfg.value("band", 25);
        const int hash_funcs = cfg.value("hash_funcs", 100);
        const double ts      = cfg.value("thresh_single", 0.90);
        const double tm      = cfg.value("thresh_multi", 0.50);

        BuiltOntology BO(ontologyPath, band, hash_funcs, n);

        json out;
        if (cfg.contains("terms")) {
            std::vector<std::string> terms = cfg["terms"].get<std::vector<std::string>>();
            auto res = match_terms_in_memory(BO, terms, ts, tm);
            out = matches_to_json(res);
        } else if (cfg.contains("expansions")) {
            std::unordered_map<std::string, std::vector<std::string>> expansions;
            for (auto it = cfg["expansions"].begin(); it != cfg["expansions"].end(); ++it) {
                expansions[it.key()] = it.value().get<std::vector<std::string>>();
            }
            auto res = match_terms_with_expansions_in_memory(BO, expansions, ts, tm);
            out = matches_to_json(res);
        } else {
            std::cerr << "JSON must contain either \"terms\" or \"expansions\".\n";
            usage();
            return 2;
        }

        std::cout << out.dump(2) << std::endl;
        return 0;
    }

    // Mode A: original 3-arg file mode
    if (argc == 4) {
        const std::string ontologyPath  = argv[1];
        const std::string candidatesCsv = argv[2];
        const std::string outputPath    = argv[3];
        match_file_mode(ontologyPath, candidatesCsv, outputPath);
        return 0;
    }

    usage();
    return 1;
}
