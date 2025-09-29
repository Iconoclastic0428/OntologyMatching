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
#include <memory>

#include <tbb/concurrent_unordered_map.h>

// Flexible request parser (factory declared in header; defined below in this file)
#include "requestIO.h"

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

// ---------------------- Ontology JSON sanitizer (for SNOMED/OBOGraphs) ----------------------

// Convert various JSON shapes to a best-effort string.
static std::string to_string_flexible(const nlohmann::json& v) {
    using nlohmann::json;
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer())  return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float())    return std::to_string(v.get<double>());

    if (v.is_object()) {
        // Common OBOGraphs/SNOMED patterns:
        if (v.contains("label")  && v["label"].is_string())   return v["label"].get<std::string>();
        if (v.contains("@value") && v["@value"].is_string())  return v["@value"].get<std::string>();
        if (v.contains("value")  && v["value"].is_string())   return v["value"].get<std::string>();
        if (v.contains("id")     && v["id"].is_string())      return v["id"].get<std::string>();
        if (v.contains("curie")  && v["curie"].is_string())   return v["curie"].get<std::string>();
        if (v.contains("@id")    && v["@id"].is_string())     return v["@id"].get<std::string>();
        // fallback: dump to a compact string
        return v.dump();
    }

    if (v.is_array()) {
        // Join items defensively
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += "; ";
            out += to_string_flexible(v[i]);
        }
        return out;
    }
    return "";
}

// Walk the OBOGraphs JSON and coerce all string-expected fields into strings.
static void sanitize_ontology_json(nlohmann::json& j) {
    using nlohmann::json;
    if (!j.is_object()) return;
    if (!j.contains("graphs") || !j["graphs"].is_array()) return;

    for (auto& g : j["graphs"]) {
        if (!g.is_object()) continue;

        // ---- Nodes ----
        if (g.contains("nodes") && g["nodes"].is_array()) {
            for (auto& node : g["nodes"]) {
                if (!node.is_object()) continue;

                // id/type/lbl should be strings
                if (node.contains("id")   && !node["id"].is_string())
                    node["id"] = to_string_flexible(node["id"]);
                if (node.contains("type") && !node["type"].is_string())
                    node["type"] = to_string_flexible(node["type"]);
                if (node.contains("lbl")  && !node["lbl"].is_string())
                    node["lbl"] = to_string_flexible(node["lbl"]);

                // meta.basicPropertyValues[*].pred / val should be strings
                if (node.contains("meta") && node["meta"].is_object()) {
                    auto& meta = node["meta"];

                    if (meta.contains("basicPropertyValues") && meta["basicPropertyValues"].is_array()) {
                        for (auto& bpv : meta["basicPropertyValues"]) {
                            if (!bpv.is_object()) continue;
                            if (bpv.contains("pred") && !bpv["pred"].is_string())
                                bpv["pred"] = to_string_flexible(bpv["pred"]);
                            if (bpv.contains("val")  && !bpv["val"].is_string())
                                bpv["val"]  = to_string_flexible(bpv["val"]);
                        }
                    }

                    // Optional: xrefs/annotations sometimes carry string-like objects too
                    if (meta.contains("xrefs") && meta["xrefs"].is_array()) {
                        for (auto& xr : meta["xrefs"]) {
                            if (!xr.is_object()) continue;
                            if (xr.contains("val")  && !xr["val"].is_string())
                                xr["val"]  = to_string_flexible(xr["val"]);
                            if (xr.contains("pred") && !xr["pred"].is_string())
                                xr["pred"] = to_string_flexible(xr["pred"]);
                        }
                    }
                }
            }
        }

        // ---- Edges ----
        if (g.contains("edges") && g["edges"].is_array()) {
            for (auto& e : g["edges"]) {
                if (!e.is_object()) continue;

                // sub/pred/obj should be strings
                if (e.contains("sub")  && !e["sub"].is_string())
                    e["sub"]  = to_string_flexible(e["sub"]);
                if (e.contains("pred") && !e["pred"].is_string())
                    e["pred"] = to_string_flexible(e["pred"]);
                if (e.contains("obj")  && !e["obj"].is_string())
                    e["obj"]  = to_string_flexible(e["obj"]);

                // meta.basicPropertyValues[*].pred / val should be strings
                if (e.contains("meta") && e["meta"].is_object()) {
                    auto& meta = e["meta"];
                    if (meta.contains("basicPropertyValues") && meta["basicPropertyValues"].is_array()) {
                        for (auto& bpv : meta["basicPropertyValues"]) {
                            if (!bpv.is_object()) continue;
                            if (bpv.contains("pred") && !bpv["pred"].is_string())
                                bpv["pred"] = to_string_flexible(bpv["pred"]);
                            if (bpv.contains("val")  && !bpv["val"].is_string())
                                bpv["val"]  = to_string_flexible(bpv["val"]);
                        }
                    }
                }
            }
        }
    }
}

// ---------------------- Robust OBOGraphs fallback parser ----------------------
// If parseJson(...) still throws after sanitization, use this to fill `index` and `ontos`.

static std::vector<std::string> parse_obographs_robust(
    const json& j,
    std::unordered_map<std::string, std::pair<std::string, std::string>>& index)
{
    std::vector<std::string> out;
    if (!j.is_object()) return out;
    if (!j.contains("graphs") || !j["graphs"].is_array()) return out;

    auto add_label = [&](const std::string& id, const std::string& canonical_label,
                         const std::string& label_variant)
    {
        // Normalize using the same tokenization pipeline as queries
        auto toks = filter_string(label_variant);
        if (toks.empty()) return;
        std::string norm = join_filtered_tokens(toks);
        // Insert into index if not present; map to (id, canonical_label)
        if (index.emplace(norm, std::make_pair(id, canonical_label)).second) {
            out.push_back(norm);
        }
    };

    for (const auto& g : j["graphs"]) {
        if (!g.is_object()) continue;
        if (!g.contains("nodes") || !g["nodes"].is_array()) continue;

        for (const auto& node : g["nodes"]) {
            if (!node.is_object()) continue;

            std::string id  = node.contains("id")  ? to_string_flexible(node["id"])  : "";
            std::string lbl = node.contains("lbl") ? to_string_flexible(node["lbl"]) : "";

            if (id.empty() && lbl.empty()) continue;
            if (lbl.empty()) lbl = id; // fallback canonical label

            // Add canonical label
            add_label(id, lbl, lbl);

            // Add SKOS pref/alt labels from meta.basicPropertyValues
            if (node.contains("meta") && node["meta"].is_object()) {
                const auto& meta = node["meta"];
                if (meta.contains("basicPropertyValues") && meta["basicPropertyValues"].is_array()) {
                    for (const auto& bpv : meta["basicPropertyValues"]) {
                        if (!bpv.is_object()) continue;
                        std::string pred = bpv.contains("pred") ? to_string_flexible(bpv["pred"]) : "";
                        std::string val  = bpv.contains("val")  ? to_string_flexible(bpv["val"])  : "";
                        if (val.empty()) continue;

                        // Look for typical SKOS label preds
                        if (pred.find("skos/core#prefLabel") != std::string::npos ||
                            pred.find("skos/core#altLabel")  != std::string::npos ||
                            pred.find("prefLabel")            != std::string::npos ||
                            pred.find("altLabel")             != std::string::npos) {
                            add_label(id, lbl, val);
                        }
                    }
                }
            }
        }
    }
    return out;
}

// ---------------------- BuiltOntology ----------------------

struct BuiltOntology {
    LSH lsh;
    int n = 3;
    // key used by LSH is a normalized string (whatever parseJson produced)
    std::unordered_map<std::string, std::pair<std::string, std::string>> index;

    // ===== New: build from in-memory JSON (no disk IO) =====
    BuiltOntology(const json& ontology_json, int band=25, int hash_funcs=100, int n_grams=3)
    : lsh(band, hash_funcs), n(n_grams)
    {
        json j = ontology_json;
        sanitize_ontology_json(j);  // sanitize before parse
        tbb::concurrent_unordered_map<std::string, std::string> dummy_inverted_index;

        std::vector<std::string> ontos;
        try {
            ontos = parseJson(j, index, dummy_inverted_index);
        } catch (const std::exception& e) {
            std::cerr << "[warn] parseJson failed (" << e.what() << "), falling back to robust parser.\n";
            index.clear();
            ontos = parse_obographs_robust(j, index);
        }

        for (const auto& s : ontos) {
            lsh.insert(text_to_ngrams(s, n), s);  // index with character n-grams
        }
    }

    // ===== Original: build from file path + on-disk cache =====
    BuiltOntology(const std::string& ontologyPath, int band=25, int hash_funcs=100, int n_grams=3)
    : lsh(band, hash_funcs), n(n_grams)
    {
        tbb::concurrent_unordered_map<std::string, std::string> dummy_inverted_index;
        json j = process_json(ontologyPath);
        sanitize_ontology_json(j);  // sanitize before parse

        std::vector<std::string> ontos;
        const std::string bin_filename = get_base_filename(ontologyPath) + ".bin";

        // If a cache exists, we can load LSH and skip (re)indexing text.
        if (file_exists(bin_filename)) {
            // Still need `index` filled for id/label lookup in results:
            try {
                (void)parseJson(j, index, dummy_inverted_index);
            } catch (const std::exception& e) {
                std::cerr << "[warn] parseJson failed while preparing index ("
                          << e.what() << "), using robust parser for index.\n";
                index.clear();
                (void)parse_obographs_robust(j, index);
            }
            lsh.load_from_disk(bin_filename);
            return;
        }

        // No cache; build both index and LSH.
        try {
            ontos = parseJson(j, index, dummy_inverted_index);
        } catch (const std::exception& e) {
            std::cerr << "[warn] parseJson failed (" << e.what() << "), falling back to robust parser.\n";
            index.clear();
            ontos = parse_obographs_robust(j, index);
        }

        for (const auto& s : ontos) {
            lsh.insert(text_to_ngrams(s, n), s);  // index with character n-grams
        }
        lsh.save_to_disk(bin_filename);
    }
};

// ------------- Factory definition (now that BuiltOntology is complete) -------------
// Declared in RequestIO.h; defined here to avoid constructing incomplete type in the header.
std::unique_ptr<BuiltOntology> build_ontology_for_request(
    const RequestConfig& rc,
    const std::string& ontologyPathArg // argv[1], used as fallback
) {
    if (rc.ontology_json.has_value()) {
        return std::unique_ptr<BuiltOntology>(
            new BuiltOntology(*rc.ontology_json, rc.band, rc.hash_funcs, rc.n)
        );
    }
    if (rc.ontology_file_json.has_value() && !rc.ontology_file_json->empty()) {
        return std::unique_ptr<BuiltOntology>(
            new BuiltOntology(*rc.ontology_file_json, rc.band, rc.hash_funcs, rc.n)
        );
    }
    // Fallback to legacy path
    return std::unique_ptr<BuiltOntology>(
        new BuiltOntology(ontologyPathArg, rc.band, rc.hash_funcs, rc.n)
    );
}

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
            auto cand  = BO.lsh.query(grams, thr);  // vector<string> normalized ontology strings

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

// ---------------------- JSON (stdin/stdout) in-memory mode ----------------------

static void usage() {
    std::cerr <<
        "Usage (JSON-in/JSON-out via stdin/stdout):\n"
        "  echo '{\n"
        "    \"ontology\": { /* ontology JSON */ },\n"
        "    \"terms\": [\"t1\",\"t2\"],\n"
        "    \"n\":3, \"band\":25, \"hash_funcs\":100,\n"
        "    \"thresh_single\":0.9, \"thresh_multi\":0.5\n"
        "  }' | ./EntityMatching <ontology.json> > results.json\n\n"
        "Or with expansions:\n"
        "  {\n"
        "    \"ontology\": { /* ontology JSON */ },\n"
        "    \"expansions\": {\"key\":[\"v1\",\"v2\"]},\n"
        "    \"n\":3, \"band\":25, \"hash_funcs\":100,\n"
        "    \"thresh_single\":0.9, \"thresh_multi\":0.5\n"
        "  }\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr <<
          "Usage:\n"
          "  ./EntityMatching <ontology.json> < request.json > results.json\n\n"
          "request.json schema (any of):\n"
          "  {\"terms\":[\"t1\",\"t2\"], \"n\":3, \"band\":25, \"hash_funcs\":100,\n"
          "   \"thresh_single\":0.9, \"thresh_multi\":0.5}\n\n"
          "  {\"expansions\": {\"key\":[\"v1\",\"v2\"]}, \"n\":3, \"band\":25, \"hash_funcs\":100,\n"
          "   \"thresh_single\":0.9, \"thresh_multi\":0.5}\n\n"
          "  {\"ontology\": { /* inline ontology JSON */ }, \"terms\": [...], ...}\n"
          "  {\"ontology_file\": \"/path/to/ontology.json\", \"expansions\": {...}, ...}\n";
        return 1;
    }

    const std::string ontologyPathArg = argv[1];

    // Read all of stdin into a string
    std::istreambuf_iterator<char> begin(std::cin), end;
    std::string payload(begin, end);
    if (payload.empty()) {
        std::cerr << "Error: no JSON provided on stdin.\n";
        return 2;
    }

    // Parse flexible request JSON
    RequestConfig rc;
    std::string perr;
    if (!parse_request_json(payload, rc, perr)) {
        std::cerr << perr << "\n";
        return 2;
    }

    // Build ontology using (priority): inline JSON, JSON's ontology_file, or argv[1].
    std::unique_ptr<BuiltOntology> BO_ptr = build_ontology_for_request(rc, ontologyPathArg);
    BuiltOntology& BO = *BO_ptr;

    // Execute matching
    nlohmann::json out;
    if (!rc.terms.empty()) {
        auto res = match_terms_in_memory(BO, rc.terms, rc.thresh_single, rc.thresh_multi);
        out = matches_to_json(res);
    } else {
        auto res = match_terms_with_expansions_in_memory(BO, rc.expansions, rc.thresh_single, rc.thresh_multi);
        out = matches_to_json(res);
    }

    std::cout << out.dump(2) << std::endl;
    return 0;
}
