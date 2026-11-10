// ======= DataFile.cpp =======

#include "DataFile.h"

#include <cstring>
#include <stdexcept>

// ======= 생성·소멸 =======

DataFile::DataFile()
    : handle(INVALID_HANDLE_VALUE), fileSize(0), count(0),
      buffer(nullptr), slots(), lastUsedSlot(0), reads(0)
{
}

DataFile::~DataFile()
{
    close();
}

// ======= 열기·닫기 =======

bool DataFile::open(const std::wstring& path, bool noBuffering)
{
    close();

    DWORD flags = FILE_FLAG_RANDOM_ACCESS;
    if (noBuffering)
    {
        flags |= FILE_FLAG_NO_BUFFERING;
    }

    handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error = "파일을 열 수 없습니다.";
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size))
    {
        error = "파일 크기를 읽을 수 없습니다.";
        close();
        return false;
    }
    fileSize = static_cast<uint64_t>(size.QuadPart);

    // FILE_FLAG_NO_BUFFERING은 버퍼 주소가 섹터 크기로 정렬되어야 합니다.
    // VirtualAlloc은 페이지(4KB) 단위로 정렬된 메모리를 주고, 슬롯마다 4KB씩 나눠 쓰므로 모두 정렬됩니다.
    buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, BLOCK_BYTES * CACHE_SLOTS,
                                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (buffer == nullptr)
    {
        error = "블록 버퍼를 할당할 수 없습니다.";
        close();
        return false;
    }
    for (int s = 0; s < CACHE_SLOTS; ++s)
    {
        slots[s].valid = false;
        slots[s].data = buffer + BLOCK_BYTES * s;
    }

    // 헤더(키 개수)는 블록 0의 앞 8바이트에 있습니다.
    const CacheSlot* first = loadBlock(0);
    if (first == nullptr || first->bytes < HEADER_BYTES)
    {
        error = "헤더를 읽을 수 없습니다.";
        close();
        return false;
    }
    std::memcpy(&count, first->data, sizeof(count));

    // 파일 크기와 헤더가 맞는지 확인합니다.
    if (count == 0 || fileSize != HEADER_BYTES + count * KEY_BYTES)
    {
        error = "파일 크기와 헤더의 키 개수가 맞지 않습니다.";
        close();
        return false;
    }

    return true;
}

void DataFile::close()
{
    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    if (buffer != nullptr)
    {
        VirtualFree(buffer, 0, MEM_RELEASE);
        buffer = nullptr;
    }
    for (int s = 0; s < CACHE_SLOTS; ++s)
    {
        slots[s].valid = false;
        slots[s].data = nullptr;
    }
    fileSize = 0;
    count = 0;
}

// ======= 키 읽기 =======

uint32_t DataFile::keyAt(uint64_t index)
{
    if (index >= count)
    {
        throw std::runtime_error("키 위치가 범위를 벗어났습니다.");
    }

    uint64_t offset = HEADER_BYTES + index * KEY_BYTES;
    uint64_t block = offset / BLOCK_BYTES;
    uint64_t offsetInBlock = offset % BLOCK_BYTES;

    const CacheSlot* slot = loadBlock(block);
    if (slot == nullptr || offsetInBlock + KEY_BYTES > slot->bytes)
    {
        throw std::runtime_error("블록을 읽을 수 없습니다.");
    }

    uint32_t key;
    std::memcpy(&key, slot->data + offsetInBlock, sizeof(key));
    return key;
}

void DataFile::clearCache()
{
    for (int s = 0; s < CACHE_SLOTS; ++s)
    {
        slots[s].valid = false;
    }
}

// ======= 블록 읽기 =======

const DataFile::CacheSlot* DataFile::loadBlock(uint64_t block)
{
    // 1. 이미 기억하고 있으면 그대로 사용
    for (int s = 0; s < CACHE_SLOTS; ++s)
    {
        if (slots[s].valid && slots[s].block == block)
        {
            lastUsedSlot = s;
            return &slots[s];
        }
    }

    // 2. 없으면 가장 최근에 쓰지 않은 슬롯에 읽어 옵니다.
    int target = (lastUsedSlot + 1) % CACHE_SLOTS;
    CacheSlot& slot = slots[target];

    // 블록 시작 오프셋은 항상 4KB 배수라서 NO_BUFFERING의 정렬 요건을 만족합니다.
    uint64_t offset = block * BLOCK_BYTES;
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(handle, slot.data, static_cast<DWORD>(BLOCK_BYTES), &bytesRead, &overlapped);
    ++reads;

    if (!ok || bytesRead == 0)
    {
        slot.valid = false;
        return nullptr;
    }

    slot.valid = true;
    slot.block = block;
    slot.bytes = bytesRead;
    lastUsedSlot = target;
    return &slot;
}
