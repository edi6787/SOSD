#pragma once
#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>

using namespace std;

//범위에서 키값을 찾는 함수, 위치를 found_position으로 반환
bool search_range(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position);

