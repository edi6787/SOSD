#include <iostream>
#include <fstream>
#include <cstdint>
#include<vector>
#include<algorithm>
#include<chrono>
#include <Windows.h>

#include"index.h"
#include"search.h"
#include"benchmark.h"

using namespace std;

void print_result(double build_time, double hit_time, double miss_time);

bool set_memory_limit(size_t memory_limit);

int main(){

    // 512MB 제한
    if (!set_memory_limit(512ULL * 1024 * 1024)) {
        cout << "메모리 제한 설정 실패\n";
        return 1;
    }

    vector<Sample> dense_index;
    vector<Sample> sparse_index;

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

    const uint64_t sample_value = 1000;
    const uint64_t sample_value_s = 1000;

    build_index(file, dense_index, count, sample_value);
    build_index(file_s, sparse_index, count_s, sample_value_s);

    cout << "dense 샘플 인덱스 개수: " << dense_index.size() << '\n';
    cout << "sparse 샘플 인덱스 개수: " << sparse_index.size() << '\n';

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
    cout << "\n";
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

    //dense
    double build_time = measure_build_time(file, dense_index, count, sample_value);
    double hit_time = measure_hit_time(file, dense_index, dense_index[100].key, count);
    double miss_time = measure_miss_time(file, dense_index, 1000000000, count);

    //sparse
    double build_time_s = measure_build_time(file_s, sparse_index, count_s, sample_value_s);
    double hit_time_s = measure_hit_time(file_s, sparse_index, sparse_index[100].key, count_s);
    double miss_time_s = measure_miss_time(file_s, sparse_index, 1000000000, count_s);

    cout << "\n" << "[dense]" << "\n";
    print_result(build_time, hit_time, miss_time);
    cout << '\n' << "[sparse]" << "\n";
    print_result(build_time_s, hit_time_s, miss_time_s);

    ofstream result("result.txt", ios::app);

    if (!result) {
        cout << "결과 파일을 열 수 없습니다.\n";
        return 1;
    }

    result << "\n" << "[dense] / sample_value : " << sample_value << "\n";
    result << "Build Time: " << build_time << " ms\n";
    result << "Hit Time: " << hit_time << " us\n";
    result << "Miss Time: " << miss_time << " us\n";

    result << "[sparse] / sample_Value : " << sample_value_s << "\n";
    result << "Build Time: " << build_time_s << " ms\n";
    result << "Hit Time: " << hit_time_s << " us\n";
    result << "Miss Time: " << miss_time_s << " us\n";

    file.close();
    result.close();
   
    return 0;
}

void print_result(double build_time, double hit_time, double miss_time) {
    cout << "Build Time: "
        << build_time
        << " ms\n";

    cout << "Hit Time: "
        << hit_time
        << " us\n";

    cout << "Miss Time: "
        << miss_time
        << " us\n";
}

HANDLE hJob = NULL;

bool set_memory_limit(size_t memory_limit) {

    hJob = CreateJobObject(NULL, NULL);

    if (hJob == NULL)
        return false;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};

    info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY;

    info.ProcessMemoryLimit = memory_limit;

    if (!SetInformationJobObject(
        hJob,
        JobObjectExtendedLimitInformation,
        &info,
        sizeof(info))) {
        return false;
    }

    if (!AssignProcessToJobObject(
        hJob,
        GetCurrentProcess())) {
        return false;
    }

    return true;
}