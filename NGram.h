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
        ngrams.push_back(text);
        return ngrams;
    }
    for (size_t i = 0; i <= text.size() - n; ++i) {
        ngrams.push_back(text.substr(i, n));
    }
    return ngrams;
}

std::vector<std::string> text_to_ngrams_words(const std::vector<std::string>& words, int n = 3) {
    std::vector<std::string> ngrams;

    if (words.size() < static_cast<size_t>(n)) {
        std::ostringstream allWordsStream;
        for (size_t i = 0; i < words.size(); ++i) {
            if (i > 0) allWordsStream << ' ';
            allWordsStream << words[i];
        }
        ngrams.push_back(allWordsStream.str());
        return ngrams;
    }

    ngrams.reserve(words.size() - n + 1);

    for (size_t i = 0; i <= words.size() - n; ++i) {
        std::ostringstream ngramStream;
        for (int j = 0; j < n; ++j) {
            if (j > 0) ngramStream << ' ';
            ngramStream << words[i + j];
        }
        ngrams.push_back(ngramStream.str());
    }

    return ngrams;
}

#endif