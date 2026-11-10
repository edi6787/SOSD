#pragma once
#include <cstddef>

// 현재 프로세스의 메모리 사용량을 bytes로 제한한다.
//  - Windows: Job object (JOB_OBJECT_LIMIT_PROCESS_MEMORY, 커밋 메모리 기준)
//  - Linux  : RLIMIT_DATA (테스트용)
bool set_memory_limit(size_t bytes);

// 최대 메모리 사용량을 출력한다 (보고서용)
void print_memory_usage();
