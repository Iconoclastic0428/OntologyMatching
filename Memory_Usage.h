#ifndef MEMORY_USAGE_H
#define MEMORY_USAGE_H

#include <iostream>
#include <fstream>
#include <string>

void print_memory_usage() {
    std::string line;
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        while (getline(statm, line)) {
            std::cout << "Memory Usage: " << line << std::endl;
        }
        statm.close();
    }
}

#endif