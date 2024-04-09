#include "LSH_Wrapper.h"
#include "LSH.h"
#include "ReadFile.h"
#include "NGram.h"
#include <chrono>

int main() {
    LSHWrapper lsh("127.0.0.1", 6379, 25);
    std::unordered_map<std::string, std::pair<std::string, std::string>> index;
    int n = 3;

    json json = process_json("./foodon.json");
    std::unordered_map<int, std::vector<std::string>> ingredients = processCSV("./nourish_public_FoodKG.csv");
    std::vector<std::string> ontologies = parseJson(json, index);

    for (int i = 0; i < ontologies.size(); ++i) {
        lsh.lsh->insert(text_to_ngrams(ontologies[i], n), ontologies[i]);
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10; ++i) {
        for (auto& word : ingredients[i]) {
            std::unordered_set<std::string> set;
            auto query_ngram = text_to_ngrams_words(word, 2);
            for (auto& ngram : query_ngram) {
                auto candidates = lsh.lsh->query(text_to_ngrams(ngram, n));
                set.merge(candidates);
            }
            std::cout << word << std::endl;
            for (auto& res : set) {
                std::cout << "(" << index[res].first << "," << index[res].second << ")" << std::endl;
            }

            std::cout << "-------end of element-------" << std::endl;
        }

        std::cout << "---------end of object-----------" << std::endl;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time taken: " << duration.count() << " milliseconds" << std::endl;

    std::string customInput = "mint browny {\"pkg york peppermint {mentha x piperita} patty box brownie mixture}";
    auto query_ngram = text_to_ngrams_words(customInput, 2);
    std::unordered_set<std::string> set;
    for (auto& ngram : query_ngram) {
        auto candidates = lsh.lsh->query(text_to_ngrams(ngram, n));
        set.merge(candidates);
    }
    for (auto& res : set) {
        std::cout << "(" << index[res].first << "," << index[res].second << ")" << std::endl;
    }
}