#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>
#include<algorithm>
#include<chrono>

#include"index.h"
#include"search.h"
#include"benchmark.h"

using namespace std;

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