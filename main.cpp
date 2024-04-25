#include "LSH_Wrapper.h"
#include "LSH.h"
#include "ReadFile.h"
#include "NGram.h"
#include "Memory_Usage.h"
#include <chrono>
#include <unordered_set>
#include <future>
#include <tbb/concurrent_unordered_map.h>

std::unordered_set<std::string> word_set = {"about", "all", "any", "as", "bag", "bell", "bottle", "box", "but", "can", "cans",
                                            "choice", "coarsely", "cubes", "cut", "dry", "extra", "fine", "finely",
                                            "for", "free", "freshly", "from", "good", "grams", "i", "if", "in", "inch",
                                            "into", "is", "jar", "lbs", "like", "more", "none", "not", "of", "on", "one",
                                            "optional", "other", "ounce", "ounces", "pieces", "pinch", "plain", "plus",
                                            "possibly", "pound", "pounds", "removed", "size", "slices", "stock", "such", "sweet",
                                            "t", "tablespoon", "tablespoons", "taste", "teaspoons", "the", "thick", "thin", "thinly",
                                            "to", "up", "use", "very", "weight", "with", "you", "your"};
std::queue<std::pair<std::string, std::vector<std::string>>> tasks;
tbb::concurrent_unordered_map<std::string, std::string> inverted_index;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> cache;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> cache_single;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> mismatch;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> ingredients_matches;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> inverted_index_multiple;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> inverted_index_single;
std::unordered_map<std::string, std::unordered_set<std::string>> matches;
std::mutex mutex;

std::vector<std::string> filter_string(const std::string& input_string) {
    std::istringstream iss(input_string);
    std::vector<std::string> words;
    std::string word;

    std::string filtered_word;

    while (iss >> word) {
        filtered_word.clear();
        for (char ch : word) {
            if (std::isalpha(ch) || std::isspace(ch)) {
                filtered_word += std::tolower(ch);
            }
        }
        if (filtered_word.empty()) {
            continue;
        }
        if (word_set.find(filtered_word) == word_set.end()) {
            words.push_back(std::move(filtered_word));
            filtered_word = std::string();
        }
    }
    return words;
}

void process_chunk_words(
    const std::unordered_map<std::string, std::string>::iterator& start,
    const std::unordered_map<std::string, std::string>::iterator& end, int thread_id) {

    std::unordered_map<std::string, std::unordered_set<std::string>> local_index_multiple;
    std::unordered_map<std::string, std::unordered_set<std::string>> local_index_single;

    int completed_tasks = 0;

    for (auto it = start; it != end; ++it) {
        auto filtered_string = filter_string(it->second);
        auto words = text_to_ngrams_words(filtered_string, 2);

        for (const auto& w : words) {
            local_index_multiple[w].insert(it->first);
        }

        auto single_words = text_to_ngrams_words(filtered_string, 1);
        for (const auto& w : single_words) {
            local_index_single[w].insert(it->first);
        }
        completed_tasks++;
        if (completed_tasks % 10000 == 0) {
            std::cout << completed_tasks << "th task completed on thread " << thread_id << std::endl;
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [key, value] : local_index_multiple) {
        inverted_index_multiple[key].insert(value.begin(), value.end());
    }
    local_index_multiple.clear();
    for (const auto& [key, value] : local_index_single) {
        inverted_index_single[key].insert(value.begin(), value.end());
    }
    local_index_single.clear();
    std::cout << "Thread " << thread_id << " finished processing " << completed_tasks << " tasks." << std::endl;
}

void process_chunk(int thread_id, const std::vector<std::pair<std::string, std::string>>& tasks,
                   LSH& lsh, int n){
    int completedTasks = 0;
    std::unordered_map<std::string, std::unordered_set<std::string>> local_mismatch;
    std::unordered_map<std::string, std::unordered_set<std::string>> local_ingredients_matches;
    for (int i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        auto key = std::get<0>(task);
        // auto query_ngram = text_to_ngrams_words(key, 2);
        std::unordered_set<std::string> set;
        // for (auto& ngram : query_ngram) {
        //     if (cache.find(ngram) != cache.end()) {
        //         set.insert(cache[ngram].begin(), cache[ngram].end());
        //     }
        //     else {
        //         auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.5);
        //         set.insert(candidates.begin(), candidates.end());
        //         cache[ngram].insert(candidates.begin(), candidates.end());
        //     }
        // }
        auto indicator = std::get<1>(task);
        if (indicator == "single") {
            auto candidates = lsh.query(text_to_ngrams(key, n), 0.9);
            set.insert(candidates.begin(), candidates.end());
        }
        else {
            auto candidates = lsh.query(text_to_ngrams(key, n), 0.5);
            set.insert(candidates.begin(), candidates.end());
        }
        
        // for (auto& match : matches) {
        //     if (set.find(inverted_index[match]) == set.end()) {
        //         local_mismatch[key].insert(inverted_index[match]);
        //     }
        // }

        local_ingredients_matches[key].insert(set.begin(), set.end());

        completedTasks++;
        if (completedTasks % 10000 == 0) {
            std::cout << completedTasks << "th task completed on thread " << thread_id << std::endl;
        }
    }
    // std::lock_guard<std::mutex> lock(mutex);
    // for (auto& pair : local_mismatch) {
    //     mismatch[pair.first].insert(pair.second.begin(), pair.second.end());
    // }
    for (auto& pair : local_ingredients_matches) {
        ingredients_matches[pair.first].insert(pair.second.begin(), pair.second.end());
    }
    std::cout << "Thread " << thread_id << " finished processing " << tasks.size() << " tasks." << std::endl;
}

int main() {
    LSH lsh(25);
    std::string filename = "output.txt";
    std::unordered_map<std::string, std::pair<std::string, std::string>> index;
    int n = 3;
    tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> cache;

    json json = process_json("./foodon.json");
    // std::unordered_map<int, std::vector<std::string>> ingredients = processCSV("./nourish_public_FoodKG.csv", 0);
    std::unordered_map<std::string, std::vector<std::string>> lexMaprIngredients = processCSV("./nourish_public_LexMaOn_FoodKG_Ingredients.csv", 1);
    std::unordered_map<std::string, std::unordered_set<std::string>> possible_matches;
    std::unordered_map<std::string, std::string> ingredients;
    for (auto& [key, value] : lexMaprIngredients) {
        ingredients[key] = value[0];
        possible_matches[key].insert(value.begin() + 1, value.end());
    }
    lexMaprIngredients.clear();
    std::vector<std::string> ontologies = parseJson(json, index, inverted_index);

    const size_t max_concurrent_tasks = 10;
    std::vector<std::thread> threads;

    auto it = ingredients.begin();
    size_t length = ingredients.size() / max_concurrent_tasks;

    for (size_t i = 0; i < max_concurrent_tasks; ++i) {
        auto end = std::next(it, i == max_concurrent_tasks - 1 ? ingredients.size() - length * i : length);
        threads.emplace_back(process_chunk_words, it, end, i);
        it = end;
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Finish creating inverted index of multiple ngram size: " << 
    inverted_index_multiple.size() << ", single ngram size: " << inverted_index_single.size() << std::endl;

    print_memory_usage();

    for (int i = 0; i < ontologies.size(); ++i) {
        lsh.insert(text_to_ngrams(ontologies[i], n), ontologies[i]);
    }

    std::cout << "Finish inserting ontologites\n";
    
    std::vector<std::pair<std::string, std::string>> tasks;
    
    for (auto& [key, value] : inverted_index_multiple) {
        tasks.push_back({key, "multiple"});
    }

    for (auto& [key, value] : inverted_index_single) {
        tasks.push_back({key, "single"});
    }

    size_t chunk_size = (tasks.size() + max_concurrent_tasks - 1) / max_concurrent_tasks;
    std::vector<std::thread> workers;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < max_concurrent_tasks; ++i) {
        size_t start = i * chunk_size;
        size_t end = std::min(start + chunk_size, tasks.size());
        std::vector<std::pair<std::string, std::string>> chunk_tasks(tasks.begin() + start, tasks.begin() + end);

        workers.emplace_back(process_chunk, i, chunk_tasks,
                            std::ref(lsh), n);
    }

    for (auto& worker : workers) {
        worker.join();
    }
    
    auto stop_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop_time - start_time);
    int total_seconds = duration.count();
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;
    
    std::cout << "The total time taken is " << hours << "h" << minutes << "m" << seconds << "s\n";

    std::ofstream outFile("ngrams.txt");

    if (!outFile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return -1;
    }

    for (auto& [key, value] : ingredients) {
        outFile << key << std::endl;
        auto filtered_string = filter_string(value);
        auto words = text_to_ngrams_words(filtered_string, 2);

        for (auto& w : words) {
            outFile << w << std::endl;
        }

        auto word_single = text_to_ngrams_words(filtered_string, 1);
        for (auto& w : word_single) {
            outFile << w << std::endl;
        }

        outFile << "------------------\n";

    }

    // for (auto& [key, value] : ingredients_matches) {
    //     outFile << key << std::endl;
    //     for (auto& v : value) {
    //         outFile << "(" << index[v].first << ", " << index[v].second << ")\n";
    //     }
    //     outFile << "-------------------\n";
    // }

    outFile.close();

    std::cout << "Finish writing matches\n";

    // inverted_index_multiple.insert(inverted_index_single.begin(), inverted_index_multiple.end());

    // inverted_index_single.clear();

    // for (auto& [key, value] : ingredients_matches) {
    //     auto& indexes = inverted_index_multiple[key];

    //     for (auto& match : value) {
    //         for (auto& i : indexes) {
    //             // matches[i].insert(match);
    //             if (possible_matches[i].find(index[match].first) != possible_matches[i].end()) {
    //                 possible_matches[i].erase(index[match].first);
    //             }
    //         }
    //     }
    // }

    // std::cout << "Finish matching\n";

    // std::ofstream mismatchFile("mismatches.txt");

    // if (!mismatchFile.is_open()) {
    //     std::cerr << "Failed to open " << filename << std::endl;
    //     return -1;
    // }

    // for (const auto& mapEntry : possible_matches) {
    //     std::string key = mapEntry.first;
    //     const auto& setValue = mapEntry.second;

    //     mismatchFile << key << std::endl;

    //     for (const auto& setEntry : setValue) {
    //         if (inverted_index.find(setEntry) == inverted_index.end())
    //             mismatchFile << setEntry << std::endl;
    //         else
    //             mismatchFile << "(" << index[inverted_index[setEntry]].first << ", " << index[inverted_index[setEntry]].second << ")" << std::endl;
    //     }

    //     // outFile << "--------matches-----------\n";

    //     // const auto mes = matches[key];

    //     // for (const auto& entry: mes) {
    //     //     outFile << "(" << index[entry].first << ", " << index[entry].second << ")" << std::endl;
    //     // }

    //     mismatchFile << "------------------------------\n";
    // }

    
    // std::string customInput = "Yeast Rolls \'{\"3 1/2 c. all-purpose flour\" \"1/4 c. sugar\" \"1/4 c. butter or 1 stick margarine softened\" \"1 tsp. salt\" \"1 pkg. regular or quick active dry yeast (2 1/4 tsp.)\" \"1/2 c. very warm water (120~ to 130~)\" \"1/2 c. very warm milk (120~ to 130~)\" \"1 large egg\" \"butter or margarine melted (if desired)\"}\'";
    // auto query_ngram = text_to_ngrams_words(filter_string(customInput), 2);
    // std::unordered_set<std::string> set;
    // // for (auto& ngram : query_ngram) {
    // //     auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.4);
    // //     set.merge(candidates);
    // // }
    // auto query_ngram_single = text_to_ngrams_words(filter_string(customInput), 1);
    // for (auto& ngram : query_ngram_single) {
    //     auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.9);
    //     set.merge(candidates);
    // }
    // for (auto& res : set) {
    //     std::cout << "(" << index[res].first << "," << index[res].second << ")" << std::endl;
    // } 
}