#ifndef MINHASH_H
#define MINHASH_H

#include <openssl/sha.h>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>
#include <climits>

class HashFunc {
public:
    HashFunc(int seed) : seed(seed) {}

    unsigned long operator()(const std::string& x) const {
        std::ostringstream oss;
        oss << x << seed;
        std::string input = oss.str();

        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

        unsigned long hashValue = 0;
        for (int i = 0; i < SHA_DIGEST_LENGTH / sizeof(unsigned long); ++i) {
            hashValue ^= reinterpret_cast<unsigned long*>(hash)[i];
        }

        return hashValue;
    }

private:
    int seed;
};

std::vector<unsigned long> minhash(const std::vector<std::string>& ngrams, const std::vector<HashFunc>& hashFuncs) {
    std::vector<unsigned long> minhashSignatures(hashFuncs.size(), ULONG_MAX);

    for (const auto& ngram : ngrams) {
        for (size_t i = 0; i < hashFuncs.size(); ++i) {
            unsigned long hashVal = hashFuncs[i](ngram);
            minhashSignatures[i] = std::min(minhashSignatures[i], hashVal);
        }
    }

    return minhashSignatures;
}

double jaccard_similarity(const std::vector<unsigned long>& signature1, const std::vector<unsigned long>& signature2) {
    assert(signature1.size() == signature2.size());

    int matchCount = 0;
    for (size_t i = 0; i < signature1.size(); ++i) {
        if (signature1[i] == signature2[i]) {
            ++matchCount;
        }
    }

    return static_cast<double>(matchCount) / signature1.size();
}

#endif