#pragma once

// ======= Timer.h =======
// std::chrono::steady_clock 기반 시간 측정
// 만든 순간부터(또는 restart() 이후) 지난 시간을 잽니다.

#include <chrono>

class Timer
{
public:
    Timer() : start(std::chrono::steady_clock::now()) {}

    void restart() { start = std::chrono::steady_clock::now(); }

    // 지난 시간 (밀리초)
    double elapsedMs() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start).count();
    }

private:
    std::chrono::steady_clock::time_point start;
};
