#include "mapped_file.h"

#include <filesystem>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

MappedFile::~MappedFile() { close(); }

#ifdef _WIN32

bool MappedFile::open(const std::string& path) {
    close();

    // 한글 경로도 열 수 있도록 wide 문자열 API 사용
    std::wstring wpath = std::filesystem::path(path).wstring();

    HANDLE file = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error_ = "파일을 열 수 없습니다: " + path;
        return false;
    }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(file, &sz) || sz.QuadPart == 0) {
        CloseHandle(file);
        error_ = "파일 크기를 확인할 수 없습니다: " + path;
        return false;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        error_ = "CreateFileMapping 실패";
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        error_ = "MapViewOfFile 실패 (64비트(x64)로 빌드했는지 확인하세요)";
        return false;
    }

    file_ = file;
    mapping_ = mapping;
    base_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<uint64_t>(sz.QuadPart);
    return true;
}

void MappedFile::close() {
    if (base_) UnmapViewOfFile(base_);
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_) CloseHandle(static_cast<HANDLE>(file_));
    base_ = nullptr;
    mapping_ = nullptr;
    file_ = nullptr;
    size_ = 0;
}

#else  // POSIX (리눅스/WSL에서 테스트용)

bool MappedFile::open(const std::string& path) {
    close();

    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        error_ = "파일을 열 수 없습니다: " + path;
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        ::close(fd);
        error_ = "파일 크기를 확인할 수 없습니다: " + path;
        return false;
    }

    void* view = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        error_ = "mmap 실패";
        return false;
    }
    madvise(view, static_cast<size_t>(st.st_size), MADV_RANDOM);

    fd_ = fd;
    base_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<uint64_t>(st.st_size);
    return true;
}

void MappedFile::close() {
    if (base_) munmap(const_cast<uint8_t*>(base_), static_cast<size_t>(size_));
    if (fd_ >= 0) ::close(fd_);
    base_ = nullptr;
    fd_ = -1;
    size_ = 0;
}

#endif
