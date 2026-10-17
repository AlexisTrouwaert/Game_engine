#include "moteur/process_memory.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <unistd.h>

#include <fstream>
#endif

namespace moteur {

std::size_t process_memory_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return counters.PrivateUsage;
    }
    return 0;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::size_t>(info.phys_footprint);
    }
    return 0;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::size_t pages = 0, resident = 0;
    if (statm >> pages >> resident) {
        return resident * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    }
    return 0;
#else
    return 0;
#endif
}

std::size_t process_heap_bytes() {
#if defined(_WIN32)
    // The CRT allocates from the process heap (UCRT): what its blocks in use add up to.
    HEAP_SUMMARY summary{};
    summary.cb = sizeof(summary);
    if (HeapSummary(GetProcessHeap(), 0, &summary)) {
        return summary.cbAllocated;
    }
    return 0;
#elif defined(__APPLE__)
    malloc_statistics_t statistics{};
    malloc_zone_statistics(nullptr, &statistics);
    return statistics.size_in_use;
#else
    return 0;
#endif
}

std::size_t process_peak_memory_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        return counters.PeakPagefileUsage;  // the peak of the private bytes (commit charge)
    }
    return 0;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::size_t>(info.ledger_phys_footprint_peak);
    }
    return 0;
#else
    return process_memory_bytes();
#endif
}

}  // namespace moteur
