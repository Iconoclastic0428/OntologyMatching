#include "LSH_Wrapper.h"
#include "LSH.h"
#include "ReadFile.h"
#include "NGram.h"
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
std::queue<std::pair<int, std::vector<std::string>>> tasks;
tbb::concurrent_unordered_map<std::string, std::string> inverted_index;
tbb::concurrent_unordered_map<std::string, std::unordered_set<std::string>> cache;
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

void process_chunk(int thread_id, const std::vector<std::pair<int, std::vector<std::string>>>& tasks,
                   LSH& lsh, tbb::concurrent_unordered_map<int, std::unordered_set<std::string>>& global_mismatch,
                   tbb::concurrent_unordered_map<int, std::unordered_set<std::string>>& global_ingredients_matches,
                   int n){
    int completedTasks = 0;
    std::unordered_map<int, std::unordered_set<std::string>> mismatch;
    std::unordered_map<int, std::unordered_set<std::string>> ingredients_matches;
    for (int i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        auto key = task.first;
        auto value = task.second;
        auto to_match = value[0];
        std::unordered_set<std::string> matches;
        for (int i = 1; i < value.size(); ++i) {
            matches.insert(value[i]);
        }
        auto filtered_string = filter_string(to_match);
        auto query_ngram = text_to_ngrams_words(filtered_string, 2);
        std::unordered_set<std::string> set;
        for (auto& ngram : query_ngram) {
            if (cache.find(ngram) != cache.end()) {
                set.insert(cache[ngram].begin(), cache[ngram].end());
            }
            else {
                auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.6);
                set.insert(candidates.begin(), candidates.end());
                cache[ngram].insert(candidates.begin(), candidates.end());
            }
        }

        auto query_ngram_single = text_to_ngrams_words(filtered_string, 1);
        for (auto& ngram : query_ngram) {
            if (cache.find(ngram) != cache.end()) {
                set.insert(cache[ngram].begin(), cache[ngram].end());
            }
            else {
                auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.9);
                set.merge(candidates);
                cache[ngram].insert(candidates.begin(), candidates.end());
            }
        }
        
        for (auto& match : matches) {
            if (set.find(inverted_index[match]) == set.end()) {
                mismatch[key].insert(inverted_index[match]);
            }
        }

        ingredients_matches[key].insert(set.begin(), set.end());

        completedTasks++;
        if (completedTasks % 10000 == 0) {
            std::cout << completedTasks << "th task completed on thread " << thread_id << std::endl;
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& pair : mismatch) {
        global_mismatch[pair.first].insert(pair.second.begin(), pair.second.end());
    }
    for (auto& pair : ingredients_matches) {
        global_ingredients_matches[pair.first].insert(pair.second.begin(), pair.second.end());
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
    std::unordered_map<int, std::vector<std::string>> ingredients = processCSV("./nourish_public_FoodKG.csv", 0);
    std::unordered_map<int, std::vector<std::string>> lexMaprIngredients = processCSV("./nourish_public_LexMaOn_FoodKG_Ingredients.csv", 1);
    std::vector<std::string> ontologies = parseJson(json, index, inverted_index);
    const size_t max_concurrent_tasks = 10;

    for (int i = 0; i < ontologies.size(); ++i) {
        lsh.insert(text_to_ngrams(ontologies[i], n), ontologies[i]);
    }

    std::cout << "Finish inserting ontologites\n";
    
    std::unordered_map<int, std::unordered_set<std::string>> mismatch;
    std::unordered_map<int, std::unordered_set<std::string>> ingredients_matches;
    std::vector<std::pair<int, std::vector<std::string>>> tasks;

    
    for (auto& [key, value] : lexMaprIngredients) {
        tasks.push_back({key, value});
    }
    size_t chunk_size = (tasks.size() + max_concurrent_tasks - 1) / max_concurrent_tasks;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    for (size_t i = 0; i < max_concurrent_tasks; ++i) {
        size_t start = i * chunk_size;
        size_t end = std::min(start + chunk_size, tasks.size());
        std::vector<std::pair<int, std::vector<std::string>>> chunk_tasks(tasks.begin() + start, tasks.begin() + end);

        workers.emplace_back(process_chunk, i, chunk_tasks,
                            std::ref(lsh), std::ref(mismatch), 
                            std::ref(ingredients_matches), n);
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
    
    std::ofstream outFile(filename);

    if (!outFile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return -1;
    }

    for (const auto& mapEntry : mismatch) {
        int key = mapEntry.first;
        const auto& setValue = mapEntry.second;

        outFile << key << std::endl;

        for (const auto& setEntry : setValue) {
            outFile << "(" << index[setEntry].first << ", " << index[setEntry].second << ")" << std::endl;
        }

        outFile << "--------matches-----------\n";

        const auto matches = ingredients_matches[key];

        for (const auto& entry: matches) {
            outFile << "(" << index[entry].first << ", " << index[entry].second << ")" << std::endl;
        }

        outFile << "------------------------------\n";
    }

    /*
    std::string customInput = "tsp oil";
    auto query_ngram = text_to_ngrams_words(filter_string(customInput), 2);
    std::unordered_set<std::string> set;
    for (auto& ngram : query_ngram) {
        auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.4);
        set.merge(candidates);
    }
    auto query_ngram_single = text_to_ngrams_words(filter_string(customInput), 1);
    for (auto& ngram : query_ngram_single) {
        auto candidates = lsh.query(text_to_ngrams(ngram, n), 0.9);
        set.merge(candidates);
    }
    for (auto& res : set) {
        std::cout << "(" << index[res].first << "," << index[res].second << ")" << std::endl;
    } */
}