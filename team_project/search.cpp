#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>

#include"search.h"


bool search_range(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position) {

    uint64_t length = end - start;

    vector<uint32_t> data(length);

    file.clear();
    file.seekg(8 + start * sizeof(uint32_t), ios::beg);

    file.read(reinterpret_cast<char*>(data.data()), length * sizeof(uint32_t));

    if (!file) {
        return false;
    }

    auto it = find(data.begin(), data.end(), key);

    if (it == data.end()) {
        return false;
    }

    found_position = start + (it - data.begin());

    return true;
}
