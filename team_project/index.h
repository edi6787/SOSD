#pragma once
#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>

using namespace std;

struct Sample {
    uint32_t key;
    uint32_t position;
};

//샘플 인덱스 배열 생성
void build_index(ifstream& file, vector<Sample>& index, uint64_t count);

//sample의 key값이 value보다 크면 true 리턴
bool compare(uint32_t value, const Sample& sample);

//키값이 존재하는 구간을 찾는 함수
//index : 샘플인덱스, key : 검색할 값, start : 검색구간의 시작위치, end : 검색구간의 끝 위치
bool find_range(const vector<Sample>& index, uint32_t key, uint64_t& count, uint64_t& start, uint64_t& end);
