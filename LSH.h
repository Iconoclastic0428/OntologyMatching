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
#include <fstream>
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

    void save_to_disk(const std::string& filename) const {
        std::ofstream outFile(filename, std::ios::binary);

        if (!outFile.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return;
        }

        // Serialize number of bands and band size
        outFile.write(reinterpret_cast<const char*>(&numBands), sizeof(numBands));
        outFile.write(reinterpret_cast<const char*>(&bandSize), sizeof(bandSize));

        // Serialize buckets
        for (int i = 0; i < numBands; ++i) {
            auto& bandBucket = buckets[i];
            size_t bucketSize = bandBucket.size();
            outFile.write(reinterpret_cast<const char*>(&bucketSize), sizeof(bucketSize));
            
            for (const auto& [key, value] : bandBucket) {
                size_t keySize = key.size();
                outFile.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
                outFile.write(key.c_str(), keySize);

                size_t valueSize = value.size();
                outFile.write(reinterpret_cast<const char*>(&valueSize), sizeof(valueSize));
                for (const auto& docID : value) {
                    size_t docIDSize = docID.size();
                    outFile.write(reinterpret_cast<const char*>(&docIDSize), sizeof(docIDSize));
                    outFile.write(docID.c_str(), docIDSize);
                }
            }
        }

        // Serialize signatures
        size_t sigSize = signatures.size();
        outFile.write(reinterpret_cast<const char*>(&sigSize), sizeof(sigSize));

        for (const auto& [docID, signature] : signatures) {
            size_t docIDSize = docID.size();
            outFile.write(reinterpret_cast<const char*>(&docIDSize), sizeof(docIDSize));
            outFile.write(docID.c_str(), docIDSize);

            size_t sigVecSize = signature.size();
            outFile.write(reinterpret_cast<const char*>(&sigVecSize), sizeof(sigVecSize));
            outFile.write(reinterpret_cast<const char*>(signature.data()), sigVecSize * sizeof(unsigned long));
        }

        outFile.close();
    }

    // Load the LSH data from a file
    void load_from_disk(const std::string& filename) {
        std::ifstream inFile(filename, std::ios::binary);

        if (!inFile.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return;
        }

        // Deserialize number of bands and band size
        inFile.read(reinterpret_cast<char*>(&numBands), sizeof(numBands));
        inFile.read(reinterpret_cast<char*>(&bandSize), sizeof(bandSize));

        // Deserialize buckets
        buckets.resize(numBands);
        for (int i = 0; i < numBands; ++i) {
            size_t bucketSize;
            inFile.read(reinterpret_cast<char*>(&bucketSize), sizeof(bucketSize));

            for (size_t j = 0; j < bucketSize; ++j) {
                size_t keySize;
                inFile.read(reinterpret_cast<char*>(&keySize), sizeof(keySize));
                std::string key(keySize, '\0');
                inFile.read(&key[0], keySize);

                size_t valueSize;
                inFile.read(reinterpret_cast<char*>(&valueSize), sizeof(valueSize));
                tbb::concurrent_vector<std::string> value(valueSize);

                for (size_t k = 0; k < valueSize; ++k) {
                    size_t docIDSize;
                    inFile.read(reinterpret_cast<char*>(&docIDSize), sizeof(docIDSize));
                    std::string docID(docIDSize, '\0');
                    inFile.read(&docID[0], docIDSize);
                    value[k] = docID;
                }

                buckets[i][key] = value;
            }
        }

        // Deserialize signatures
        size_t sigSize;
        inFile.read(reinterpret_cast<char*>(&sigSize), sizeof(sigSize));

        for (size_t i = 0; i < sigSize; ++i) {
            size_t docIDSize;
            inFile.read(reinterpret_cast<char*>(&docIDSize), sizeof(docIDSize));
            std::string docID(docIDSize, '\0');
            inFile.read(&docID[0], docIDSize);

            size_t sigVecSize;
            inFile.read(reinterpret_cast<char*>(&sigVecSize), sizeof(sigVecSize));
            std::vector<unsigned long> signature(sigVecSize);
            inFile.read(reinterpret_cast<char*>(signature.data()), sigVecSize * sizeof(unsigned long));

            signatures[docID] = signature;
        }

        inFile.close();
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
