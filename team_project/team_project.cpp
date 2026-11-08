#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>
#include<algorithm>
#include<chrono>

using namespace std;

struct Sample {
    uint32_t key;
    uint32_t position;
};

//샘플 인덱스 생성
void build_index(ifstream& file, vector<Sample>& index, uint64_t count);

//키값이 존재하는 구간을 찾는 함수
//index : 샘플인덱스, key : 검색할 값, start : 검색구간의 시작위치, end : 검색구간의 끝 위치
bool find_range(const vector<Sample>& index, uint32_t key, uint64_t& count, uint64_t& start, uint64_t& end);

//범위 내에서 키값을 찾는 함수
bool search_range(ifstream& file, uint32_t key, uint64_t start, uint64_t end, uint64_t& found_position);

//시간 측정 함수
double measure_hit_time(ifstream& file, const vector<Sample>& index, uint32_t key, uint64_t count);
double measure_build_time(ifstream& file, vector<Sample>& index, uint64_t count);
double measure_miss_time(ifstream& file, const vector<Sample>& index, uint32_t key, uint64_t count);


int main(){

    vector<Sample> dense_index;
    vector<Sample> sparse_index;

    //파일 열기
    ifstream file(
        R"(C:\Users\kimwo\SOSD\data\uniform_dense_200M_uint32)",
        ios::binary
    );

    ifstream file_s(
        R"(C:\Users\kimwo\SOSD\data\uniform_sparse_200M_uint32)",
        ios::binary
    );

    if (!file.is_open() || !file_s.is_open())
    {
        cout << "파일 열기 실패\n";
        return 1;
    }

    cout << "파일 열기 성공\n";

    uint64_t count;
    uint64_t count_s;

    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    file_s.read(reinterpret_cast<char*>(&count_s), sizeof(count_s));

    cout << "dense 데이터 개수: " << count << '\n';
    cout << "sparse 데이터 개수: " << count << '\n';

    build_index(file, dense_index, count);
    build_index(file_s, sparse_index, count_s);

    cout << "dense 샘플 인덱스 개수: " << dense_index.size() << '\n';
    cout << "sparse 샘플 인덱스 개수: " << sparse_index.size() << '\n';

    // 일부 샘플 확인
    for (size_t i = 0; i < 5 && i < dense_index.size(); i++) {
        cout << "key: " << dense_index[i].key
            << ", position: " << dense_index[i].position << '\n';
    }
    for (size_t i = 0; i < 5 && i < sparse_index.size(); i++) {
        cout << "key: " << sparse_index[i].key
            << ", position: " << sparse_index[i].position << '\n';
    }

    uint64_t start, start_s;
    uint64_t end, end_s;
    uint32_t key = dense_index[100].key;
    uint64_t found_position, found_position_s;
    uint32_t key_s = sparse_index[100].key;

    if (find_range(dense_index, key, count, start, end)) {
        if (search_range(file, key, start, end, found_position)) {
            cout << "[dense]검색 성공\n";
            cout << "start: " << start << '\n';
            cout << "end: " << end << '\n';
            cout << "key: " << key << '\n';
            cout << "found_position: " << found_position << '\n';
        }
        else {
            cout << "구간 내에서 키를 찾지 못했습니다.\n";
        }
    }
    else {
        cout << "검색구간 찾기 실패\n";
    }

    //sparse
    if (find_range(sparse_index, key_s, count_s, start_s, end_s)) {
        if (search_range(file_s, key_s, start_s, end_s, found_position_s)) {
            cout << "[sparse]검색 성공\n";
            cout << "start: " << start_s << '\n';
            cout << "end: " << end_s << '\n';
            cout << "key: " << key_s << '\n';
            cout << "found_position: " << found_position_s << '\n';
        }
        else {
            cout << "구간 내에서 키를 찾지 못했습니다.\n";
        }
    }
    else {
        cout << "검색구간 찾기 실패\n";
    }


    double build_time = measure_build_time(file, dense_index, count);

    double hit_time = measure_hit_time(file, dense_index, dense_index[100].key, count);

    double miss_time = measure_miss_time(file, dense_index, 1000000000, count);

    cout << "Build Time: "
        << build_time
        << " ms\n";

    cout << "Hit Time: "
        << hit_time
        << " us\n";

    cout << "Miss Time: "
        << miss_time
        << " us\n";

    ofstream result("result.txt", ios::app);

    if (!result) {
        cout << "결과 파일을 열 수 없습니다.\n";
        return 1;
    }

    result << "Build Time: " << build_time << " ms\n";
    result << "Hit Time: " << hit_time << " us\n";
    result << "Miss Time: " << miss_time << " us\n";

    file.close();
    result.close();
   
    return 0;
}


void build_index(ifstream& file, vector<Sample>& index, uint64_t count) {

    if (!file) {
        cerr << "파일 읽기 실패\n";
        return;
    }

    index.clear();

    for (uint64_t i = 0; i < count; i += 1000) {

        file.seekg(8 + static_cast<uint64_t>(i) * 4, ios::beg);

        uint32_t value;

        file.read(
            reinterpret_cast<char*>(&value),
            sizeof(value)
        );

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