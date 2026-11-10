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

//메모리 제한
bool set_memory_limit(size_t memory_limit);

//키 추출 함수
void extract_keys(ifstream& file, uint32_t keys[], uint64_t positions[], int size);

int main(){

    // 512MB 제한
    if (!set_memory_limit(512ULL * 1024 * 1024)) {
        cout << "메모리 제한 설정 실패\n";
        return 1;
    }

    vector<Sample> dense_index;
    vector<Sample> sparse_index;

    ifstream file(
        "data/uniform_dense_200M_uint32",
        ios::binary
    );

    ifstream file_s(
        "data/uniform_sparse_200M_uint32",
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

    //샘플인덱스 간격조절
    const uint64_t sample_value = 1000000;
    const uint64_t sample_value_s = 1000000;

    //key값 설정
    uint32_t key[5];
    uint32_t key_s[5];

    uint64_t positions[5] = { 10000,1000000,10000000,50000000,100000000 };

    //키 값 추출
    extract_keys(file, key, positions, 5);
    extract_keys(file_s, key_s, positions, 5);

    uint64_t found_position, found_position_s;

    //빌드 타임
    double build_time = measure_build_time(file, dense_index, count, sample_value);
    double build_time_s = measure_build_time(file_s, sparse_index, count_s, sample_value_s);


    //result로 출력
    ofstream result("result.txt", ios::app);
    if (!result) {
        cout << "결과 파일을 열 수 없습니다.\n";
        return 1;
    }
 
    for (int i = 0; i < 5; i++) {
        //dense : hit, miss 시간측정
        double hit_time = measure_hit_time(file, dense_index, key[i], count, found_position);
        double miss_time = measure_miss_time(file, dense_index, 1000000000, count);

        //sparse : hit, miss 시간측정
        double hit_time_s = measure_hit_time(file_s, sparse_index, key_s[i], count_s, found_position_s);
        double miss_time_s = measure_miss_time(file_s, sparse_index, 1000000000, count_s);

        cout << "\n" << "[dense]" << "\n";
        print_result(build_time, hit_time, miss_time);
        cout << '\n' << "[sparse]" << "\n";
        print_result(build_time_s, hit_time_s, miss_time_s);

        result << "\n" << "[dense] / sample_value : " << sample_value << ", " << "key : " << key [i]<< '\n';
        result << "Build Time: " << build_time << " ms\n";
        result << "Hit Time: " << hit_time << " ms" << ", " << "found : " << found_position << '\n';
        result << "Miss Time: " << miss_time << " ms\n";

        result << "[sparse] / sample_value : " << sample_value_s << ", " << "key : " << key_s[i] << '\n';
        result << "Build Time: " << build_time_s << " ms\n";
        result << "Hit Time: " << hit_time_s << " ms" << ", " << "found : " << found_position_s << '\n';
        result << "Miss Time: " << miss_time_s << " ms\n";

    }

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
        << " ms\n";

    cout << "Miss Time: "
        << miss_time
        << " ms\n";
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

void extract_keys(ifstream& file, uint32_t keys[],uint64_t positions[], int size) {

    for (int i = 0; i < size; i++) {

        file.clear();

        // 원하는 데이터 위치로 이동
        file.seekg(8 + positions[i] * sizeof(uint32_t), ios::beg);

        // 실제 키값 읽기
        file.read(reinterpret_cast<char*>(&keys[i]), sizeof(uint32_t));
    }
}