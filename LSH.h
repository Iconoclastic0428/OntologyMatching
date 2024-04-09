#ifndef LSH_H
#define LSH_H

#include "MinHash.h"
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <iomanip>
#include <unordered_set>

class LSH {
public:
    LSH(redisContext* context, int numBands, int numHashes = 100)
        : context(context), numBands(numBands), bandSize(numHashes / numBands) {
        for (int i = 0; i < numHashes; ++i) {
            hashFuncs.emplace_back(i); // Initialize HashFunc objects with different seeds
        }
    }

    void insert(const std::vector<std::string>& ngrams, const std::string& docID) {
        auto minhashSignature = minhash(ngrams, hashFuncs);

        // Store signature in Redis
        for (size_t i = 0; i < minhashSignature.size(); ++i) {
            redisCommand(context, "HSET signatures:%s %d %lu", docID.c_str(), i, minhashSignature[i]);
        }

        for (int band = 0; band < numBands; ++band) {
            int start = band * bandSize;
            int end = (band + 1) * bandSize;
            std::string bandHash = computeBandHash(minhashSignature, start, end);

            // Store bucket in Redis
            redisCommand(context, "SADD buckets:%d:%s %s", band, bandHash.c_str(), docID.c_str());
        }
    }

    std::unordered_set<std::string> query(const std::vector<std::string>& queryNgrams, double threshold = 0.5) {
        auto querySignature = minhash(queryNgrams, hashFuncs);
        std::unordered_set<std::string> candidateDocs;
        std::unordered_set<std::string> filteredDocs;

        for (int band = 0; band < numBands; ++band) {
            int start = band * bandSize;
            int end = (band + 1) * bandSize;
            std::string bandHash = computeBandHash(querySignature, start, end);

            // Fetch candidate documents from Redis
            redisReply* reply = (redisReply*)redisCommand(context, "SMEMBERS buckets:%d:%s", band, bandHash.c_str());
            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; ++i) {
                    candidateDocs.insert(reply->element[i]->str);
                }
            }
            freeReplyObject(reply);
        }

        for (const auto& docID : candidateDocs) {
            std::vector<unsigned long> docSignature;

            // Fetch document signature from Redis
            redisReply* reply = (redisReply*)redisCommand(context, "HGETALL signatures:%s", docID.c_str());
            if (reply->type == REDIS_REPLY_ARRAY) {
                for (size_t i = 0; i < reply->elements; i += 2) {
                    unsigned long hashValue = std::stoul(reply->element[i + 1]->str);
                    docSignature.push_back(hashValue);
                }
            }
            freeReplyObject(reply);

            double similarity = jaccard_similarity(querySignature, docSignature);
            if (similarity >= threshold) {
                filteredDocs.insert(docID);
            }
        }

        return filteredDocs;
    }

private:
    redisContext* context;
    int numBands;
    int bandSize;
    std::vector<HashFunc> hashFuncs;

    std::string computeBandHash(const std::vector<unsigned long>& signature, int start, int end) {
        std::ostringstream oss;
        for (int i = start; i < end; ++i) {
            oss << std::hex << signature[i];
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