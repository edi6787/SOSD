#pragma once
#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>

using namespace std;


bool search_range(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position);

