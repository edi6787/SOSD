#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include<chrono>

#include "index.h"
#include"search.h"

//key값이 있는 경우
double measure_hit_time(ifstream& file, const vector<Sample>& index, uint32_t key, uint64_t count, uint64_t& found_position);

//key값이 없는 경우
double measure_miss_time(ifstream& file, const vector<Sample>& index, uint32_t key, uint64_t count);

//빌드타임 측정
double measure_build_time(ifstream& file, vector<Sample>& index, uint64_t count, uint64_t sampel_value);



