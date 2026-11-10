#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

#include "sosd_index.h"  // NOT_FOUND

// 성능 비교용 기준(baseline) 검색 방법들. 인덱스 없이 검색한다.

// (A) 파일 이진 탐색: 탐색 단계마다 seekg + read로 키 1개를 읽음 (빌드 없음)
template <class Key>
class StreamBinarySearch {
public:
    bool open(const std::string& path, uint64_t n) {
        file_.open(path, std::ios::binary);
        n_ = n;
        return file_.is_open();
    }

    uint64_t find(Key key) {
        uint64_t lo = 0, hi = n_;  // [lo, hi)에서 key 이상인 첫 위치 찾기
        while (lo < hi) {
            const uint64_t mid = lo + (hi - lo) / 2;
            if (read_at(mid) < key) lo = mid + 1;
            else hi = mid;
        }
        if (lo < n_ && read_at(lo) == key) return lo;
        return NOT_FOUND;
    }

private:
    Key read_at(uint64_t i) {
        Key v{};
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(8 + i * sizeof(Key)), std::ios::beg);
        file_.read(reinterpret_cast<char*>(&v), sizeof(Key));
        return v;
    }

    std::ifstream file_;
    uint64_t n_ = 0;
};

// (B) 메모리 매핑된 배열 전체에 std::lower_bound (빌드 없음)
template <class Key>
uint64_t plain_binary_search(const Key* data, uint64_t n, Key key) {
    const Key* it = std::lower_bound(data, data + n, key);
    if (it != data + n && *it == key) return static_cast<uint64_t>(it - data);
    return NOT_FOUND;
}
