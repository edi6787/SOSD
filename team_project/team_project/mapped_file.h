#pragma once
#include <cstdint>
#include <string>

// SOSD 데이터 파일을 메모리 매핑(Memory-Mapped File)으로 여는 클래스.
//
// SOSD 파일 형식: [uint64 개수 n][Key 0][Key 1]...[Key n-1]  (정렬된 키 배열)
//
// 200M개 uint32 키 = 800MB 라서 512MB 메모리 제한 안에서 통째로 vector에 올릴 수 없다.
// 대신 파일을 읽기 전용으로 매핑하면
//   - 프로세스가 '커밋'하는 메모리가 거의 없고 (Job object의 메모리 제한은 커밋 메모리 기준),
//   - 실제로 접근한 페이지(4KB)만 OS가 디스크/캐시에서 읽어 온다.
// 따라서 빌드 단계에서 파일 전체를 읽을 필요가 없고, 검색 시 접근하는 몇 개 페이지만 읽게 된다.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // 성공하면 true. 실패 시 error()에 이유가 들어간다.
    bool open(const std::string& path);
    void close();

    const uint8_t* data() const { return base_; }
    uint64_t size() const { return size_; }
    const std::string& error() const { return error_; }

private:
    const uint8_t* base_ = nullptr;
    uint64_t size_ = 0;
    std::string error_;
#ifdef _WIN32
    void* file_ = nullptr;     // HANDLE
    void* mapping_ = nullptr;  // HANDLE
#else
    int fd_ = -1;
#endif
};
