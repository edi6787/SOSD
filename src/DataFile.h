#pragma once

// ======= DataFile.h =======
// SOSD 데이터 파일을 4KB 블록 단위로 읽는 클래스
//
// 파일 형식: [키 개수: uint64][키0: uint32][키1: uint32] ... [키 n-1: uint32]
//   i번째 키의 파일 오프셋 = 8 + 4i
//
// 블록의 정의
//   파일을 처음부터 4KB씩 자른 조각입니다. 블록 b = 파일 바이트 [4096 × b, 4096 × (b + 1))
//   디스크·OS 캐시의 4KB 페이지와 경계가 같으므로 "블록 1번 읽기 = 실제 페이지 1개 읽기"입니다.
//   (헤더 8바이트 때문에 블록 0에는 키 1022개, 나머지 블록에는 키 1024개가 들어 있습니다.)
//
// 블록 기억
//   최근에 읽은 블록 2개를 기억합니다. 기억한 블록의 키를 다시 요청하면 파일을 읽지 않습니다.
//   2개인 이유: 이분 탐색의 마지막 범위가 이웃한 두 블록에 걸치면 가운데 위치가 두 블록을
//   번갈아 오가는데, 1개만 기억하면 같은 블록을 계속 다시 읽게 됩니다.
//   블록 읽기 횟수(readCount)는 실제로 파일을 읽은 횟수만 셉니다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

class DataFile
{
public:
    static const uint64_t HEADER_BYTES = 8;
    static const uint64_t KEY_BYTES = 4;
    static const uint64_t BLOCK_BYTES = 4096;
    static const int CACHE_SLOTS = 2;    // 기억하는 블록 수

    DataFile();
    ~DataFile();

    // 파일 핸들과 버퍼를 가지므로 복사하지 않습니다.
    DataFile(const DataFile&) = delete;
    DataFile& operator=(const DataFile&) = delete;

    // 파일을 열고 헤더(키 개수)를 읽습니다.
    // noBuffering = true이면 OS 파일 캐시를 거치지 않고 디스크에서 직접 읽습니다 (실험용).
    // 실패하면 false를 반환하고, 이유는 errorMessage()로 알 수 있습니다.
    bool open(const std::wstring& path, bool noBuffering);
    void close();

    uint64_t keyCount() const { return count; }

    // ======= 블록 계산 (파일을 읽지 않음) =======

    // index번째 키가 들어 있는 블록 번호
    uint64_t blockOfKey(uint64_t index) const
    {
        return (HEADER_BYTES + index * KEY_BYTES) / BLOCK_BYTES;
    }

    // block번 블록에 들어 있는 첫 키의 위치
    uint64_t firstIndexInBlock(uint64_t block) const
    {
        uint64_t startByte = block * BLOCK_BYTES;
        if (startByte <= HEADER_BYTES) return 0;
        return (startByte - HEADER_BYTES) / KEY_BYTES;
    }

    // block번 블록에 들어 있는 마지막 키의 위치 (마지막 블록은 키 개수에 맞춰 자름)
    uint64_t lastIndexInBlock(uint64_t block) const
    {
        uint64_t endByte = (block + 1) * BLOCK_BYTES;   // 다음 블록의 시작 바이트
        uint64_t index = (endByte - HEADER_BYTES) / KEY_BYTES - 1;
        return (index < count) ? index : count - 1;
    }

    // index번째 키를 반환합니다. 그 키가 든 블록을 기억하고 있지 않으면 파일에서 읽습니다.
    // 읽기에 실패하면 std::runtime_error를 던집니다.
    uint32_t keyAt(uint64_t index);

    // 실제로 파일에서 블록을 읽은 횟수
    uint64_t readCount() const { return reads; }
    void resetReadCount() { reads = 0; }

    // 기억한 블록을 버립니다. 다음 요청은 반드시 파일에서 읽습니다.
    void clearCache();

    const std::string& errorMessage() const { return error; }

private:
    // 기억하는 블록 하나
    struct CacheSlot
    {
        bool valid;
        uint64_t block;      // 블록 번호
        uint64_t bytes;      // 실제로 읽은 바이트 수 (마지막 블록은 4KB보다 작을 수 있음)
        uint8_t* data;       // 블록 내용 (buffer 안의 4KB 구간)
    };

    // block번 블록이 든 슬롯을 반환합니다. 기억하고 있지 않으면 파일에서 읽습니다.
    // 읽기에 실패하면 nullptr
    const CacheSlot* loadBlock(uint64_t block);

    HANDLE handle;
    uint64_t fileSize;
    uint64_t count;          // 키 개수 (헤더 값)

    uint8_t* buffer;         // 슬롯 수 × 4KB. NO_BUFFERING 요건 때문에 VirtualAlloc으로 4KB 정렬
    CacheSlot slots[CACHE_SLOTS];
    int lastUsedSlot;        // 가장 최근에 쓴 슬롯 (새 블록은 다른 슬롯에 넣음)

    uint64_t reads;
    std::string error;
};
