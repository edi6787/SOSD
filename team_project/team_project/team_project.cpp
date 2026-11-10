// SOSD querying program
//
// 사용법:
//   team_project.exe [데이터파일] [옵션]
//     데이터파일          기본값 data/uniform_dense_200M_uint32
//     --index            보간 탐색 대신 SosdIndex(샘플 인덱스) 사용
//     --step N           --index 사용 시 샘플 간격 (기본 65536)
//     --bench            대화형 검색 대신 벤치마크 실행 (결과는 result.txt에 추가)
//     --queries N        벤치마크 쿼리 수 (hit, miss 각각 N개, 기본 10000)
//     --u32 / --u64      키 타입 지정 (기본: 파일 이름에 "uint64"가 있으면 u64, 아니면 u32)
//
// 대화형 모드: 정수 키를 입력하면 데이터셋에서의 위치(0부터 시작)를 출력한다. q 입력 시 종료.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

#include "benchmark.h"
#include "interp_search.h"
#include "mapped_file.h"
#include "memory_limit.h"
#include "sosd_index.h"

using namespace std;

constexpr size_t MEMORY_LIMIT = 512ULL * 1024 * 1024;  // 512MB

struct Options {
    string path = "data/uniform_dense_200M_uint32";
    uint64_t step = 65536;
    bool bench = false;
    bool use_index = false;  // false: 보간 탐색(빌드 없음, 기본)
    size_t queries = 10000;
    int key_bits = 0;  // 0이면 파일 이름으로 판단
};

static bool parse_args(int argc, char** argv, Options& opt) {
    try {
        for (int i = 1; i < argc; ++i) {
            const string a = argv[i];
            if (a == "--step" && i + 1 < argc) opt.step = stoull(argv[++i]);
            else if (a == "--queries" && i + 1 < argc) opt.queries = stoull(argv[++i]);
            else if (a == "--bench") opt.bench = true;
            else if (a == "--index") opt.use_index = true;
            else if (a == "--u32") opt.key_bits = 32;
            else if (a == "--u64") opt.key_bits = 64;
            else if (!a.empty() && a[0] != '-') opt.path = a;
            else {
                cerr << "알 수 없는 옵션: " << a << '\n';
                return false;
            }
        }
    } catch (const exception&) {
        cerr << "옵션 값이 올바른 정수가 아닙니다.\n";
        return false;
    }
    if (opt.step == 0) opt.step = 1;
    if (opt.queries == 0) opt.queries = 1;
    if (opt.key_bits == 0) opt.key_bits = opt.path.find("uint64") != string::npos ? 64 : 32;
    return true;
}

// 사용자가 입력한 문자열을 Key 범위의 음이 아닌 정수로 변환
template <class Key>
static bool parse_key(const string& s, Key& key) {
    if (s.empty() || s.size() > 20) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    try {
        const unsigned long long v = stoull(s);
        if (v > numeric_limits<Key>::max()) return false;
        key = static_cast<Key>(v);
        return true;
    } catch (const exception&) {
        return false;
    }
}

template <class Key>
static int run(const Options& opt) {
    // ── 빌드: 파일 매핑 + 인덱스 생성 ──
    auto t0 = Clock::now();
    MappedFile file;
    if (!file.open(opt.path)) {
        cerr << file.error() << '\n';
        return 1;
    }
    if (file.size() < 8) {
        cerr << "SOSD 파일 형식이 아닙니다.\n";
        return 1;
    }
    uint64_t n = 0;
    memcpy(&n, file.data(), sizeof(n));
    if (file.size() != 8 + n * sizeof(Key)) {
        cerr << "파일 크기가 맞지 않습니다. 키 타입(--u32/--u64)을 확인하세요.\n";
        return 1;
    }
    if (n == 0) {
        cerr << "데이터가 비어 있습니다.\n";
        return 1;
    }
    const Key* data = reinterpret_cast<const Key*>(file.data() + 8);
    const double map_ms = elapsed_ms(t0, Clock::now());

    cout << "데이터 파일: " << opt.path << '\n';
    cout << "키 개수: " << n << " (uint" << sizeof(Key) * 8 << ")\n";

    if (opt.bench) {
        vector<uint64_t> steps = {256, 1024, 4096, 16384, 65536, 262144};
        if (find(steps.begin(), steps.end(), opt.step) == steps.end()) steps.push_back(opt.step);

        cout << "벤치마크: hit " << opt.queries << "개 + miss " << opt.queries << "개\n\n";
        file.close();  // 벤치마크는 방법마다 파일을 새로 매핑한다
        auto results = run_benchmark<Key>(opt.path, steps, opt.queries, 42);
        print_bench_table(results, opt.queries, cout);

        ofstream out("result.txt", ios::app);
        if (out) {
            out << "\n[" << opt.path << "] n=" << n << ", queries=" << opt.queries
                << " (hit/miss 각각)\n";
            print_bench_table(results, opt.queries, out);
            cout << "\nresult.txt에 결과를 추가했습니다.\n";
        }
        print_memory_usage();
        return 0;
    }

    // ── 대화형 검색 ──
    InterpolationSearch<Key> interp;
    SosdIndex<Key> index;
    auto b0 = Clock::now();
    if (opt.use_index) index.build(data, n, opt.step);
    else interp.build(data, n);
    const double build_ms = map_ms + elapsed_ms(b0, Clock::now());

    if (opt.use_index)
        cout << "SosdIndex 빌드 완료: " << build_ms << " ms (샘플 " << index.sample_count()
             << "개, 인덱스 " << index.memory_bytes() / 1024.0 << " KB)\n";
    else
        cout << "보간 탐색 준비 완료(빌드 = 파일 매핑): " << build_ms << " ms\n";

    double hit_ms = 0, miss_ms = 0;
    size_t hits = 0, misses = 0;
    string line;
    while (true) {
        cout << "\n검색할 키를 입력하세요 (종료: q): ";
        if (!getline(cin, line)) break;
        // 앞뒤 공백 제거
        line.erase(0, line.find_first_not_of(" \t\r"));
        line.erase(line.find_last_not_of(" \t\r") + 1);
        if (line == "q" || line == "Q") break;

        Key key;
        if (!parse_key<Key>(line, key)) {
            cout << "0 이상 " << +numeric_limits<Key>::max() << " 이하의 정수를 입력하세요.\n";
            continue;
        }

        auto s0 = Clock::now();
        const uint64_t pos = opt.use_index ? index.find(key) : interp.find(key);
        auto s1 = Clock::now();
        const double ms = elapsed_ms(s0, s1);

        if (pos != NOT_FOUND) {
            hit_ms += ms;
            ++hits;
            cout << "키 " << +key << " → 위치 " << pos << " (" << ms * 1000.0 << " us)\n";
        } else {
            miss_ms += ms;
            ++misses;
            cout << "키 " << +key << " → 데이터셋에 없음 (" << ms * 1000.0 << " us)\n";
        }
    }

    cout << "\n=== 결과 ===\n";
    cout << "빌드 시간: " << build_ms << " ms\n";
    cout << "있는 키 검색 " << hits << "회: " << hit_ms << " ms\n";
    cout << "없는 키 검색 " << misses << "회: " << miss_ms << " ms\n";
    cout << "총 시간(빌드 + 검색): " << build_ms + hit_ms + miss_ms << " ms\n";
    print_memory_usage();
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // 한글 출력
#endif

    Options opt;
    if (!parse_args(argc, argv, opt)) return 1;

    if (!set_memory_limit(MEMORY_LIMIT)) {
        cerr << "메모리 제한 설정 실패\n";
        return 1;
    }

    return opt.key_bits == 64 ? run<uint64_t>(opt) : run<uint32_t>(opt);
}
