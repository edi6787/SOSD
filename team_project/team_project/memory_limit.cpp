#include "memory_limit.h"

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")

bool set_memory_limit(size_t bytes) {
    // Job 핸들은 프로세스가 끝날 때까지 열어 둔다
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) return false;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    info.ProcessMemoryLimit = bytes;

    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info)))
        return false;
    return AssignProcessToJobObject(job, GetCurrentProcess()) != 0;
}

void print_memory_usage() {
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        std::cout << "최대 커밋 메모리(제한 대상): " << pmc.PeakPagefileUsage / (1024.0 * 1024.0)
                  << " MB\n";
        std::cout << "최대 워킹셋(매핑된 파일 페이지 포함): "
                  << pmc.PeakWorkingSetSize / (1024.0 * 1024.0) << " MB\n";
    }
}

#else
#include <sys/resource.h>

bool set_memory_limit(size_t bytes) {
    // RLIMIT_DATA는 힙/익명 메모리를 제한하고, 읽기 전용 파일 매핑은 포함하지 않는다
    // (Windows Job object의 커밋 메모리 제한과 같은 기준)
    struct rlimit rl;
    rl.rlim_cur = bytes;
    rl.rlim_max = bytes;
    return setrlimit(RLIMIT_DATA, &rl) == 0;
}

void print_memory_usage() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        std::cout << "최대 RSS(매핑된 파일 페이지 포함): " << ru.ru_maxrss / 1024.0 << " MB\n";
}
#endif
