#pragma once
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "baseline.h"
#include "interp_search.h"
#include "mapped_file.h"
#include "sosd_index.h"

// ─────────────────────────────────────────────────────────────────────────────
// 벤치마크
//   총 시간 = 빌드 시간 + (있는 키 검색 시간) + (없는 키 검색 시간)
//   - 있는 키(hit) : 데이터에서 무작위 위치의 키
//   - 없는 키(miss): 데이터에 존재하지 않음을 확인한 키
// 모든 방법의 결과를 정답(std::lower_bound)과 비교해 정확성도 검사한다.
// ─────────────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;

inline double elapsed_ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct BenchResult {
    std::string name;
    double build_ms = 0;
    double hit_ms = 0;
    double miss_ms = 0;
    size_t index_bytes = 0;
    bool correct = true;
    double total() const { return build_ms + hit_ms + miss_ms; }
};

template <class Key>
struct QuerySet {
    std::vector<Key> hit_keys;
    std::vector<uint64_t> hit_expected;  // 키가 처음 등장하는 위치
    std::vector<Key> miss_keys;
};

template <class Key>
QuerySet<Key> make_queries(const Key* data, uint64_t n, size_t count, uint64_t seed) {
    QuerySet<Key> q;
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<uint64_t> pos_dist(0, n - 1);

    for (size_t i = 0; i < count; ++i) {
        const Key k = data[pos_dist(rng)];
        q.hit_keys.push_back(k);
        q.hit_expected.push_back(plain_binary_search(data, n, k));
    }

    // 없는 키: 무작위 키 후보 중 실제로 없는 것만 채택
    const Key lo = data[0], hi = data[n - 1];
    std::uniform_int_distribution<uint64_t> key_dist(lo, hi);
    size_t attempts = 0;
    while (q.miss_keys.size() < count && attempts < count * 100) {
        ++attempts;
        // 절반은 인접한 두 키 사이의 빈 값, 절반은 완전 무작위 값
        Key k;
        if (attempts % 2) {
            const uint64_t p = pos_dist(rng);
            k = static_cast<Key>(data[p] + 1);
        } else {
            k = static_cast<Key>(key_dist(rng));
        }
        if (plain_binary_search(data, n, k) == NOT_FOUND) q.miss_keys.push_back(k);
    }
    // 빈 값이 거의 없는 dense 데이터: 범위 밖 키로 채움
    Key extra = hi;
    while (q.miss_keys.size() < count && extra < std::numeric_limits<Key>::max()) {
        q.miss_keys.push_back(++extra);
    }
    return q;
}

// find(key)를 제공하는 검색기로 hit/miss 시간 측정
template <class Key, class Finder>
void run_queries(const QuerySet<Key>& q, Finder&& find, BenchResult& r) {
    std::vector<uint64_t> got(q.hit_keys.size());

    auto t0 = Clock::now();
    for (size_t i = 0; i < q.hit_keys.size(); ++i) got[i] = find(q.hit_keys[i]);
    auto t1 = Clock::now();
    r.hit_ms = elapsed_ms(t0, t1);
    for (size_t i = 0; i < got.size(); ++i)
        if (got[i] != q.hit_expected[i]) r.correct = false;

    got.assign(q.miss_keys.size(), 0);
    t0 = Clock::now();
    for (size_t i = 0; i < q.miss_keys.size(); ++i) got[i] = find(q.miss_keys[i]);
    t1 = Clock::now();
    r.miss_ms = elapsed_ms(t0, t1);
    for (uint64_t g : got)
        if (g != NOT_FOUND) r.correct = false;
}

inline void print_bench_table(const std::vector<BenchResult>& results, size_t queries,
                              std::ostream& out) {
    out << std::fixed << std::setprecision(3);
    out << std::left << std::setw(26) << "방법" << std::right << std::setw(12) << "인덱스(KB)"
        << std::setw(12) << "빌드(ms)" << std::setw(12) << "hit(ms)" << std::setw(12)
        << "miss(ms)" << std::setw(12) << "합계(ms)" << std::setw(14) << "쿼리당(us)"
        << "  정확성\n";
    for (const auto& r : results) {
        const double per_query_us = (r.hit_ms + r.miss_ms) * 1000.0 / (2.0 * queries);
        out << std::left << std::setw(26) << r.name << std::right << std::setw(12)
            << r.index_bytes / 1024.0 << std::setw(12) << r.build_ms << std::setw(12)
            << r.hit_ms << std::setw(12) << r.miss_ms << std::setw(12) << r.total()
            << std::setw(14) << per_query_us << "  " << (r.correct ? "OK" : "FAIL") << '\n';
    }
}

// 각 방법마다 파일을 새로 매핑해서 측정한다 (매핑 + 인덱스 생성 = 빌드 시간).
// 같은 매핑을 공유하면 앞선 방법이 이미 접근한 페이지 덕분에 뒤 방법의 빌드가 빨라 보이기 때문.
// steps: 비교할 샘플 간격 목록
template <class Key>
std::vector<BenchResult> run_benchmark(const std::string& path, const std::vector<uint64_t>& steps,
                                       size_t queries, uint64_t seed) {
    std::vector<BenchResult> results;

    QuerySet<Key> q;
    uint64_t n = 0;
    {
        MappedFile f;
        if (!f.open(path)) return results;
        std::memcpy(&n, f.data(), sizeof(n));
        q = make_queries(reinterpret_cast<const Key*>(f.data() + 8), n, queries, seed);
        // 정답을 구하면서 쿼리 주변 페이지가 OS 캐시에 올라가므로
        // 모든 방법이 같은 조건(페이지 캐시가 따뜻한 상태)에서 측정된다.
    }

    // (A) 기준: 파일 이진 탐색 (seek + read)
    {
        BenchResult r;
        r.name = "baseline: ifstream 이진탐색";
        auto t0 = Clock::now();
        StreamBinarySearch<Key> s;
        s.open(path, n);
        r.build_ms = elapsed_ms(t0, Clock::now());
        run_queries(q, [&](Key k) { return s.find(k); }, r);
        results.push_back(r);
    }

    // (B) 기준: 매핑된 배열 전체 이진 탐색
    {
        BenchResult r;
        r.name = "baseline: mmap 이진탐색";
        auto t0 = Clock::now();
        MappedFile f;
        f.open(path);
        const Key* data = reinterpret_cast<const Key*>(f.data() + 8);
        r.build_ms = elapsed_ms(t0, Clock::now());
        run_queries(q, [&](Key k) { return plain_binary_search(data, n, k); }, r);
        results.push_back(r);
    }

    // (C) 제안: 보간 탐색 (빌드 없음)
    {
        BenchResult r;
        r.name = "보간탐색 (빌드 없음)";
        auto t0 = Clock::now();
        MappedFile f;
        f.open(path);
        InterpolationSearch<Key> s;
        s.build(reinterpret_cast<const Key*>(f.data() + 8), n);
        r.build_ms = elapsed_ms(t0, Clock::now());
        run_queries(q, [&](Key k) { return s.find(k); }, r);
        results.push_back(r);
    }

    // (D) 비교: SosdIndex (샘플 간격별)
    for (uint64_t step : steps) {
        BenchResult r;
        r.name = "SosdIndex step=" + std::to_string(step);
        auto t0 = Clock::now();
        MappedFile f;
        f.open(path);
        SosdIndex<Key> idx;
        idx.build(reinterpret_cast<const Key*>(f.data() + 8), n, step);
        r.build_ms = elapsed_ms(t0, Clock::now());
        r.index_bytes = idx.memory_bytes();
        run_queries(q, [&](Key k) { return idx.find(k); }, r);
        results.push_back(r);
    }
    return results;
}
