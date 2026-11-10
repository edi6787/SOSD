#pragma once
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "sosd_index.h"  // NOT_FOUND

// ─────────────────────────────────────────────────────────────────────────────
// InterpolationSearch : 빌드가 거의 없는 보간 탐색 + 적응형(adaptive) 위치 캐시 (기본 검색 방법)
//
// 과제 점수 = 빌드 시간 + 검색 시간 이므로, 쿼리 수가 많지 않으면 빌드 비용이 지배적이다.
// SOSD uniform 데이터(dense, sparse 모두)는 키가 균등하게 분포해 있으므로
//   위치 ≈ (key - min) / (max - min) * n
// 으로 바로 예측할 수 있어 인덱스 없이도 몇 번의 접근만에 찾을 수 있다.
//
// 검색 시간의 대부분은 '처음 접근하는 4KB 페이지'를 OS가 가져오는 비용(page fault)이다.
// 전체 min/max로 한 예측은 정답에서 수천 칸(여러 페이지) 떨어지므로 페이지를 2~3개 건드린다.
//
// [적응형 위치 캐시]
//   검색하면서 '이미 읽은' (위치, 키) 쌍을 키의 상위 비트 기준 버킷에 기록해 두고,
//   다음 쿼리는 근처 버킷에 기록된 점들을 보간의 양 끝점으로 쓴다.
//   - 빌드 때는 아무것도 읽지 않는다 (버킷 배열은 calloc → OS가 0 페이지를 지연 할당).
//   - 캐시를 채우려고 데이터를 추가로 읽는 일이 없다 → 쿼리가 적어도 손해가 없다.
//   - 쿼리가 쌓일수록 양 끝점이 가까워져 예측이 정답 페이지에 바로 떨어진다.
//   (데이터베이스의 adaptive indexing / database cracking 과 같은 발상)
//
// [구간 내부 탐색] (참고: Van Sandt et al., "Efficiently Searching In-Memory Sorted Arrays:
//                  Revenge of the Interpolation Search", SIGMOD 2019)
//   1) 가드(guard) 프로브: 예측 위치 p를 확인한 뒤 p±GUARD도 확인해서 구간 양쪽을 함께 좁힌다.
//   2) 이진 탐색 fallback: 보간 한 번에 구간이 절반 이하로 줄지 않으면 다음 단계는 이진 탐색
//      → 분포가 치우친 데이터에서도 최악 O(log n) 보장.
//   3) 구간이 작아지면 이진 탐색으로 마무리.
// ─────────────────────────────────────────────────────────────────────────────
template <class Key>
class InterpolationSearch {
public:
    // bucket_bits: 버킷 수 = 최대 2^bucket_bits 개
    void build(const Key* data, uint64_t n, unsigned bucket_bits = 12) {
        data_ = data;
        n_ = n;
        points_.reset();
        if (n_ == 0) return;
        min_key_ = data_[0];
        max_key_ = data_[n_ - 1];

        const uint64_t range = static_cast<uint64_t>(max_key_ - min_key_);
        const unsigned range_bits = static_cast<unsigned>(std::bit_width(range));
        if (bucket_bits == 0) bucket_bits = 1;  // shift가 64가 되지 않도록
        const unsigned bits = bucket_bits < range_bits ? bucket_bits : range_bits;
        shift_ = range_bits - bits;
        buckets_ = (range >> shift_) + 1;
        points_.reset(static_cast<Point*>(std::calloc(buckets_, sizeof(Point))));
    }

    uint64_t find(Key key) {
        if (n_ == 0 || key < min_key_ || key > max_key_) return NOT_FOUND;
        if (key == min_key_) return 0;  // 첫 위치

        // (1) 캐시에서 양 끝점 찾기. 불변식: d[l] = kl < key <= kr = d[r]
        uint64_t l = 0, r = n_ - 1;
        Key kl = min_key_, kr = max_key_;
        const uint64_t b = bucket(key);
        if (points_) {
            for (uint64_t i = 0; i <= SCAN && i <= b; ++i) {  // 왼쪽: key보다 작은 점
                const Point& p = points_[b - i];
                if (p.pos1 && p.key < key) { l = p.pos1 - 1; kl = p.key; break; }
            }
            for (uint64_t i = 0; i <= SCAN && b + i < buckets_; ++i) {  // 오른쪽: key 이상인 점
                const Point& p = points_[b + i];
                if (p.pos1 && p.key >= key) { r = p.pos1 - 1; kr = p.key; break; }
            }
        }

        // (2) 보간 + 가드 + 이진 탐색
        const Key* d = data_;
        bool use_binary = false;
        while (r - l > FINISH) {
            const uint64_t width = r - l;
            uint64_t p;
            if (use_binary) {
                p = l + width / 2;
            } else {
                const double f = static_cast<double>(key - kl) / static_cast<double>(kr - kl);
                p = l + 1 + static_cast<uint64_t>(f * static_cast<double>(width - 1));
                if (p >= r) p = r - 1;
            }

            const Key kp = d[p];
            if (kp < key) {
                l = p; kl = kp;
                if (!use_binary && p + GUARD < r) {  // 가드: 오른쪽 경계도 당겨 보기
                    const Key kg = d[p + GUARD];
                    if (kg >= key) { r = p + GUARD; kr = kg; }
                    else { l = p + GUARD; kl = kg; }
                }
            } else {
                r = p; kr = kp;
                if (!use_binary && p > l + GUARD) {  // 가드: 왼쪽 경계도 당겨 보기
                    const Key kg = d[p - GUARD];
                    if (kg < key) { l = p - GUARD; kl = kg; }
                    else { r = p - GUARD; kr = kg; }
                }
            }
            // 보간이 구간을 절반 이하로 못 줄였으면 다음 단계는 이진 탐색
            use_binary = !use_binary && (r - l) * 2 > width;
        }
        while (r - l > 1) {
            const uint64_t mid = l + (r - l) / 2;
            const Key km = d[mid];
            if (km < key) { l = mid; kl = km; }
            else { r = mid; kr = km; }
        }

        // (3) 이번에 읽은 양 끝점을 캐시에 기록 (추가로 읽는 데이터 없음)
        if (points_) {
            if (l > 0) points_[bucket(kl)] = {l + 1, kl};
            points_[bucket(kr)] = {r + 1, kr};
        }
        return kr == key ? r : NOT_FOUND;
    }

    size_t memory_bytes() const { return points_ ? buckets_ * sizeof(Point) : 0; }

private:
    static constexpr uint64_t GUARD = 32;   // 32개 * 4B = 128B, 같은 페이지 근처
    static constexpr uint64_t FINISH = 64;  // 이 이하이면 이진 탐색으로 마무리
    static constexpr uint64_t SCAN = 2;     // 끝점을 찾을 때 살펴볼 이웃 버킷 수

    struct Point {
        uint64_t pos1;  // 위치 + 1 (0이면 빈 칸)
        Key key;
    };
    struct FreeDeleter {
        void operator()(Point* p) const { std::free(p); }
    };

    uint64_t bucket(Key k) const { return static_cast<uint64_t>(k - min_key_) >> shift_; }

    const Key* data_ = nullptr;
    uint64_t n_ = 0;
    Key min_key_{};
    Key max_key_{};
    unsigned shift_ = 0;
    uint64_t buckets_ = 0;
    std::unique_ptr<Point[], FreeDeleter> points_;  // 버킷마다 최근에 읽은 (위치, 키) 하나
};
