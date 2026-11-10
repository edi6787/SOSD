// ======= Bench.cpp =======
// 탐색 방법별 성능 측정 도구 (제출물 아님)
//
// 측정 대상: 이분 탐색, 보간 탐색, 희소 인덱스 + 보간 (샘플 수 K별)
// 측정 조건: OS 파일 캐시를 거치지 않음 (FILE_FLAG_NO_BUFFERING) → 블록 읽기마다 실제 디스크 접근
//
// 측정 항목
//   1. 평가 상황 재현: 쿼리 5개(존재 3 + 미존재 2)로 빌드 시간 + 검색 시간
//      → 반복마다 다른 쿼리 묶음을 쓰고(모든 방법에 같은 묶음), 중앙값을 냅니다.
//      빌드 시간에는 파일 열기도 포함합니다 (본 프로그램과 같음).
//   2. 쿼리당 블록 읽기: 무작위 쿼리 2000개(존재 1000 + 미존재 1000)의 평균·최대
//
// 사용법
//   Bench.exe <데이터 파일> [반복 횟수 (기본 7)]

#include "DataFile.h"
#include "Searcher.h"
#include "Timer.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// ======= 쿼리 준비 (측정 전) =======
// 탐색기와 별개로 표준 파일 함수로 읽어 쿼리를 고릅니다.

class QueryMaker
{
public:
    explicit QueryMaker(const std::wstring& path)
    {
        if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == nullptr)
        {
            throw std::runtime_error("쿼리 준비용으로 파일을 열 수 없습니다.");
        }
        std::fread(&count, sizeof(count), 1, fp);
    }
    ~QueryMaker() { if (fp) std::fclose(fp); }

    // 무작위 위치의 키 (존재하는 키)
    int64_t existing(std::mt19937_64& rng)
    {
        std::uniform_int_distribution<uint64_t> pick(0, count - 1);
        return keyAt(pick(rng));
    }

    // 인접한 두 키 사이의 빈 값 (존재하지 않는 키)
    int64_t absent(std::mt19937_64& rng)
    {
        std::uniform_int_distribution<uint64_t> pick(0, count - 2);
        while (true)
        {
            uint64_t i = pick(rng);
            uint32_t a = keyAt(i);
            uint32_t b = keyAt(i + 1);
            if (static_cast<uint64_t>(b) - a >= 2) return static_cast<int64_t>(a) + 1;
        }
    }

private:
    uint32_t keyAt(uint64_t index)
    {
        uint32_t key = 0;
        _fseeki64(fp, static_cast<long long>(8 + index * 4), SEEK_SET);
        std::fread(&key, sizeof(key), 1, fp);
        return key;
    }

    FILE* fp = nullptr;
    uint64_t count = 0;
};

// ======= 측정 대상 =======

struct Method
{
    std::string label;
    uint64_t samples;   // 0: 보간 탐색, 1: 이분 탐색, 2 이상: 희소 인덱스 샘플 수
};

std::unique_ptr<Searcher> makeSearcher(const Method& m, DataFile& file)
{
    if (m.samples == 1) return std::make_unique<BinarySearcher>(file);
    if (m.samples == 0) return std::make_unique<InterpolationSearcher>(file);
    return std::make_unique<SparseIndexSearcher>(file, m.samples);
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    return (values.size() % 2) ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
}

// ======= 메인 =======

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2)
    {
        std::printf("사용법: Bench.exe <데이터 파일> [반복 횟수]\n");
        return 1;
    }
    std::wstring path = argv[1];
    int repeats = (argc >= 3) ? std::max(1, _wtoi(argv[2])) : 7;

    try
    {
        // 1. 쿼리 준비
        QueryMaker maker(path);
        std::mt19937_64 rng(2026);

        std::vector<std::vector<int64_t>> fiveQuerySets(repeats);
        for (auto& set : fiveQuerySets)
        {
            for (int i = 0; i < 3; ++i) set.push_back(maker.existing(rng));
            for (int i = 0; i < 2; ++i) set.push_back(maker.absent(rng));
        }

        std::vector<int64_t> manyQueries;
        for (int i = 0; i < 1000; ++i) manyQueries.push_back(maker.existing(rng));
        for (int i = 0; i < 1000; ++i) manyQueries.push_back(maker.absent(rng));

        const std::vector<Method> methods = {
            { "이분 탐색",            1 },
            { "보간 탐색",            0 },
            { "희소 인덱스 K=16",     16 },
            { "희소 인덱스 K=64",     64 },
            { "희소 인덱스 K=256",    256 },
            { "희소 인덱스 K=1024",   1024 },
            { "희소 인덱스 K=4096",   4096 },
            { "희소 인덱스 K=16384",  16384 },
        };

        std::printf("측정 조건: OS 캐시 없이(NO_BUFFERING), 반복 %d회 중앙값, 쿼리 5개 = 존재 3 + 미존재 2\n\n", repeats);
        std::printf("%-22s | %10s %8s | %10s | %10s | %18s\n",
                    "방법", "빌드 ms", "빌드 읽기", "검색 ms", "합계 ms", "쿼리당 읽기 평균/최대");
        std::printf("-----------------------+---------------------+------------+------------+-------------------\n");

        for (const Method& m : methods)
        {
            std::vector<double> buildMs, searchMs, totalMs;
            uint64_t buildReads = 0;

            // 2. 평가 상황 재현 (반복)
            for (int rep = 0; rep < repeats; ++rep)
            {
                DataFile file;
                std::unique_ptr<Searcher> searcher = makeSearcher(m, file);

                Timer buildTimer;
                if (!file.open(path, true))
                {
                    std::printf("[오류] %s\n", file.errorMessage().c_str());
                    return 1;
                }
                searcher->build();
                double b = buildTimer.elapsedMs();
                buildReads = file.readCount();

                Timer searchTimer;
                for (int64_t key : fiveQuerySets[rep])
                {
                    searcher->find(key);
                }
                double s = searchTimer.elapsedMs();

                buildMs.push_back(b);
                searchMs.push_back(s);
                totalMs.push_back(b + s);
            }

            // 3. 쿼리당 블록 읽기 (시간 측정 없음)
            DataFile file;
            std::unique_ptr<Searcher> searcher = makeSearcher(m, file);
            file.open(path, false);
            searcher->build();
            uint64_t readSum = 0, readMax = 0;
            for (int64_t key : manyQueries)
            {
                uint64_t reads = searcher->find(key).blockReads;
                readSum += reads;
                readMax = std::max(readMax, reads);
            }

            std::printf("%-22s | %10.3f %8llu | %10.3f | %10.3f | %10.2f / %llu\n",
                        m.label.c_str(), median(buildMs), (unsigned long long)buildReads,
                        median(searchMs), median(totalMs),
                        (double)readSum / manyQueries.size(), (unsigned long long)readMax);
        }
    }
    catch (const std::exception& e)
    {
        std::printf("[오류] %s\n", e.what());
        return 1;
    }

    return 0;
}
