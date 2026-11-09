#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>

#include "index.h"

const uint64_t sample_value = 1000;

void build_index(ifstream& file, vector<Sample>& index, uint64_t count, uint64_t sample_value) {

    if (!file) {
        cerr << "파일 읽기 실패\n";
        return;
    }

    index.clear();

    for (uint64_t i = 0; i < count; i += sample_value) {

        file.seekg(8 + static_cast<uint64_t>(i) * 4, ios::beg);

        uint32_t value;

        file.read(reinterpret_cast<char*>(&value), sizeof(value));

        index.push_back({ value, static_cast<uint32_t>(i) });
    }
}

bool compare(uint32_t value, const Sample& sample) {
    return value < sample.key;
}

bool find_range(const vector<Sample>& index, uint32_t key, uint64_t& count, uint64_t& start, uint64_t& end) {

    if (index.empty()) {
        return false;
    }

    auto it = upper_bound(index.begin(), index.end(), key, compare);

    if (it == index.begin()) {
        return false;
    }

    --it;
    start = it->position;

    ++it;

    if (it == index.end()) {
        end = count;
    }
    else {
        end = it->position;
    }

    return true;
}
