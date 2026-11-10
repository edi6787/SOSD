// ======= DataCheck.cpp =======
// 0단계 데이터 확인 도구 (제출물 아님)
//
// SOSD 형식 파일을 처음부터 끝까지 조금씩(4MB) 나눠 읽으며 아래 항목을 확인합니다.
// 파일 전체를 메모리에 올리지 않습니다.
//   1. 파일 크기와 헤더의 키 개수가 맞는지: 파일 크기 == 8 + 키 개수 × 4
//   2. 첫 키, 마지막 키
//   3. 오름차순인지, 중복 키가 있는지 (개수, 가장 긴 중복 묶음)
//   4. 키 사이 간격 (최소 / 평균 / 최대), 빈틈 없는 완전 연속인지
//   5. 보간 예측 오차: 첫 키~마지막 키를 잇는 직선으로 위치를 예측했을 때
//      실제 위치와 최대 몇 칸(몇 블록) 차이 나는지 → 2단계 보간 탐색의 예상 성능
//
// 사용법
//   DataCheck.exe <데이터 파일> [<데이터 파일> ...]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

// ======= 설정 =======

const size_t   CHUNK_KEYS = 1 << 20;   // 한 번에 읽는 키 수 (1M개 = 4MB)
const uint64_t HEADER_BYTES = 8;       // 헤더: 키 개수 (uint64)
const uint64_t BLOCK_KEYS = 1024;      // 블록 하나 = 4KB = 키 1024개

// ======= 확인 결과 =======

struct CheckResult
{
    uint64_t fileSize = 0;
    uint64_t headerCount = 0;
    uint64_t keysRead = 0;

    uint32_t firstKey = 0;
    uint32_t lastKey = 0;

    uint64_t descendingCount = 0;    // 앞 키보다 작은 키가 나온 횟수 (0이어야 정렬됨)
    uint64_t duplicateCount = 0;     // 앞 키와 같은 키의 수
    uint64_t longestDuplicateRun = 1;// 같은 키가 연속으로 나온 가장 긴 길이

    uint64_t minGap = UINT64_MAX;    // 인접한 두 키의 차이
    uint64_t maxGap = 0;
    uint64_t gapsLargerThanOne = 0;  // 간격이 2 이상인 곳 수 (0이면 완전 연속)

    double maxInterpolationError = 0;     // |실제 위치 - 예측 위치|의 최댓값 (칸)
    uint64_t maxErrorIndex = 0;           // 최대 오차가 나온 위치
};

// ======= 파일 읽기 =======

// 헤더와 첫 키, 마지막 키만 먼저 읽습니다. (보간 예측 오차 계산에 필요)
bool readEnds(FILE* fp, CheckResult& r)
{
    if (std::fread(&r.headerCount, sizeof(uint64_t), 1, fp) != 1) return false;
    if (r.headerCount == 0) return false;

    if (std::fread(&r.firstKey, sizeof(uint32_t), 1, fp) != 1) return false;

    long long lastOffset = static_cast<long long>(HEADER_BYTES + (r.headerCount - 1) * 4);
    if (_fseeki64(fp, lastOffset, SEEK_SET) != 0) return false;
    if (std::fread(&r.lastKey, sizeof(uint32_t), 1, fp) != 1) return false;

    // 다시 첫 키 위치로
    return _fseeki64(fp, static_cast<long long>(HEADER_BYTES), SEEK_SET) == 0;
}

// 파일 전체를 4MB씩 읽으며 정렬·중복·간격·보간 오차를 확인합니다.
void scanKeys(FILE* fp, CheckResult& r)
{
    std::vector<uint32_t> chunk(CHUNK_KEYS);

    // 보간 예측: 위치 ≈ (키 - 첫 키) × (n - 1) / (마지막 키 - 첫 키)
    // uint32 차이 × 2억은 32비트를 넘으므로 double로 계산합니다.
    double keyRange = static_cast<double>(r.lastKey) - static_cast<double>(r.firstKey);
    double slope = (keyRange > 0) ? static_cast<double>(r.headerCount - 1) / keyRange : 0.0;

    uint64_t index = 0;          // 지금 보고 있는 키의 위치
    uint32_t prevKey = 0;
    uint64_t currentRun = 1;
    uint64_t gapSum = 0;

    while (true)
    {
        size_t got = std::fread(chunk.data(), sizeof(uint32_t), CHUNK_KEYS, fp);
        if (got == 0) break;

        for (size_t j = 0; j < got; ++j, ++index)
        {
            uint32_t key = chunk[j];

            // 보간 예측 오차
            double predicted = (static_cast<double>(key) - static_cast<double>(r.firstKey)) * slope;
            double error = std::fabs(static_cast<double>(index) - predicted);
            if (error > r.maxInterpolationError)
            {
                r.maxInterpolationError = error;
                r.maxErrorIndex = index;
            }

            if (index == 0)
            {
                prevKey = key;
                continue;
            }

            // 정렬·중복
            if (key < prevKey)
            {
                ++r.descendingCount;
            }
            else if (key == prevKey)
            {
                ++r.duplicateCount;
                ++currentRun;
                if (currentRun > r.longestDuplicateRun) r.longestDuplicateRun = currentRun;
            }
            else
            {
                currentRun = 1;
            }

            // 간격 (오름차순인 곳만)
            if (key >= prevKey)
            {
                uint64_t gap = static_cast<uint64_t>(key) - prevKey;
                gapSum += gap;
                if (gap < r.minGap) r.minGap = gap;
                if (gap > r.maxGap) r.maxGap = gap;
                if (gap > 1) ++r.gapsLargerThanOne;
            }

            prevKey = key;
        }
    }

    r.keysRead = index;
}

// ======= 결과 출력 =======

void printResult(const char* path, const CheckResult& r)
{
    uint64_t expectedSize = HEADER_BYTES + r.headerCount * 4;
    bool sizeOk = (r.fileSize == expectedSize) && (r.keysRead == r.headerCount);
    bool sorted = (r.descendingCount == 0);
    bool contiguous = sorted && r.duplicateCount == 0 && r.gapsLargerThanOne == 0;
    double meanGap = (r.keysRead > 1)
        ? (static_cast<double>(r.lastKey) - r.firstKey) / static_cast<double>(r.keysRead - 1)
        : 0.0;

    std::printf("파일: %s\n", path);
    std::printf("  [크기]   파일 크기 %llu바이트, 헤더 키 개수 %llu, 읽은 키 %llu → %s\n",
                (unsigned long long)r.fileSize, (unsigned long long)r.headerCount,
                (unsigned long long)r.keysRead, sizeOk ? "일치" : "불일치");
    std::printf("  [범위]   첫 키 %u, 마지막 키 %u\n", r.firstKey, r.lastKey);
    std::printf("  [정렬]   %s (역순 %llu곳)\n",
                sorted ? "오름차순" : "정렬 안 됨", (unsigned long long)r.descendingCount);
    std::printf("  [중복]   중복 키 %llu개, 가장 긴 중복 묶음 %llu개\n",
                (unsigned long long)r.duplicateCount, (unsigned long long)r.longestDuplicateRun);
    std::printf("  [간격]   최소 %llu / 평균 %.2f / 최대 %llu, 간격 2 이상 %llu곳 → %s\n",
                (unsigned long long)r.minGap, meanGap, (unsigned long long)r.maxGap,
                (unsigned long long)r.gapsLargerThanOne, contiguous ? "완전 연속" : "완전 연속 아님");
    std::printf("  [보간]   최대 예측 오차 %.1f칸 (약 %.2f블록, 위치 %llu)\n",
                r.maxInterpolationError, r.maxInterpolationError / BLOCK_KEYS,
                (unsigned long long)r.maxErrorIndex);
}

// ======= 메인 =======

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf("사용법: DataCheck.exe <데이터 파일> [<데이터 파일> ...]\n");
        return 1;
    }

    for (int i = 1; i < argc; ++i)
    {
        const char* path = argv[i];
        CheckResult r;

        std::error_code ec;
        r.fileSize = std::filesystem::file_size(path, ec);
        FILE* fp = nullptr;
        if (ec || fopen_s(&fp, path, "rb") != 0 || fp == nullptr)
        {
            std::printf("파일: %s\n  [오류] 파일을 열 수 없습니다.\n\n", path);
            continue;
        }

        if (!readEnds(fp, r))
        {
            std::printf("파일: %s\n  [오류] 헤더 또는 첫·마지막 키를 읽을 수 없습니다.\n\n", path);
            std::fclose(fp);
            continue;
        }

        scanKeys(fp, r);
        std::fclose(fp);

        printResult(path, r);
        std::printf("\n");
    }

    return 0;
}
