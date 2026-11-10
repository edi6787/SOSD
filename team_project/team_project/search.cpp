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

    if (it == data.end() || *it != key) {
        return false;
    }

    found_position = start + (it - data.begin());

    return true;
}

bool search_range_binary(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position) {

    uint64_t low = start;
    uint64_t high = end;

    while (low < high) {

        uint64_t mid = low + (high - low) / 2;

        uint32_t value;

        file.clear();
        file.seekg(8 + mid * sizeof(uint32_t), ios::beg);
        file.read(reinterpret_cast<char*>(&value), sizeof(value));

        if (!file) return false;

        if (value == key) {
            found_position = mid;
            return true;
        }
        else if (value < key) {
            low = mid + 1;
        }
        else {
            high = mid;
        }
    }

    return false;
}

bool search_range_buffer(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position) {

    const uint64_t BUFFER_SIZE = 10000;
    vector<uint32_t> buffer(BUFFER_SIZE);

    uint64_t current = start;

    while (current < end) {

        uint64_t length = min(BUFFER_SIZE, end - current);

        file.clear();
        file.seekg(8 + current * sizeof(uint32_t), ios::beg);

        file.read(
            reinterpret_cast<char*>(buffer.data()),
            length * sizeof(uint32_t)
        );

        if (!file) return false;

        auto it = find(buffer.begin(), buffer.begin() + length, key);

        if (it != buffer.begin() + length) {
            found_position = current + (it - buffer.begin());
            return true;
        }

        current += length;
    }

    return false;
}