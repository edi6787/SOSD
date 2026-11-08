#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include<chrono>

#include"benchmark.h"

double measure_build_time(ifstream& file, vector<Sample>& index, uint64_t count) {

    auto start = chrono::steady_clock::now();

    build_index(file, index, count);

    auto end = chrono::steady_clock::now();

    chrono::duration<double, milli> elapsed = end - start;

    return elapsed.count();
}

double measure_hit_time(ifstream& file, const vector<Sample>& index, uint32_t key, uint64_t count) {

    uint64_t start, end, found_position;

    auto begin = chrono::steady_clock::now();

    bool found = false;

    if (find_range(index, key, count, start, end)) {
        found = search_range(file, key, start, end, found_position);
    }

    auto finish = chrono::steady_clock::now();

    if (!found) {
        return -1.0;
    }

    chrono::duration<double, micro> elapsed = finish - begin;

    return elapsed.count();
}

double measure_miss_time(ifstream& file, const vector<Sample>& index, uint32_t key, uint64_t count) {

    uint64_t start, end, found_position;

    auto begin = chrono::steady_clock::now();

    bool found = false;

    if (find_range(index, key, count, start, end)) {
        found = search_range(file, key, start, end, found_position);
    }

    auto finish = chrono::steady_clock::now();

    if (found) {
        return -1.0;
    }

    chrono::duration<double, micro> elapsed = finish - begin;

    return elapsed.count();
}