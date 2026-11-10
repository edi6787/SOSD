#pragma once
#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>

using namespace std;

//선형탐색
bool search_range(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position);

//이진탐색
bool search_range_binary(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position);

//버퍼 단위 순차 탐색
bool search_range_buffer(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position);


bool search_range_interpolation(ifstream& file, uint32_t key,
    uint64_t start, uint64_t end, uint64_t& found_position);