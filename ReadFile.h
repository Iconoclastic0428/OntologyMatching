#ifndef READFILE_H
#define READFILE_H

#include <nlohmann/json.hpp>
#include <regex>
#include <fstream>
#include <iostream>
#include "ThreadPool.h"
#include <unordered_set>

using json = nlohmann::json;

json process_json(const std::string& filename) {

    std::ifstream inputFile(filename);
    if (!inputFile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return -1;
    }

    json j;
    inputFile >> j;

    return j;
}

std::vector<std::string> parseJson(json& j, std::unordered_map<std::string, std::pair<std::string, std::string>>& inverted_index) {
    std::vector<std::string> res;
    for (auto& [key, value] : j.items()) {
        if (key.substr(0, 5) == "ENVO_") {
            continue;
        }
        if (!value.empty()) {
            std::string firstValue = value[0];
            res.push_back(firstValue);
            inverted_index[firstValue] = std::make_pair(key, value[1]);
        }
    }
    std::cout << "Finish parsing JSON" << std::endl;
    return res;
}

std::string clean(const std::string& item) {
    // Remove numerical values
    std::string no_numbers = std::regex_replace(item, std::regex("\\d+"), "");

    // Remove any text within parentheses (including the parentheses)
    std::string no_parentheses = std::regex_replace(no_numbers, std::regex("\\(.*?\\)"), "").erase(0,1);

    return no_parentheses;
}

std::unordered_map<int, std::vector<std::string>> processChunk(const std::vector<std::string>& lines) {
    std::unordered_map<int, std::vector<std::string>> localDataMap;

    // Set of words to remove from the ingredients
    std::unordered_set<std::string> word_set = {"about", "all", "any", "as", "bag", "bell", "bottle", "box", "but", "can", "cans",
                                                "choice", "coarsely", "cubes", "cut", "dry", "extra", "fine", "finely",
                                                "for", "free", "freshly", "from", "good", "grams", "i", "if", "in", "inch",
                                                "into", "is", "jar", "lbs", "like", "more", "none", "not", "of", "on", "one",
                                                "optional", "other", "ounce", "ounces", "pieces", "pinch", "plain", "plus",
                                                "possibly", "pound", "pounds", "removed", "size", "slices", "stock", "such", "sweet",
                                                "t", "tablespoon", "tablespoons", "taste", "teaspoons", "the", "thick", "thin", "thinly",
                                                "to", "up", "use", "very", "weight", "with", "you", "your"};

    for (const auto& line : lines) {
        std::istringstream lineStream(line);
        std::string temp, ingredient;
        int recipeID;

        // Skip the rowID
        std::getline(lineStream, temp, ',');

        // Read the recipeID
        std::getline(lineStream, temp, ',');
        bool flag = false;
        for (char const &c : temp) {
            if (!std::isdigit(c)) {
                flag = true;
                break;
            }
        }
        if (flag) {
            continue;
        }
        recipeID = std::stoi(temp);

        std::string ingredientsStr;
        std::getline(lineStream, ingredientsStr);
        std::istringstream ingredientsStream(ingredientsStr);

        while (std::getline(ingredientsStream, ingredient, ',')) {
            // ingredient = clean(ingredient);
            ingredient.erase(std::remove_if(ingredient.begin(), ingredient.end(), ::ispunct), ingredient.end());
            std::transform(ingredient.begin(), ingredient.end(), ingredient.begin(), ::tolower);
            std::istringstream wordStream(ingredient);
            std::string word;
            std::vector<std::string> cleanedIngredients;

            while (wordStream >> word) {
                if (word_set.find(word) == word_set.end() && !word.empty()) {
                    cleanedIngredients.push_back(word);
                }
            }

            // If the cleaned ingredient list is not empty, add it to the map
            if (!cleanedIngredients.empty()) {
                std::string joinedIngredients = std::accumulate(std::next(cleanedIngredients.begin()), cleanedIngredients.end(), cleanedIngredients[0],
                                                    [](const std::string& a, const std::string& b) -> std::string { 
                                                        return a + ' ' + b; 
                                                    });
                localDataMap[recipeID].push_back(joinedIngredients);
            }
        }
    }

    // std::cout << "Finish a chunk" << std::endl;
    return localDataMap;
}

std::unordered_map<int, std::vector<std::string>> processCSV(const std::string& filePath, size_t linesPerChunk = 1000, size_t numThreads = 10) {
    std::ifstream file(filePath);

    std::vector<std::future<std::unordered_map<int, std::vector<std::string>>>> futures;

    ThreadPool pool(numThreads);
    std::vector<std::string> buffer;
    std::string line;

    while (file.good()) {

        for (size_t i = 0; i < linesPerChunk && std::getline(file, line); ++i) {
            buffer.push_back(line);
        }

        if (!buffer.empty()) {
            // std::cout << "Main thread launching a new thread for a chunk" << std::endl;
            futures.push_back(pool.enqueueTask(processChunk, buffer));
            buffer.clear();
        }

    }

    file.close();

    std::cout << "Finished ingredient chunk processing." << std::endl;

    std::unordered_map<int, std::vector<std::string>> globalDataMap;

    // Merge local maps from each thread
    for (auto& fut : futures) {
        auto localMap = fut.get();
        for (const auto& pair : localMap) {
            globalDataMap[pair.first] = pair.second;
        }
    }

    std::cout << "Finished ingredient data map with size = " << globalDataMap.size() << std::endl;
    return globalDataMap;
}

#endif