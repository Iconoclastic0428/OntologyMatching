#ifndef UTIL_H
#define UTIL_H

#include<string>
#include <iostream>
#include <fstream>

bool file_exists(const std::string& filename) {
    std::ifstream infile(filename);
    return infile.good();
}

// Helper function to get base filename without extension
std::string get_base_filename(const std::string& filename) {
    size_t dotPos = filename.find_last_of(".");
    if (dotPos == std::string::npos) {
        return filename;
    } else {
        return filename.substr(0, dotPos);
    }
}

#endif