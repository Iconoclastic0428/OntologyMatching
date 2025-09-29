// RequestIO.h
// Flexible request parser + ontology factory declaration.
// The factory is declared here and defined in main.cpp (after BuiltOntology is defined).

#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>

// Forward declaration; full definition lives in main.cpp (or your own header).
struct BuiltOntology;

// Normalized view of the request, regardless of input shape.
struct RequestConfig {
    // Inputs
    std::vector<std::string> terms;
    std::unordered_map<std::string, std::vector<std::string>> expansions;

    // LSH / n-gram params
    int n = 3;
    int band = 25;
    int hash_funcs = 100;
    double thresh_single = 0.90;
    double thresh_multi  = 0.50;

    // Ontology sources
    std::optional<nlohmann::json> ontology_json;      // inline ontology
    std::optional<std::string>   ontology_file_json;  // path specified inside JSON
};

// Parse stdin payload into RequestConfig.
// Accepts any of:
// - { "terms":[...], ... }
// - { "expansions":{...}, ... }
// - { "ontology":{...}, "terms":[...], ... }
// - { "ontology_file":"...", "expansions":{...}, ... }
// Also tolerates an envelope { "input": { ... } }.
inline bool parse_request_json(const std::string& payload, RequestConfig& out, std::string& err) {
    using nlohmann::json;
    json cfg;
    try {
        cfg = json::parse(payload);
    } catch (const std::exception& e) {
        err = std::string("Error parsing JSON: ") + e.what();
        return false;
    }

    // Optional envelope
    const json* root = &cfg;
    if (cfg.contains("input") && cfg["input"].is_object()) {
        root = &cfg["input"];
    }

    // Params (defaults match main.cpp)
    out.n             = root->value("n", 3);
    out.band          = root->value("band", 25);
    out.hash_funcs    = root->value("hash_funcs", 100);
    out.thresh_single = root->value("thresh_single", 0.90);
    out.thresh_multi  = root->value("thresh_multi", 0.50);

    // Inputs
    if (root->contains("terms") && (*root)["terms"].is_array()) {
        out.terms = (*root)["terms"].get<std::vector<std::string>>();
    }
    if (root->contains("expansions") && (*root)["expansions"].is_object()) {
        for (auto it = (*root)["expansions"].begin(); it != (*root)["expansions"].end(); ++it) {
            out.expansions[it.key()] = it.value().get<std::vector<std::string>>();
        }
    }

    // Ontology sources
    if (root->contains("ontology") && (*root)["ontology"].is_object()) {
        out.ontology_json = (*root)["ontology"];
    }
    if (root->contains("ontology_file") && (*root)["ontology_file"].is_string()) {
        out.ontology_file_json = (*root)["ontology_file"].get<std::string>();
    }

    // Must provide either terms or expansions
    if (out.terms.empty() && out.expansions.empty()) {
        err = "JSON must contain either \"terms\" or \"expansions\".";
        return false;
    }
    return true;
}

// Factory DECLARATION only. Definition is in main.cpp after BuiltOntology is defined.
std::unique_ptr<BuiltOntology> build_ontology_for_request(
    const RequestConfig& rc,
    const std::string& ontologyPathArg // argv[1], used as fallback
);
