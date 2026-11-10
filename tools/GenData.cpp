// ======= GenData.cpp =======
// 테스트용 예시 데이터 생성기 (제출물 아님)
//
// 실제 SOSD 데이터를 받기 전에 개발·테스트용으로 같은 형식의 파일을 만듭니다.
//   파일 형식: [키 개수: uint64, 8바이트][키0: uint32][키1: uint32] ... [키 n-1: uint32]
//   키는 오름차순, 중복 없음
//
// 만드는 방법 (선택 표본 추출, Knuth Algorithm S)
//   "후보 범위 [start, start + universe)"의 값을 작은 것부터 하나씩 보면서
//   (아직 더 뽑아야 할 개수) / (아직 남은 후보 개수) 확률로 뽑습니다.
//   - 정확히 n개가 뽑히고, 모든 조합이 같은 확률로 나옵니다 (균등 분포).
//   - 작은 값부터 차례로 뽑으므로 정렬이 필요 없고, 메모리에 키를 쌓지 않습니다.
//
//   dense : 후보 범위 = n × 1.01 → 연속 정수에서 약 1%가 빠진 형태
//   sparse: 후보 범위 = uint32 전체(2^32) → 평균 간격 약 21 (n = 2억 기준)
//
// 치우친 분포 시험 파일 (skewed)
//   보간 탐색의 폴백이 실제로 작동하는지 시험하기 위한 파일입니다. 과제 데이터셋이 아닙니다.
//   키 i = 1000 + i + (i / (n - 1))^8 × 40억 → 앞쪽에 키가 몰리고 뒤로 갈수록 간격이 급격히 커짐
//   직선 예측이 크게 빗나가므로, 폴백이 없으면 읽기 횟수가 매우 커집니다.
//
// 사용법
//   GenData.exe <출력 폴더>          → 균등 분포 파일 4개 (1만 개 2개, 2억 개 2개)
//   GenData.exe <출력 폴더> skewed   → 치우친 분포 시험 파일 1개 (100만 개)

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ======= 난수 생성기 =======

// SplitMix64: 짧고 빠른 64비트 난수 생성기
// 후보 값마다 난수를 하나씩 쓰므로 (sparse는 약 43억 번) 빠른 생성기가 필요합니다.
class SplitMix64
{
public:
    explicit SplitMix64(uint64_t seed) : state(seed) {}

    uint64_t next()
    {
        state += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // [0, 1) 범위의 double
    double nextDouble()
    {
        return (next() >> 11) * (1.0 / 9007199254740992.0);   // 2^53
    }

private:
    uint64_t state;
};

// ======= 데이터 파일 생성 =======

struct GenResult
{
    uint64_t count;
    uint32_t firstKey;
    uint32_t lastKey;
};

// 후보 범위 [start, start + universe)에서 n개를 균등하게 뽑아 정렬된 순서로 파일에 씁니다.
// start + universe는 2^32 이하여야 합니다 (uint32 범위).
bool generateFile(const std::string& path, uint64_t n,
                  uint64_t start, uint64_t universe, uint64_t seed,
                  GenResult& result)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || fp == nullptr)
    {
        std::printf("  [오류] 파일을 열 수 없습니다: %s\n", path.c_str());
        return false;
    }

    // 헤더: 키 개수 (uint64)
    std::fwrite(&n, sizeof(n), 1, fp);

    // 키를 버퍼에 모았다가 한꺼번에 씁니다 (1M개 = 4MB)
    const size_t BUFFER_KEYS = 1 << 20;
    std::vector<uint32_t> buffer;
    buffer.reserve(BUFFER_KEYS);

    SplitMix64 rng(seed);
    uint64_t needed = n;            // 아직 더 뽑아야 할 개수
    uint64_t remaining = universe;  // 아직 남은 후보 개수
    bool isFirst = true;

    for (uint64_t value = start; needed > 0; ++value, --remaining)
    {
        // (needed / remaining) 확률로 선택. needed == remaining이면 항상 선택됩니다.
        if (rng.nextDouble() * static_cast<double>(remaining) < static_cast<double>(needed))
        {
            uint32_t key = static_cast<uint32_t>(value);
            buffer.push_back(key);
            --needed;

            if (isFirst)
            {
                result.firstKey = key;
                isFirst = false;
            }
            result.lastKey = key;

            if (buffer.size() == BUFFER_KEYS)
            {
                std::fwrite(buffer.data(), sizeof(uint32_t), buffer.size(), fp);
                buffer.clear();
            }
        }
    }

    if (!buffer.empty())
    {
        std::fwrite(buffer.data(), sizeof(uint32_t), buffer.size(), fp);
    }

    std::fclose(fp);
    result.count = n;
    return true;
}

// 치우친 분포 시험 파일을 만듭니다. (설명은 파일 맨 위 참고)
bool generateSkewedFile(const std::string& path, uint64_t n, GenResult& result)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || fp == nullptr)
    {
        std::printf("  [오류] 파일을 열 수 없습니다: %s\n", path.c_str());
        return false;
    }

    std::fwrite(&n, sizeof(n), 1, fp);
    std::vector<uint32_t> keys(n);
    for (uint64_t i = 0; i < n; ++i)
    {
        // i 항이 1씩 늘고 거듭제곱 항은 줄지 않으므로 키는 항상 증가합니다 (중복 없음).
        double t = static_cast<double>(i) / static_cast<double>(n - 1);
        double curve = std::floor(std::pow(t, 8.0) * 4.0e9);
        keys[i] = static_cast<uint32_t>(1000 + i + static_cast<uint64_t>(curve));
    }
    std::fwrite(keys.data(), sizeof(uint32_t), keys.size(), fp);
    std::fclose(fp);

    result.count = n;
    result.firstKey = keys.front();
    result.lastKey = keys.back();
    return true;
}

// ======= 생성할 파일 목록 =======

struct FileSpec
{
    const char* name;
    uint64_t n;
    uint64_t start;
    uint64_t universe;
    uint64_t seed;
};

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf("사용법: GenData.exe <출력 폴더> [skewed]\n");
        return 1;
    }
    std::string outDir = argv[1];

    // 치우친 분포 시험 파일만 만들기
    if (argc >= 3 && std::string(argv[2]) == "skewed")
    {
        const char* name = "gen_skewed_1M_uint32";
        std::printf("생성 중: %s\n", name);
        GenResult result = {};
        if (!generateSkewedFile(outDir + "/" + name, 1000000ULL, result))
        {
            return 1;
        }
        std::printf("  키 개수 %llu, 첫 키 %u, 마지막 키 %u\n",
                    static_cast<unsigned long long>(result.count), result.firstKey, result.lastKey);
        std::printf("완료\n");
        return 0;
    }

    const uint64_t SMALL_N = 10000ULL;        // 1만 개
    const uint64_t LARGE_N = 200000000ULL;    // 2억 개
    const uint64_t UINT32_RANGE = 1ULL << 32; // uint32 전체 범위

    // dense는 1,000,000부터 시작해 "첫 키보다 작은 값" 테스트가 가능하게 합니다.
    const uint64_t DENSE_START = 1000000ULL;

    // 실제 SOSD 파일과 헷갈리지 않도록 이름 앞에 gen_을 붙입니다.
    const FileSpec specs[] = {
        { "gen_uniform_dense_10K_uint32",   SMALL_N, DENSE_START, SMALL_N + SMALL_N / 100, 1 },
        { "gen_uniform_sparse_10K_uint32",  SMALL_N, 0,           UINT32_RANGE,            2 },
        { "gen_uniform_dense_200M_uint32",  LARGE_N, DENSE_START, LARGE_N + LARGE_N / 100, 3 },
        { "gen_uniform_sparse_200M_uint32", LARGE_N, 0,           UINT32_RANGE,            4 },
    };

    for (const FileSpec& spec : specs)
    {
        std::string path = outDir + "/" + spec.name;
        std::printf("생성 중: %s\n", spec.name);

        auto t0 = std::chrono::steady_clock::now();
        GenResult result = {};
        if (!generateFile(path, spec.n, spec.start, spec.universe, spec.seed, result))
        {
            return 1;
        }
        auto t1 = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();

        std::printf("  키 개수 %llu, 첫 키 %u, 마지막 키 %u, 파일 크기 %llu바이트, %.1f초\n",
                    static_cast<unsigned long long>(result.count),
                    result.firstKey, result.lastKey,
                    static_cast<unsigned long long>(8 + result.count * 4), sec);
    }

    std::printf("완료\n");
    return 0;
}
