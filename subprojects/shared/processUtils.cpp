#include "processUtils.hpp"
#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

ProcessUsage ProcessUtils::get_process_usage() {
    static ULONGLONG last_time = 0, last_sys_time = 0, last_user_time = 0;
    FILETIME sys_idle, sys_kernel, sys_user;
    FILETIME proc_creation, proc_exit, proc_kernel, proc_user;
    GetSystemTimes(&sys_idle, &sys_kernel, &sys_user);

    HANDLE hProcess = GetCurrentProcess();
    GetProcessTimes(hProcess, &proc_creation, &proc_exit, &proc_kernel, &proc_user);

    ULONGLONG sys_time = (((ULONGLONG)sys_kernel.dwHighDateTime) << 32) | sys_kernel.dwLowDateTime;
    ULONGLONG user_time = (((ULONGLONG)sys_user.dwHighDateTime) << 32) | sys_user.dwLowDateTime;
    ULONGLONG proc_sys_time = (((ULONGLONG)proc_kernel.dwHighDateTime) << 32) | proc_kernel.dwLowDateTime;
    ULONGLONG proc_user_time = (((ULONGLONG)proc_user.dwHighDateTime) << 32) | proc_user.dwLowDateTime;

    double cpu_percent = 0.0;
    ULONGLONG now = sys_time + user_time;
    ULONGLONG proc_now = proc_sys_time + proc_user_time;
    if (last_time != 0) {
        cpu_percent = double(proc_now - last_user_time - last_sys_time) / double(now - last_time) * 100.0;
    }
    last_time = now;
    last_sys_time = proc_sys_time;
    last_user_time = proc_user_time;

    PROCESS_MEMORY_COUNTERS pmc;
    SIZE_T mem = 0;
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        mem = pmc.WorkingSetSize;
    }
    return {cpu_percent, mem};
}

std::filesystem::path ProcessUtils::get_executable_path_impl() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer);
}

#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#include <limits.h>

ProcessUsage ProcessUtils::get_process_usage() {
    static unsigned long long last_total_time = 0, last_proc_time = 0;

    std::ifstream stat_file("/proc/stat");
    std::string cpu;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    stat_file >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    unsigned long long total_time = user + nice + system + idle + iowait + irq + softirq + steal;

    std::ifstream proc_file("/proc/self/stat");
    std::string ignore;
    unsigned long utime, stime;
    for (int i = 0; i < 13; ++i) proc_file >> ignore;
    proc_file >> utime >> stime;
    unsigned long long proc_time = utime + stime;

    double cpu_percent = 0.0;
    if (last_total_time != 0) {
        cpu_percent = double(proc_time - last_proc_time) / double(total_time - last_total_time) * 100.0;
    }
    last_total_time = total_time;
    last_proc_time = proc_time;

    std::ifstream statm_file("/proc/self/statm");
    unsigned long size, resident;
    statm_file >> size >> resident;
    std::size_t mem = resident * sysconf(_SC_PAGESIZE);

    return {cpu_percent, mem};
}

std::filesystem::path ProcessUtils::get_executable_path_impl() {
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::filesystem::path(buffer);
    }
    return std::filesystem::current_path();
}

#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <unistd.h>

ProcessUsage ProcessUtils::get_process_usage() {
    static uint64_t last_wall_time = 0, last_proc_time = 0;

    // Get process CPU time (in microseconds)
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    uint64_t proc_time = usage.ru_utime.tv_sec * 1000000ULL + usage.ru_utime.tv_usec +
                         usage.ru_stime.tv_sec * 1000000ULL + usage.ru_stime.tv_usec;

    // Get wall clock time (in microseconds)
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t wall_time = tv.tv_sec * 1000000ULL + tv.tv_usec;

    double cpu_percent = 0.0;
    if (last_wall_time != 0) {
        // Calculate CPU usage as: (delta_cpu_time / delta_wall_time) * 100
        // This gives the percentage of one CPU core used
        uint64_t delta_proc = proc_time - last_proc_time;
        uint64_t delta_wall = wall_time - last_wall_time;
        if (delta_wall > 0) {
            cpu_percent = (double(delta_proc) / double(delta_wall)) * 100.0;
        }
    }
    last_wall_time = wall_time;
    last_proc_time = proc_time;

    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
    std::size_t mem = t_info.resident_size;

    return {cpu_percent, mem};
}

std::filesystem::path ProcessUtils::get_executable_path_impl() {
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        return std::filesystem::path(buffer);
    }
    return std::filesystem::current_path();
}

#else
ProcessUsage ProcessUtils::get_process_usage() {
    return {0.0, 0};
}

std::filesystem::path ProcessUtils::get_executable_path_impl() {
    return std::filesystem::current_path();
}
#endif

// Common implementations (platform-independent)
std::filesystem::path ProcessUtils::get_executable_path() {
    static std::filesystem::path cached_path = get_executable_path_impl();
    return cached_path;
}

std::filesystem::path ProcessUtils::get_executable_dir() {
    return get_executable_path().parent_path();
}

// Cross-platform thread naming
void ProcessUtils::set_current_thread_name(const std::string& name) {
#if defined(_WIN32)
    std::wstring wname(name.begin(), name.end());
    // Best-effort: ignore failures on older Windows versions
    ::SetThreadDescription(::GetCurrentThread(), wname.c_str());
#elif defined(__APPLE__)
    // macOS supports setting name for current thread only
    pthread_setname_np(name.c_str());
#elif defined(__linux__)
    // Linux requires thread handle and limits name to 16 chars including NUL
    pthread_setname_np(pthread_self(), name.c_str());
#else
    (void)name; // no-op on unsupported platforms
#endif
}