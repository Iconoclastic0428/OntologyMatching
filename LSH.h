#ifndef LSH_H
#define LSH_H

#include "MinHash.h"
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <map>
#include <openssl/sha.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>
#include <tbb/spin_mutex.h>

class LSH {
public:
    LSH(int numBands, int numHashes = 100) : numBands(numBands), bandSize(numHashes / numBands), buckets(numBands) {
        for (int i = 0; i < numHashes; ++i) {
            hashFuncs.emplace_back(i); // Initialize HashFunc objects with different seeds
        }
        for (int i = 0; i < numBands; ++i) {
            buckets[i] = tbb::concurrent_unordered_map<std::string, tbb::concurrent_vector<std::string>>();
        }
    }

    void insert(const std::vector<std::string>& ngrams, const std::string& docID) {
        auto minhashSignature = minhash(ngrams, hashFuncs);
        signatures[docID] = minhashSignature;

        for (int band = 0; band < numBands; ++band) {
            int start = band * bandSize;
            int end = (band + 1) * bandSize;
            std::string bandHash = computeBandHash(minhashSignature, start, end);

            buckets[band][bandHash].push_back(docID);
        }
    }

    std::unordered_set<std::string> query(const std::vector<std::string>& queryNgrams, double threshold = 0.4) {
        auto querySignature = minhash(queryNgrams, hashFuncs);
        std::unordered_set<std::string> candidateDocs;
        
        tbb::parallel_for(0, numBands, [&](int band) {
            int start = band * bandSize;
            int end = (band + 1) * bandSize;
            std::string bandHash = computeBandHash(querySignature, start, end);
            
            auto& bandBucket = buckets[band];
            if (bandBucket.find(bandHash) != bandBucket.end()) {
                tbb::spin_mutex::scoped_lock lock;
                for (const auto& docID : bandBucket[bandHash]) {
                    lock.acquire(mutex_for_candidateDocs);
                    candidateDocs.insert(docID);
                    lock.release();
                }
            }
        });

        tbb::concurrent_unordered_map<std::string, bool> filteredDocs;
        
        tbb::parallel_for_each(candidateDocs.begin(), candidateDocs.end(), [&](const std::string& docID) {
            auto& docSignature = signatures.at(docID);
            double similarity = jaccard_similarity(querySignature, docSignature);
            if (similarity >= threshold) {
                filteredDocs[docID] = true;
            }
        });

        std::unordered_set<std::string> result;
        // Convert concurrent map to set
        for (auto& item : filteredDocs) {
            result.insert(item.first);
        }
        return result;
    }

private:
    int numBands;
    int bandSize;
    std::vector<HashFunc> hashFuncs;
    tbb::concurrent_vector<tbb::concurrent_unordered_map<std::string, tbb::concurrent_vector<std::string>>> buckets;
    tbb::concurrent_unordered_map<std::string, std::vector<unsigned long>> signatures;
    tbb::spin_mutex mutex_for_candidateDocs;

    std::string computeBandHash(const std::vector<unsigned long>& signature, int start, int end) {
        std::ostringstream oss;
        for (int i = start; i < end; ++i) {
            oss << signature[i];
        }
        std::string combined = oss.str();

        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);

        std::ostringstream result;
        for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
            result << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }

        return result.str();
    }
};

#endif
