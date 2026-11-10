// ======= main.cpp =======
// SOSD 데이터셋에서 키의 위치를 찾는 프로그램
//
// 흐름: [빌드] → [입력] → [검색] → [출력]
//   - 빌드 시간: 파일 열기 + 헤더·첫 키·마지막 키 읽기   (측정 O)
//   - 입력 시간: 사용자가 쿼리를 타이핑하는 시간         (측정 X)
//   - 검색 시간: 입력이 끝난 시점부터 모든 결과를 얻을 때까지 (측정 O)
//   - 결과 출력은 검색 시간에 넣지 않습니다.
//
// 사용법
//   SosdSearch.exe <데이터 파일> [--no-buffering] [--binary]
//   --no-buffering: OS 파일 캐시를 거치지 않고 디스크에서 직접 읽습니다 (실험용).
//   --binary      : 보간 탐색 대신 1단계 기준선(이분 탐색)을 씁니다 (비교용).
//   --index K     : 샘플 K개짜리 희소 인덱스 + 보간 탐색을 씁니다 (3단계 실험용).

#include "DataFile.h"
#include "Searcher.h"
#include "Timer.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ======= 입력 처리 =======

// 한 줄에서 공백으로 구분된 정수들을 읽습니다. 정수가 아닌 값은 알리고 건너뜁니다.
std::vector<int64_t> parseQueries(const std::string& line)
{
    std::vector<int64_t> queries;
    std::istringstream stream(line);
    std::string token;

    while (stream >> token)
    {
        try
        {
            size_t used = 0;
            long long value = std::stoll(token, &used);
            if (used != token.size())
            {
                throw std::invalid_argument("정수 뒤에 다른 문자가 있음");
            }
            queries.push_back(value);
        }
        catch (const std::exception&)
        {
            std::printf("  [건너뜀] 정수가 아닙니다: %s\n", token.c_str());
        }
    }
    return queries;
}

// ======= 메인 =======

int wmain(int argc, wchar_t* argv[])
{
    // 한글 출력이 깨지지 않도록 콘솔 출력을 UTF-8로 설정합니다.
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2)
    {
        std::printf("사용법: SosdSearch.exe <데이터 파일> [--no-buffering] [--binary | --index K]\n");
        return 1;
    }
    std::wstring path = argv[1];
    bool noBuffering = false;
    bool useBinary = false;
    uint64_t indexSamples = 0;   // 0이면 희소 인덱스를 쓰지 않음
    for (int i = 2; i < argc; ++i)
    {
        std::wstring option = argv[i];
        if (option == L"--no-buffering")
        {
            noBuffering = true;
        }
        else if (option == L"--binary")
        {
            useBinary = true;
        }
        else if (option == L"--index" && i + 1 < argc)
        {
            indexSamples = std::wcstoull(argv[++i], nullptr, 10);
            if (indexSamples < 2)
            {
                std::printf("[오류] --index의 샘플 수는 2 이상이어야 합니다.\n");
                return 1;
            }
        }
        else
        {
            std::printf("[오류] 알 수 없는 옵션입니다. (--no-buffering, --binary, --index K만 가능)\n");
            return 1;
        }
    }

    try
    {
        // ======= 1. 빌드 (측정 O) =======
        DataFile file;
        BinarySearcher binarySearcher(file);
        InterpolationSearcher interpolationSearcher(file);
        SparseIndexSearcher sparseIndexSearcher(file, indexSamples);

        Searcher* chosen = &interpolationSearcher;
        if (useBinary)
        {
            chosen = &binarySearcher;
        }
        else if (indexSamples > 0)
        {
            chosen = &sparseIndexSearcher;
        }
        Searcher& searcher = *chosen;

        Timer buildTimer;
        if (!file.open(path, noBuffering))
        {
            std::printf("[오류] %s\n", file.errorMessage().c_str());
            return 1;
        }
        searcher.build();
        double buildMs = buildTimer.elapsedMs();
        uint64_t buildReads = file.readCount();

        std::printf("[빌드] %s, 키 %llu개, 첫 키 %u, 마지막 키 %u\n",
                    searcher.name(), (unsigned long long)file.keyCount(),
                    searcher.firstKey(), searcher.lastKey());
        std::printf("       시간 %.3f ms, 블록 읽기 %llu회%s\n",
                    buildMs, (unsigned long long)buildReads,
                    noBuffering ? " (OS 캐시 사용 안 함)" : "");

        // ======= 2. 입력 (측정 X) =======
        std::printf("\n쿼리 키를 공백으로 구분해 한 줄로 입력하세요:\n> ");
        std::fflush(stdout);

        std::string line;
        std::getline(std::cin, line);
        std::vector<int64_t> queries = parseQueries(line);
        if (queries.empty())
        {
            std::printf("입력된 쿼리가 없습니다.\n");
            return 0;
        }

        // ======= 3. 검색 (측정 O) =======
        std::vector<SearchResult> results;
        std::vector<double> queryMs;
        results.reserve(queries.size());
        queryMs.reserve(queries.size());

        Timer searchTimer;
        for (int64_t key : queries)
        {
            Timer queryTimer;
            results.push_back(searcher.find(key));
            queryMs.push_back(queryTimer.elapsedMs());
        }
        double searchMs = searchTimer.elapsedMs();

        // ======= 4. 출력 (측정 X) =======
        std::printf("\n[검색 결과]\n");
        uint64_t totalReads = 0;
        for (size_t i = 0; i < queries.size(); ++i)
        {
            const SearchResult& r = results[i];
            totalReads += r.blockReads;

            if (r.found)
            {
                std::printf("  %zu) 키 %lld → 위치 %llu (블록 읽기 %llu회, %.3f ms)\n",
                            i + 1, (long long)queries[i], (unsigned long long)r.position,
                            (unsigned long long)r.blockReads, queryMs[i]);
            }
            else
            {
                std::printf("  %zu) 키 %lld → 없음 (블록 읽기 %llu회, %.3f ms)\n",
                            i + 1, (long long)queries[i],
                            (unsigned long long)r.blockReads, queryMs[i]);
            }
        }

        std::printf("\n[시간] 빌드 %.3f ms + 검색 %.3f ms = 합계 %.3f ms\n",
                    buildMs, searchMs, buildMs + searchMs);
        std::printf("[읽기] 빌드 %llu회 + 검색 %llu회 (쿼리 %zu개, 평균 %.1f회)\n",
                    (unsigned long long)buildReads, (unsigned long long)totalReads,
                    queries.size(), (double)totalReads / queries.size());
    }
    catch (const std::exception& e)
    {
        std::printf("[오류] %s\n", e.what());
        return 1;
    }

    return 0;
}
