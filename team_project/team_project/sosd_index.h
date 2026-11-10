#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

// 검색 실패를 나타내는 값
constexpr uint64_t NOT_FOUND = UINT64_MAX;

// ─────────────────────────────────────────────────────────────────────────────
// SosdIndex : 정렬된 키 배열(메모리 매핑된 SOSD 파일) 위에 얹는 3단계 인덱스
//
//   [1단계] Radix 테이블  : 키의 상위 비트 → 샘플 배열의 구간      O(1)
//   [2단계] 샘플 배열      : step 개마다 하나씩 뽑은 키 (희소 인덱스)  구간 내 이진 탐색
//   [3단계] 데이터 블록    : 두 샘플 사이(step개)에서
//                            보간(interpolation)으로 위치 예측 → 지수 탐색 → 이진 탐색
//
// - 빌드: 파일에서 n/step 개의 키만 읽는다 (파일 전체를 읽지 않음 → 빌드 시간 최소화)
// - 인덱스 메모리: n/step * sizeof(Key) + radix 테이블 (수백 KB 이하)
// - 검색: 캐시에 올라가 있는 작은 인덱스로 범위를 step개로 좁힌 뒤,
//         균등 분포 데이터에서는 보간 예측이 거의 정확하므로 데이터 페이지를 1~2개만 접근.
//         분포가 치우친 데이터라도 지수/이진 탐색 덕분에 최악 O(log step)이 보장된다.
//
// 검색 결과: 키가 처음 등장하는 위치(0부터 시작하는 인덱스), 없으면 NOT_FOUND
// ─────────────────────────────────────────────────────────────────────────────
template <class Key>
class SosdIndex {
public:
    // data: 정렬된 키 배열, n: 개수, step: 샘플 간격(클수록 빌드↓ 검색↑)
    void build(const Key* data, uint64_t n, uint64_t step) {
        data_ = data;
        n_ = n;
        step_ = step == 0 ? 1 : step;
        samples_.clear();
        radix_.clear();
        if (n_ == 0) return;

        min_key_ = data_[0];
        max_key_ = data_[n_ - 1];

        // [2단계] 샘플 배열: data[0], data[step], data[2*step], ...
        const uint64_t m = (n_ + step_ - 1) / step_;
        samples_.resize(m);
        for (uint64_t j = 0; j < m; ++j) samples_[j] = data_[j * step_];

        // [1단계] Radix 테이블
        //   prefix(k) = (k - min) >> shift  (상위 radix_bits 비트)
        //   radix_[b] = prefix가 b보다 작은 샘플의 개수
        //   → prefix가 b인 샘플들은 samples_[radix_[b] .. radix_[b+1]) 에 있다.
        const uint64_t range = static_cast<uint64_t>(max_key_ - min_key_);
        const unsigned range_bits = static_cast<unsigned>(std::bit_width(range));
        unsigned radix_bits = static_cast<unsigned>(std::bit_width(m)) + 1;  // 버킷당 샘플 ~0.5개
        radix_bits = std::clamp(radix_bits, 1u, 20u);
        radix_bits = std::min(radix_bits, range_bits);
        shift_ = range_bits - radix_bits;

        const uint64_t buckets = (range >> shift_) + 1;
        radix_.assign(buckets + 1, 0);
        uint64_t j = 0;
        for (uint64_t b = 0; b <= buckets; ++b) {
            while (j < m && prefix(samples_[j]) < b) ++j;
            radix_[b] = static_cast<uint32_t>(j);
        }
    }

    uint64_t find(Key key) const {
        // 범위 밖의 키는 데이터를 보지 않고 바로 '없음' 판정
        if (n_ == 0 || key < min_key_ || key > max_key_) return NOT_FOUND;

        // [1단계] radix 테이블로 샘플 구간 결정
        const uint64_t b = prefix(key);
        auto first = samples_.begin() + radix_[b];
        auto last = samples_.begin() + radix_[b + 1];

        // [2단계] 구간 안에서 key 이상인 첫 샘플 j 찾기
        const uint64_t j = static_cast<uint64_t>(std::lower_bound(first, last, key) - samples_.begin());
        if (j == 0) return 0;  // samples[0] = min <= key <= samples[0] → key == min

        // [3단계] data[l] < key <= data[r] 를 만족하는 구간에서 탐색
        const uint64_t l = (j - 1) * step_;
        const uint64_t r = (j == samples_.size()) ? n_ - 1 : j * step_;
        return last_mile(key, l, r);
    }

    uint64_t step() const { return step_; }
    size_t sample_count() const { return samples_.size(); }
    size_t memory_bytes() const {
        return samples_.size() * sizeof(Key) + radix_.size() * sizeof(uint32_t);
    }

private:
    uint64_t prefix(Key k) const { return static_cast<uint64_t>(k - min_key_) >> shift_; }

    // 불변식: data[l] < key <= data[r], l < r
    // 결과: key 이상인 첫 위치(lower bound)가 key와 같으면 그 위치, 아니면 NOT_FOUND
    uint64_t last_mile(Key key, uint64_t l, uint64_t r) const {
        const Key* d = data_;

        // (1) 보간 예측: 두 끝 키 사이에서 key가 있을 법한 위치를 선형으로 추정
        const double frac = static_cast<double>(key - d[l]) / static_cast<double>(d[r] - d[l]);
        uint64_t p = l + 1 + static_cast<uint64_t>(frac * static_cast<double>(r - l - 1));
        if (p > r) p = r;

        // (2) 지수 탐색: 예측 위치에서 1, 2, 4, 8 ... 칸씩 넓혀 가며 key를 감싸는 구간을 찾음
        if (d[p] < key) {
            l = p;
            for (uint64_t s = 1;; s <<= 1) {
                const uint64_t q = l + s;
                if (q >= r) break;
                if (d[q] >= key) { r = q; break; }
                l = q;
            }
        } else {
            r = p;
            for (uint64_t s = 1;; s <<= 1) {
                if (r - l <= s) break;
                const uint64_t q = r - s;
                if (d[q] < key) { l = q; break; }
                r = q;
            }
        }

        // (3) 좁혀진 구간에서 이진 탐색
        while (r - l > 1) {
            const uint64_t mid = l + (r - l) / 2;
            if (d[mid] < key) l = mid;
            else r = mid;
        }
        return d[r] == key ? r : NOT_FOUND;
    }

    const Key* data_ = nullptr;
    uint64_t n_ = 0;
    uint64_t step_ = 1;
    Key min_key_{};
    Key max_key_{};
    unsigned shift_ = 0;
    std::vector<Key> samples_;
    std::vector<uint32_t> radix_;
};
