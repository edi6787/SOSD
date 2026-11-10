// ======= VerifySearch.cpp =======
// 탐색 정확성 검증 도구 (제출물 아님)
//
// 모든 탐색기(이분 탐색, 보간 탐색)에 같은 쿼리를 넣고 아래를 확인합니다.
//   - 탐색기와 별개로 파일을 직접 읽어 만든 정답과 같은지
//   - 탐색기끼리 결과(존재 여부, 위치)가 같은지
// 쿼리 종류
//   1. 존재하는 키: 무작위 위치의 키 → 그 키의 첫 위치를 반환해야 함
//   2. 존재하지 않는 키: 인접한 두 키 사이 값 → "없음"
//   3. 경계: 첫 키, 마지막 키, 첫 키 - 1, 마지막 키 + 1, 음수, uint32 범위 초과
// 탐색기마다 쿼리당 블록 읽기 횟수(평균·최대)를 집계합니다.
//
// 사용법
//   VerifySearch.exe <데이터 파일> [무작위 쿼리 수 (기본 10000)]

#include "DataFile.h"
#include "Searcher.h"

#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// ======= 정답용 직접 읽기 =======
// DataFile을 쓰지 않고 표준 파일 함수로 따로 읽어, 탐색기 코드의 실수가 정답에 섞이지 않게 합니다.

class DirectReader
{
public:
    explicit DirectReader(const std::wstring& path)
    {
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr)
        {
            throw std::runtime_error("검증용으로 파일을 열 수 없습니다.");
        }
    }
    ~DirectReader() { if (fp) std::fclose(fp); }

    uint32_t keyAt(uint64_t index)
    {
        uint32_t key = 0;
        _fseeki64(fp, static_cast<long long>(8 + index * 4), SEEK_SET);
        if (std::fread(&key, sizeof(key), 1, fp) != 1)
        {
            throw std::runtime_error("검증용 읽기 실패");
        }
        return key;
    }

private:
    FILE* fp = nullptr;
};

// ======= 집계 =======

struct Stats
{
    uint64_t tested = 0;
    uint64_t failed = 0;
    uint64_t readSum = 0;
    uint64_t readMax = 0;

    void add(bool ok, uint64_t reads)
    {
        ++tested;
        if (!ok) ++failed;
        readSum += reads;
        if (reads > readMax) readMax = reads;
    }

    void print(const char* name) const
    {
        std::printf("    %-20s %7llu개, 실패 %llu개, 블록 읽기 평균 %.2f회 / 최대 %llu회\n",
                    name, (unsigned long long)tested, (unsigned long long)failed,
                    tested ? (double)readSum / tested : 0.0, (unsigned long long)readMax);
    }
};

// 탐색기 하나와 그 집계
struct Candidate
{
    Searcher* searcher;
    Stats found;
    Stats absent;
    Stats edge;
};

// ======= 검사 =======

// 존재하는 키: 반환 위치 p가 key의 "첫 위치"인지 확인
bool checkFound(DirectReader& direct, int64_t key, const SearchResult& r)
{
    if (!r.found) return false;
    if (direct.keyAt(r.position) != key) return false;
    if (r.position > 0 && direct.keyAt(r.position - 1) == key) return false;
    return true;
}

enum class QueryKind { Found, Absent, Edge };

// 모든 탐색기에 같은 쿼리를 넣고 정답·서로의 결과와 비교합니다.
// 탐색기끼리 결과가 다르면 mismatch를 1 늘립니다.
void runQuery(std::vector<Candidate>& candidates, DirectReader& direct,
              int64_t key, bool shouldExist, QueryKind kind, uint64_t& mismatch,
              std::vector<SearchResult>* out = nullptr)
{
    std::vector<SearchResult> results;
    for (Candidate& c : candidates)
    {
        SearchResult r = c.searcher->find(key);
        bool ok = shouldExist ? checkFound(direct, key, r) : !r.found;
        Stats& stats = (kind == QueryKind::Found) ? c.found
                     : (kind == QueryKind::Absent) ? c.absent : c.edge;
        stats.add(ok, r.blockReads);
        results.push_back(r);
    }

    for (size_t i = 1; i < results.size(); ++i)
    {
        if (results[i].found != results[0].found ||
            (results[i].found && results[i].position != results[0].position))
        {
            ++mismatch;
            break;
        }
    }

    if (out) *out = results;
}

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2)
    {
        std::printf("사용법: VerifySearch.exe <데이터 파일> [무작위 쿼리 수]\n");
        return 1;
    }
    std::wstring path = argv[1];
    uint64_t randomCount = (argc >= 3) ? std::wcstoull(argv[2], nullptr, 10) : 10000;

    try
    {
        DataFile file;
        if (!file.open(path, false))
        {
            std::printf("[오류] %s\n", file.errorMessage().c_str());
            return 1;
        }

        // 희소 인덱스는 샘플이 아주 적은 경우(3개)와 보통인 경우(1024개)를 함께 검사합니다.
        BinarySearcher binary(file);
        InterpolationSearcher interpolation(file);
        SparseIndexSearcher indexSmall(file, 3);
        SparseIndexSearcher indexLarge(file, 1024);

        std::vector<Candidate> candidates = {
            { &binary, {}, {}, {} },
            { &interpolation, {}, {}, {} },
            { &indexSmall, {}, {}, {} },
            { &indexLarge, {}, {}, {} },
        };
        for (Candidate& c : candidates)
        {
            c.searcher->build();
        }

        DirectReader direct(path);
        const uint64_t n = file.keyCount();
        const uint32_t first = binary.firstKey();
        const uint32_t last = binary.lastKey();
        uint64_t mismatch = 0;

        std::mt19937_64 rng(12345);
        std::uniform_int_distribution<uint64_t> pickIndex(0, n - 1);

        // 1. 존재하는 키
        for (uint64_t t = 0; t < randomCount; ++t)
        {
            int64_t key = direct.keyAt(pickIndex(rng));
            runQuery(candidates, direct, key, true, QueryKind::Found, mismatch);
        }

        // 2. 존재하지 않는 키: 무작위 위치 i에서 a[i]와 a[i+1] 사이에 빈 값이 있으면 a[i] + 1
        //    (dense처럼 빈틈이 드문 데이터에서도 충분히 모이도록 시도 횟수에 여유를 둡니다)
        if (n >= 2)
        {
            std::uniform_int_distribution<uint64_t> pickPair(0, n - 2);
            uint64_t absentTested = 0;
            for (uint64_t attempt = 0; absentTested < randomCount && attempt < randomCount * 1000; ++attempt)
            {
                uint64_t i = pickPair(rng);
                uint32_t a = direct.keyAt(i);
                uint32_t b = direct.keyAt(i + 1);
                if (static_cast<uint64_t>(b) - a < 2) continue;

                runQuery(candidates, direct, static_cast<int64_t>(a) + 1, false, QueryKind::Absent, mismatch);
                ++absentTested;
            }
        }

        // 3. 경계
        struct EdgeCase { const char* name; int64_t key; bool shouldExist; };
        const EdgeCase edges[] = {
            { "첫 키",         (int64_t)first,     true  },
            { "마지막 키",     (int64_t)last,      true  },
            { "첫 키 - 1",     (int64_t)first - 1, false },
            { "마지막 키 + 1", (int64_t)last + 1,  false },
            { "음수 (-1)",     -1,                 false },
            { "uint32 초과",   4294967296LL,       false },
        };
        std::printf("[경계] (블록 읽기: 탐색기 순서대로)\n");
        for (const EdgeCase& e : edges)
        {
            // 첫 키가 0이면 "첫 키 - 1"은 음수 검사와 같고, 마지막 키가 최댓값이면 "+1"은 범위 초과 검사와 같습니다.
            std::vector<SearchResult> results;
            runQuery(candidates, direct, e.key, e.shouldExist, QueryKind::Edge, mismatch, &results);
            const SearchResult& r = results.back();
            std::string reads;
            for (size_t i = 0; i < results.size(); ++i)
            {
                reads += (i ? " / " : "") + std::to_string(results[i].blockReads);
            }
            std::printf("  %-14s 키 %lld → %s (%s)\n",
                        e.name, (long long)e.key,
                        r.found ? ("위치 " + std::to_string(r.position)).c_str() : "없음",
                        reads.c_str());
        }

        // 요약
        bool allOk = (mismatch == 0);
        std::printf("[요약]\n");
        for (const Candidate& c : candidates)
        {
            const SparseIndexSearcher* index = dynamic_cast<const SparseIndexSearcher*>(c.searcher);
            if (index)
            {
                std::printf("  %s (샘플 %llu개)\n", c.searcher->name(),
                            (unsigned long long)index->sampleCount());
            }
            else
            {
                std::printf("  %s\n", c.searcher->name());
            }
            c.found.print("존재하는 키");
            c.absent.print("존재하지 않는 키");
            c.edge.print("경계");
            allOk = allOk && c.found.failed == 0 && c.absent.failed == 0 && c.edge.failed == 0;
        }
        std::printf("  탐색기끼리 결과가 다른 쿼리: %llu개\n", (unsigned long long)mismatch);
        std::printf("[결과] %s\n", allOk ? "모두 통과" : "실패 있음");
        return allOk ? 0 : 2;
    }
    catch (const std::exception& e)
    {
        std::printf("[오류] %s\n", e.what());
        return 1;
    }
}
