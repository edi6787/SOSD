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

bool search_range_interpolation(ifstream& file, uint32_t key,
    uint64_t start, uint64_t end, uint64_t& found_position) {

    uint64_t low = start;
    uint64_t high = end - 1;

    uint32_t low_value, high_value;

    while (low <= high) {

        // 양 끝값 읽기
        file.clear();
        file.seekg(8 + low * sizeof(uint32_t), ios::beg);
        file.read(reinterpret_cast<char*>(&low_value), sizeof(low_value));

        file.clear();
        file.seekg(8 + high * sizeof(uint32_t), ios::beg);
        file.read(reinterpret_cast<char*>(&high_value), sizeof(high_value));

        if (!file) return false;

        // 탐색 범위 밖에 있는 경우
        if (key < low_value || key > high_value) {
            return false;
        }

        // 양 끝값이 같은 경우 0으로 나누는 것 방지
        if (low_value == high_value) {
            if (low_value == key) {
                found_position = low;
                return true;
            }
            return false;
        }

        // 보간탐색 위치 계산
        uint64_t pos = low +
            (static_cast<uint64_t>(key - low_value) * (high - low))
            / (high_value - low_value);

        uint32_t value;

        file.clear();
        file.seekg(8 + pos * sizeof(uint32_t), ios::beg);
        file.read(reinterpret_cast<char*>(&value), sizeof(value));

        if (!file) return false;

        if (value == key) {
            found_position = pos;
            return true;
        }
        else if (value < key) {
            low = pos + 1;
        }
        else {
            if (pos == 0) return false;
            high = pos - 1;
        }
    }

    return false;
}