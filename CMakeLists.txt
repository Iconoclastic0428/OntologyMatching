cmake_minimum_required(VERSION 3.22.1)
project(OntologyMatching)

find_package(nlohmann_json REQUIRED)

add_library(OntologyMatching main.cpp)

target_link_libraries(OntologyMatching PRIVATE nlohmann_json::nlohmann_json)