// ======= StructureCompare.cpp =======
// 4단계 비교 실험: 데이터를 메모리에 올려 자료구조를 만드는 방식들 (제출물 아님)
//
// 측정 대상 (모두 파일 앞부분 N개 키를 메모리에 올린 뒤 만듭니다)
//   array : 정렬 배열 그대로 + 이분 탐색 (std::lower_bound)  → 올리기만 하는 가장 단순한 방식
//   hash  : std::unordered_map<키, 위치>  (reserve로 재해싱 없이, 가장 유리한 조건)
//   map   : std::map<키, 위치>            (레드블랙 트리. std::set은 위치를 담을 수 없어 map 사용)
//   avl   : 직접 구현한 AVL 트리 (experiments/AvlTree.h)
//   fullread : 파일 전체(약 800MB)를 순차로 한 번 읽는 시간만 측정
//              → 데이터를 전부 올리는 방식은 무엇이든 이 시간보다 빠를 수 없음
//
// 측정 항목
//   읽기 ms   : 파일에서 N개 키를 순차로 읽는 시간 (OS 캐시 없이, 1MB 단위)
//   구축 ms   : 메모리의 키로 자료구조를 만드는 시간
//   메모리 MB : 자료구조를 만들며 늘어난 프로세스 메모리 (Private Bytes)
//   검색 us   : 쿼리 5개(존재 3 + 미존재 2) 검색 시간
//   정확성    : 무작위 존재 키 1만 개 + 미존재 키 1만 개의 결과가 맞는지
//
// 자료구조를 지운 메모리를 다음 자료구조가 재사용하면 메모리 측정이 틀어지므로,
// 한 번 실행에 자료구조 하나만 측정합니다.
//
// 사용법
//   StructureCompare.exe <데이터 파일> <N> <array|hash|map|avl>
//   StructureCompare.exe <데이터 파일> fullread

#include "AvlTree.h"
#include "DataFile.h"
#include "Timer.h"

#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "psapi.lib")

// ======= 측정 도구 =======

// 현재 프로세스가 확보한 메모리 (Private Bytes, MB)
double privateMB()
{
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return counters.PrivateUsage / (1024.0 * 1024.0);
}

// 파일 앞부분 keyCount개 키를 순차로 읽습니다 (OS 캐시 없이, 1MB 단위).
// keys가 nullptr이면 읽기만 하고 저장하지 않습니다. 걸린 시간(ms)을 반환합니다.
double readKeysSequential(const std::wstring& path, uint64_t keyCount, std::vector<uint32_t>* keys)
{
    const DWORD CHUNK_BYTES = 1 << 20;
    const uint64_t needBytes = 8 + keyCount * 4;

    if (keys) keys->assign(keyCount, 0);   // 할당은 읽기 시간에 넣지 않음
    uint8_t* chunk = static_cast<uint8_t*>(VirtualAlloc(nullptr, CHUNK_BYTES,
                                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    Timer timer;
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        VirtualFree(chunk, 0, MEM_RELEASE);
        return -1;
    }

    uint64_t offset = 0;
    while (offset < needBytes)
    {
        DWORD got = 0;
        if (!ReadFile(handle, chunk, CHUNK_BYTES, &got, nullptr) || got == 0) break;

        // 이번 조각 [offset, offset + got) 중 키 구간 [8, needBytes)에 해당하는 부분을 복사
        if (keys)
        {
            uint64_t from = std::max<uint64_t>(offset, 8);
            uint64_t to = std::min<uint64_t>(offset + got, needBytes);
            if (to > from)
            {
                std::memcpy(keys->data() + (from - 8) / 4, chunk + (from - offset), to - from);
            }
        }
        offset += got;
    }
    double ms = timer.elapsedMs();

    CloseHandle(handle);
    VirtualFree(chunk, 0, MEM_RELEASE);
    return ms;
}

// ======= 쿼리 =======

struct Query
{
    uint32_t key;
    bool exists;
    uint64_t position;   // exists일 때 정답 위치
};

// 존재하는 키 existCount개 + 존재하지 않는 키(인접 키 사이 빈 값) absentCount개
std::vector<Query> makeQueries(const std::vector<uint32_t>& keys,
                               int existCount, int absentCount, uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> pick(0, keys.size() - 2);
    std::vector<Query> queries;

    for (int i = 0; i < existCount; ++i)
    {
        uint64_t index = pick(rng);
        queries.push_back({ keys[index], true, index });
    }
    for (int made = 0; made < absentCount; )
    {
        uint64_t index = pick(rng);
        if (static_cast<uint64_t>(keys[index + 1]) - keys[index] >= 2)
        {
            queries.push_back({ keys[index] + 1, false, 0 });
            ++made;
        }
    }
    return queries;
}

// ======= 자료구조별 측정 =======
// 각 측정은 (구축, 검색 함수)를 받아 같은 방식으로 잽니다.

struct Result
{
    double buildMs = 0;
    double memoryMB = 0;
    double searchUs = 0;
    uint64_t failures = 0;
};

// find(key, position&) → bool
template <typename BuildFn, typename FindFn>
Result measure(BuildFn build, FindFn find,
               const std::vector<Query>& fiveQueries, const std::vector<Query>& checkQueries)
{
    Result r;

    double memBefore = privateMB();
    Timer buildTimer;
    build();
    r.buildMs = buildTimer.elapsedMs();
    r.memoryMB = privateMB() - memBefore;

    // 쿼리 5개 검색 시간 (결과를 버리지 않도록 합계를 남김)
    volatile uint64_t sink = 0;
    Timer searchTimer;
    for (const Query& q : fiveQueries)
    {
        uint64_t position = 0;
        if (find(q.key, position)) sink = sink + position;
    }
    r.searchUs = searchTimer.elapsedMs() * 1000.0;

    // 정확성
    for (const Query& q : checkQueries)
    {
        uint64_t position = 0;
        bool found = find(q.key, position);
        if (found != q.exists || (found && position != q.position)) ++r.failures;
    }
    return r;
}

// ======= 메인 =======

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 3)
    {
        std::printf("사용법: StructureCompare.exe <데이터 파일> <N> <array|hash|map|avl>\n");
        std::printf("        StructureCompare.exe <데이터 파일> fullread\n");
        return 1;
    }
    std::wstring path = argv[1];
    std::wstring second = argv[2];

    // 전체 키 개수 (헤더)
    DataFile header;
    if (!header.open(path, false))
    {
        std::printf("[오류] %s\n", header.errorMessage().c_str());
        return 1;
    }
    const uint64_t totalKeys = header.keyCount();
    header.close();

    // 파일 전체 순차 읽기
    if (second == L"fullread")
    {
        double ms = readKeysSequential(path, totalKeys, nullptr);
        std::printf("fullread | 키 %llu개 (%.0f MB) 순차 읽기 %.1f ms\n",
                    (unsigned long long)totalKeys, (8 + totalKeys * 4) / (1024.0 * 1024.0), ms);
        return 0;
    }

    if (argc < 4)
    {
        std::printf("[오류] 자료구조 이름이 없습니다.\n");
        return 1;
    }
    const uint64_t n = std::min<uint64_t>(std::wcstoull(argv[2], nullptr, 10), totalKeys);
    const std::wstring kind = argv[3];

    // 1. 앞부분 N개 키 읽기 (모든 자료구조 공통)
    std::vector<uint32_t> keys;
    double readMs = readKeysSequential(path, n, &keys);

    std::vector<Query> fiveQueries = makeQueries(keys, 3, 2, 7);           // 평가 상황: 존재 3 + 미존재 2
    std::vector<Query> checkQueries = makeQueries(keys, 10000, 10000, 99); // 정확성 검사용

    // 2. 자료구조별 구축·검색
    Result r;
    std::string name;
    std::string extra;

    if (kind == L"array")
    {
        name = "정렬 배열 + 이분 탐색";
        // 키는 이미 읽어 둔 배열 그대로 씁니다. 구축할 것이 없고, 메모리는 배열 크기(4 × N)입니다.
        r = measure(
            [] {},
            [&](uint32_t key, uint64_t& position) {
                auto it = std::lower_bound(keys.begin(), keys.end(), key);
                if (it == keys.end() || *it != key) return false;
                position = static_cast<uint64_t>(it - keys.begin());
                return true;
            },
            fiveQueries, checkQueries);
        r.memoryMB = keys.size() * 4 / (1024.0 * 1024.0);
    }
    else if (kind == L"hash")
    {
        name = "해시 테이블 (unordered_map)";
        std::unordered_map<uint32_t, uint64_t> table;
        r = measure(
            [&] {
                table.reserve(n);
                for (uint64_t i = 0; i < n; ++i) table.emplace(keys[i], i);   // 같은 키는 첫 위치 유지
            },
            [&](uint32_t key, uint64_t& position) {
                auto it = table.find(key);
                if (it == table.end()) return false;
                position = it->second;
                return true;
            },
            fiveQueries, checkQueries);
    }
    else if (kind == L"map")
    {
        name = "레드블랙 트리 (std::map)";
        std::map<uint32_t, uint64_t> tree;
        r = measure(
            [&] {
                for (uint64_t i = 0; i < n; ++i) tree.emplace(keys[i], i);
            },
            [&](uint32_t key, uint64_t& position) {
                auto it = tree.find(key);
                if (it == tree.end()) return false;
                position = it->second;
                return true;
            },
            fiveQueries, checkQueries);
    }
    else if (kind == L"avl")
    {
        name = "AVL 트리 (직접 구현)";
        AvlTree tree;
        r = measure(
            [&] {
                for (uint64_t i = 0; i < n; ++i) tree.insert(keys[i], i);
            },
            [&](uint32_t key, uint64_t& position) { return tree.find(key, position); },
            fiveQueries, checkQueries);
        extra = " (트리 높이 " + std::to_string(tree.treeHeight()) + ")";
    }
    else
    {
        std::printf("[오류] 알 수 없는 자료구조입니다. (array, hash, map, avl)\n");
        return 1;
    }

    // 3. 출력 (2억 개 환산은 N에 비례한다고 보고 계산한 하한)
    double scale = static_cast<double>(totalKeys) / static_cast<double>(n);
    std::printf("%s | N=%llu | 읽기 %.1f ms | 구축 %.1f ms | 메모리 %.1f MB | 검색 %.2f us | 오류 %llu/%zu%s\n",
                name.c_str(), (unsigned long long)n, readMs, r.buildMs, r.memoryMB, r.searchUs,
                (unsigned long long)r.failures, checkQueries.size(), extra.c_str());
    std::printf("    → %llu개 환산: 읽기+구축 약 %.0f ms, 메모리 약 %.0f MB\n",
                (unsigned long long)totalKeys, (readMs + r.buildMs) * scale, r.memoryMB * scale);
    return r.failures == 0 ? 0 : 2;
}
