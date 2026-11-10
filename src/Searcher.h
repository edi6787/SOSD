#pragma once

// ======= Searcher.h =======
// 데이터 파일에서 키의 위치를 찾는 탐색기들
//   Searcher              : 공통 부분 (빌드, 범위 검사, 존재 확인)
//   BinarySearcher        : 1단계 기준선. 파일 위 이분 탐색
//   InterpolationSearcher : 2단계 본안. 보간 탐색 + 이분 탐색 폴백
//   SparseIndexSearcher   : 3단계 개선안. 희소 인덱스로 구간을 고른 뒤 보간 탐색

#include "DataFile.h"

#include <cstdint>
#include <vector>

// 탐색 결과
struct SearchResult
{
    bool found;            // 키가 있는지
    uint64_t position;     // 키가 있으면 그 위치 (0부터 셈, 중복이면 첫 위치)
    uint64_t blockReads;   // 이 쿼리에서 파일을 읽은 횟수
};

// ======= Searcher (공통) =======
// 모든 탐색기는 "키 이상인 첫 위치(lower bound)"를 찾고, 그 위치의 값이 키와 같은지 확인합니다.
// 탐색 방법마다 다른 부분은 lowerBound() 하나뿐입니다.
class Searcher
{
public:
    explicit Searcher(DataFile& file);
    virtual ~Searcher() = default;

    // 빌드: 첫 키와 마지막 키를 읽어 둡니다. (탐색기에 따라 추가 준비를 할 수 있음)
    virtual void build();

    // 키를 찾습니다. 키는 입력 그대로(음수, uint32 초과 가능) 받습니다.
    SearchResult find(int64_t key);

    virtual const char* name() const = 0;

    uint32_t firstKey() const { return first; }
    uint32_t lastKey() const { return last; }

protected:
    // 첫 키 < target <= 마지막 키일 때, target 이상인 첫 위치를 반환합니다.
    virtual uint64_t lowerBound(uint32_t target) = 0;

    DataFile& file;
    uint32_t first;
    uint32_t last;
};

// ======= BinarySearcher (1단계 기준선) =======
// 범위의 가운데 위치를 읽어 절반씩 줄입니다.
// 범위가 블록 하나 안으로 좁혀지면 DataFile이 기억한 블록을 쓰므로 더 읽지 않습니다.
class BinarySearcher : public Searcher
{
public:
    explicit BinarySearcher(DataFile& file) : Searcher(file) {}
    const char* name() const override { return "이분 탐색"; }

protected:
    uint64_t lowerBound(uint32_t target) override;
};

// ======= InterpolationSearcher (2단계 본안) =======
// 범위 양 끝의 (위치, 키)를 잇는 직선으로 target의 위치를 예측하고, 그 위치가 든 블록을 읽습니다.
//   - 블록의 첫 키·마지막 키와 비교해 범위를 블록 단위로 줄입니다.
//   - target이 블록 안에 있으면 메모리에서 마무리합니다 (추가 읽기 없음).
// 폴백: 보간 한 번 뒤 범위가 절반보다 크게 남으면 다음 한 번은 가운데 위치를 읽습니다.
//       데이터가 고르지 않아 예측이 빗나가도 최악의 경우 이분 탐색의 약 2배 안에 끝납니다.
class InterpolationSearcher : public Searcher
{
public:
    explicit InterpolationSearcher(DataFile& file) : Searcher(file) {}
    const char* name() const override { return "보간 탐색"; }

protected:
    uint64_t lowerBound(uint32_t target) override;

    // 구간 (lo, hi] 안에서 보간 탐색으로 lower bound를 찾습니다.
    // 조건: a[lo] = loKey < target <= hiKey = a[hi]  (양 끝 키는 이미 알고 있는 값)
    uint64_t lowerBoundBetween(uint64_t lo, uint32_t loKey,
                               uint64_t hi, uint32_t hiKey, uint32_t target);

private:
    // 위치 [s, e]가 한 블록 안에 있고 a[s] < target <= a[e]일 때, 메모리에서 lower bound를 찾습니다.
    uint64_t lowerBoundInBlock(uint64_t s, uint64_t e, uint32_t target);
};

// ======= SparseIndexSearcher (3단계 개선안) =======
// 빌드할 때 파일의 일정 간격 위치 K개를 읽어 (키, 위치) 쌍 배열(희소 인덱스)을 만듭니다.
// 검색할 때는 이 배열에서 이분 탐색으로 target이 든 구간을 고르고(메모리, 읽기 없음),
// 그 구간 안에서 보간 탐색을 합니다.
//   - B+트리의 상위 레벨과 같은 역할입니다. 구간이 좁아 보간 예측이 더 정확해집니다.
//   - 대가: 샘플 1개 = 빌드 때 블록 읽기 1번
class SparseIndexSearcher : public InterpolationSearcher
{
public:
    // sampleCount: 샘플 수 K (첫 키·마지막 키 포함, 2 이상)
    SparseIndexSearcher(DataFile& file, uint64_t sampleCount);
    const char* name() const override { return "희소 인덱스 + 보간"; }

    void build() override;

    uint64_t sampleCount() const { return positions.size(); }

protected:
    uint64_t lowerBound(uint32_t target) override;

private:
    uint64_t requestedSamples;
    std::vector<uint64_t> positions;   // 샘플 위치 (오름차순)
    std::vector<uint32_t> keys;        // 그 위치의 키
};
