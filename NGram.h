#ifndef NGRAM_H
#define NGRAM_H

#include <string>
#include <vector>
#include <sstream>

std::vector<std::string> split(const std::string &text) {
    std::istringstream iss(text);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::vector<std::string> text_to_ngrams(const std::string& text, int n = 3) {
    std::vector<std::string> ngrams;
    if (text.size() < n) {
        std::cout << text << std::endl;
        ngrams.push_back(text);
        return ngrams;
    }
    for (size_t i = 0; i <= text.size() - n; ++i) {
        ngrams.push_back(text.substr(i, n));
    }
    return ngrams;
}

std::vector<std::string> text_to_ngrams_words(const std::string &text, int n = 3) {
    auto words = split(text);
    std::vector<std::string> ngrams;

    if (words.size() < static_cast<size_t>(n)) {
        std::string allWords;
        for (const auto& word : words) {
            allWords += word + " ";
        }
        // Trim the trailing space and add to ngrams
        if (!allWords.empty()) {
            allWords.pop_back();
            ngrams.push_back(allWords);
        }
        return ngrams;
    }

    for (size_t i = 0; i <= words.size() - n; ++i) {
        std::string ngram;
        for (int j = 0; j < n; ++j) {
            ngram += words[i + j] + " ";
        }
        // Trim the trailing space and add to ngrams
        ngram.pop_back();
        ngrams.push_back(ngram);
    }

    return ngrams;
}

#endif