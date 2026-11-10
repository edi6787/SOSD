// ======= JobLauncher.cpp =======
// 메모리 제한 실행기 (제출물 아님)
//
// 교수님 평가 환경을 재현합니다. Windows Job Object로 프로세스 메모리를 제한(기본 512MB)한 뒤
// 프로그램을 실행하고, 끝나면 최대 메모리 사용량과 종료 코드를 알려 줍니다.
//
// Job Object 동작
//   - JOB_OBJECT_LIMIT_PROCESS_MEMORY: 프로세스가 확보(commit)할 수 있는 메모리의 상한
//   - 상한을 넘는 할당은 실패합니다 (new → std::bad_alloc, malloc → NULL). 프로세스가 바로 죽지는 않습니다.
//   - 파일을 OS 캐시로 읽는 것은 프로세스 메모리에 포함되지 않습니다.
//
// 실행 순서
//   1. Job Object 생성 + 메모리 제한 설정
//   2. 대상 프로그램을 "일시 정지" 상태로 생성 (제한이 걸리기 전에 메모리를 쓰지 않도록)
//   3. Job에 넣은 뒤 실행 재개
//   4. 끝날 때까지 기다린 후 결과 출력
//
// 사용법
//   JobLauncher.exe [--limit-mb 512] <프로그램> [인자...]
//   표준 입력·출력은 대상 프로그램에 그대로 전달됩니다.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

// ======= 명령줄 만들기 =======

// 인자 하나를 따옴표로 감쌉니다 (경로에 공백·한글·괄호가 있어도 안전하게).
// 이 프로젝트의 인자에는 따옴표가 들어가지 않으므로 단순하게 처리합니다.
std::wstring quote(const std::wstring& argument)
{
    return L"\"" + argument + L"\"";
}

// 표준 입출력 핸들을 자식 프로세스가 물려받을 수 있게 합니다.
void makeInheritable(DWORD stdHandleId)
{
    HANDLE handle = GetStdHandle(stdHandleId);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
    {
        SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
}

// ======= 메인 =======

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    // 1. 인자 읽기
    int programIndex = 1;
    unsigned long long limitMB = 512;
    if (argc >= 3 && std::wstring(argv[1]) == L"--limit-mb")
    {
        limitMB = std::wcstoull(argv[2], nullptr, 10);
        programIndex = 3;
    }
    if (programIndex >= argc || limitMB == 0)
    {
        std::printf("사용법: JobLauncher.exe [--limit-mb 512] <프로그램> [인자...]\n");
        return 1;
    }

    // 프로그램 경로의 '/'는 CreateProcess가 찾지 못하므로 '\'로 바꿉니다.
    std::wstring program = argv[programIndex];
    for (wchar_t& c : program)
    {
        if (c == L'/') c = L'\\';
    }

    std::wstring commandLine = quote(program);
    for (int i = programIndex + 1; i < argc; ++i)
    {
        commandLine += L" " + quote(argv[i]);
    }

    // 2. Job Object 생성과 메모리 제한
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr)
    {
        std::printf("[JobLauncher] Job Object를 만들 수 없습니다. (오류 %lu)\n", GetLastError());
        return 1;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY |      // 프로세스 메모리 상한
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;    // 실행기가 먼저 끝나도 대상이 남지 않게
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(limitMB) * 1024 * 1024;

    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    {
        std::printf("[JobLauncher] 메모리 제한을 설정할 수 없습니다. (오류 %lu)\n", GetLastError());
        CloseHandle(job);
        return 1;
    }

    // 3. 대상 프로그램을 일시 정지 상태로 만들고 Job에 넣은 뒤 실행
    makeInheritable(STD_INPUT_HANDLE);
    makeInheritable(STD_OUTPUT_HANDLE);
    makeInheritable(STD_ERROR_HANDLE);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED, nullptr, nullptr, &startup, &process))
    {
        std::printf("[JobLauncher] 프로그램을 실행할 수 없습니다. (오류 %lu)\n", GetLastError());
        CloseHandle(job);
        return 1;
    }

    if (!AssignProcessToJobObject(job, process.hProcess))
    {
        std::printf("[JobLauncher] 프로그램을 Job에 넣을 수 없습니다. (오류 %lu)\n", GetLastError());
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        return 1;
    }
    ResumeThread(process.hThread);

    // 4. 끝날 때까지 기다린 뒤 결과 출력
    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION usage = {};
    QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &usage, sizeof(usage), nullptr);
    double peakMB = usage.PeakProcessMemoryUsed / (1024.0 * 1024.0);

    std::fflush(stdout);
    std::printf("[JobLauncher] 메모리 제한 %llu MB, 최대 사용 %.1f MB, 종료 코드 %lu (0x%08lX)\n",
                limitMB, peakMB, exitCode, exitCode);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return static_cast<int>(exitCode);
}
