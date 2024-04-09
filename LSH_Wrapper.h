// LSHWrapper.h
#ifndef LSH_WRAPPER_H
#define LSH_WRAPPER_H

#include "LSH.h"
#include <iostream>

class LSHWrapper {
public:
    LSHWrapper(const char* redisHost, int redisPort, int numBands, int numHashes = 100)
        : numBands(numBands), numHashes(numHashes) {
        connectRedis(redisHost, redisPort);
    }

    ~LSHWrapper() {
        if (context) {
            redisFree(context);
        }
    }

    LSH* lsh = nullptr;

private:
    redisContext* context = nullptr;
    int numBands;
    int numHashes;

    void connectRedis(const char* host, int port) {
        context = redisConnect(host, port);
        if (!context || context->err) {
            if (context) {
                std::cerr << "Redis connection error: " << context->errstr << std::endl;
            } else {
                std::cerr << "Cannot allocate Redis context" << std::endl;
            }
            throw std::runtime_error("Failed to connect to Redis");
        }

        lsh = new LSH(context, numBands, numHashes);
    }

    // Prevent copying and assignment
    LSHWrapper(const LSHWrapper&) = delete;
    LSHWrapper& operator=(const LSHWrapper&) = delete;
};

#endif
