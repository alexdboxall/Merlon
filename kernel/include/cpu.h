#pragma once

#include <common.h>
#include <arch.h>
#include <spinlock.h>

struct vas;
struct thread;

struct cpu {
    struct vas* current_vas;
    struct thread* current_thread;
    platform_cpu_data_t* platform_specific;
    size_t cpu_number;
    int irql;
};

void InitBootstrapCpu(void);
void InitOtherCpu(void);

export struct cpu* GetCpu(void);
export int GetCpuCount(void);
export struct cpu* GetCpuAtIndex(int index);