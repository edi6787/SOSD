// ======= Searcher.cpp =======

#include "Searcher.h"

#include <algorithm>

// ======= Searcher (공통) =======

Searcher::Searcher(DataFile& file)
    : file(file), first(0), last(0)
{
}

void Searcher::build()
{
    first = file.keyAt(0);
    last = file.keyAt(file.keyCount() - 1);
}

SearchResult Searcher::find(int64_t key)
{
    // 쿼리마다 같은 조건에서 읽기 횟수를 세기 위해, 이전 쿼리에서 기억한 블록을 버립니다.
    file.clearCache();
    file.resetReadCount();

    SearchResult result = { false, 0, 0 };

    // 첫 키보다 작거나 마지막 키보다 크면 파일을 읽지 않고 바로 "없음"
    // (음수와 uint32 범위를 넘는 값도 여기서 걸러집니다.)
    if (key < static_cast<int64_t>(first) || key > static_cast<int64_t>(last))
    {
        return result;
    }
    uint32_t target = static_cast<uint32_t>(key);

    // 첫 키와 같으면 위치 0 (빌드에서 이미 읽은 값이라 파일을 읽지 않음)
    if (target == first)
    {
        result.found = true;
        result.position = 0;
        return result;
    }

    // 여기서는 첫 키 < target <= 마지막 키
    uint64_t position = lowerBound(target);
    result.found = (file.keyAt(position) == target);
    result.position = position;
    result.blockReads = file.readCount();
    return result;
}

// ======= BinarySearcher =======

uint64_t BinarySearcher::lowerBound(uint32_t target)
{
    // a[0] < target이므로 답은 [1, n - 1] 안에 있습니다.
    uint64_t lo = 1;
    uint64_t hi = file.keyCount() - 1;
    while (lo < hi)
    {
        uint64_t mid = lo + (hi - lo) / 2;
        if (file.keyAt(mid) < target)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    return lo;
}

// ======= InterpolationSearcher =======

uint64_t InterpolationSearcher::lowerBound(uint32_t target)
{
    // 파일 전체 (0, n - 1]에서 찾습니다. 양 끝 키는 빌드에서 읽어 둔 첫 키·마지막 키입니다.
    return lowerBoundBetween(0, first, file.keyCount() - 1, last, target);
}

uint64_t InterpolationSearcher::lowerBoundBetween(uint64_t lo, uint32_t loKey,
                                                  uint64_t hi, uint32_t hiKey, uint32_t target)
{
    // 불변 조건: a[lo] = loKey < target <= hiKey = a[hi], 답은 (lo, hi] 안에 있음
    // 양 끝의 키 값(loKey, hiKey)은 이미 읽은 값이라 예측에 쓸 때 파일을 읽지 않습니다.
    bool nextIsBinary = false;   // 폴백: 다음 한 번은 가운데 위치를 읽을지

    while (hi - lo > 1)
    {
        // 1. 읽을 위치 고르기 (lo와 hi 사이, 양 끝 제외)
        uint64_t probe;
        if (nextIsBinary)
        {
            probe = lo + (hi - lo) / 2;
        }
        else
        {
            // 예측 위치 = lo + (target - loKey) / (hiKey - loKey) × (hi - lo)
            // 키 차이 × 위치 차이는 32비트를 넘으므로 double로 계산합니다.
            // 불변 조건 때문에 hiKey > loKey이므로 0으로 나누지 않습니다.
            double ratio = static_cast<double>(target - loKey) / static_cast<double>(hiKey - loKey);
            probe = lo + static_cast<uint64_t>(ratio * static_cast<double>(hi - lo));
            probe = std::max(probe, lo + 1);
            probe = std::min(probe, hi - 1);
        }

        // 2. probe가 든 블록을 읽고, 그 블록에서 (lo, hi) 안에 있는 구간 [s, e]를 봅니다.
        uint64_t block = file.blockOfKey(probe);
        uint64_t s = std::max(file.firstIndexInBlock(block), lo + 1);
        uint64_t e = std::min(file.lastIndexInBlock(block), hi - 1);
        uint32_t sKey = file.keyAt(s);   // s와 e는 같은 블록이라 읽기는 1번
        uint32_t eKey = file.keyAt(e);

        // 3. 블록의 첫 키·마지막 키와 비교해 범위를 줄입니다.
        uint64_t oldSize = hi - lo;
        if (eKey < target)
        {
            lo = e;              // 답은 블록 뒤쪽
            loKey = eKey;
        }
        else if (sKey >= target)
        {
            hi = s;              // 답은 블록 앞쪽 (s 포함)
            hiKey = sKey;
        }
        else
        {
            // a[s] < target <= a[e]: 답이 이 블록 안에 있음
            return lowerBoundInBlock(s, e, target);
        }

        // 4. 폴백 판단: 보간 뒤 범위가 절반보다 크게 남았으면 다음 한 번은 이분 탐색
        if (nextIsBinary)
        {
            nextIsBinary = false;
        }
        else
        {
            nextIsBinary = (hi - lo) > oldSize / 2;
        }
    }

    return hi;
}

uint64_t InterpolationSearcher::lowerBoundInBlock(uint64_t s, uint64_t e, uint32_t target)
{
    // 불변 조건: a[s] < target <= a[e]. 같은 블록이라 keyAt은 기억한 블록에서 읽습니다.
    uint64_t lo = s;
    uint64_t hi = e;
    while (hi - lo > 1)
    {
        uint64_t mid = lo + (hi - lo) / 2;
        if (file.keyAt(mid) < target)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }
    return hi;
}

// ======= SparseIndexSearcher =======

SparseIndexSearcher::SparseIndexSearcher(DataFile& file, uint64_t sampleCount)
    : InterpolationSearcher(file), requestedSamples(sampleCount < 2 ? 2 : sampleCount)
{
}

void SparseIndexSearcher::build()
{
    Searcher::build();   // 첫 키, 마지막 키

    const uint64_t n = file.keyCount();
    const uint64_t k = std::min(requestedSamples, n);   // 키보다 많이 뽑을 수는 없음

    positions.clear();
    keys.clear();
    positions.reserve(k);
    keys.reserve(k);

    // 위치 0부터 n - 1까지 같은 간격으로 k개를 뽑습니다. (첫 키·마지막 키 포함)
    // j × (n - 1)은 64비트 안에 들어갑니다 (k, n 모두 2^32 미만).
    for (uint64_t j = 0; j < k; ++j)
    {
        uint64_t position = (k == 1) ? 0 : j * (n - 1) / (k - 1);
        uint32_t key;
        if (j == 0)
        {
            key = first;            // 이미 읽은 값
        }
        else if (j == k - 1)
        {
            key = last;             // 이미 읽은 값
        }
        else
        {
            key = file.keyAt(position);
        }
        positions.push_back(position);
        keys.push_back(key);
    }
}

uint64_t SparseIndexSearcher::lowerBound(uint32_t target)
{
    // 1. 인덱스에서 target 이상인 첫 샘플 j를 찾습니다 (메모리 이분 탐색, 읽기 없음).
    //    keys[0] = 첫 키 < target <= 마지막 키 = keys[k - 1]이므로 1 <= j <= k - 1입니다.
    size_t j = std::lower_bound(keys.begin(), keys.end(), target) - keys.begin();

    // 2. 구간 (positions[j - 1], positions[j]] 안에 답이 있습니다.
    //    keys[j - 1] < target <= keys[j]
    return lowerBoundBetween(positions[j - 1], keys[j - 1], positions[j], keys[j], target);
}
