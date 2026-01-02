#pragma once
#include <cstddef>
#include <utility>
#include <filesystem>
#include <thread>
#include <sstream>
#include <string>

// Platform-specific includes
#ifdef _WIN32
    #ifndef NOMINMAX  // tell windows NOT to define min and max macros. Then we can use std::min/std::max safely.
    #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__linux__)
    #include <sys/syscall.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <pthread.h>
#endif

struct ProcessUsage {
    double cpu_percent;   // CPU usage as a percentage (0.0 - 100.0)
    std::size_t memory_bytes; // Resident memory in bytes
};

class ProcessUtils {
public:
    // Process usage functionality
    static ProcessUsage get_process_usage();
    
    // Process path functionality
    static std::filesystem::path get_executable_path();
    static std::filesystem::path get_executable_dir();

    // Get the native thread ID that appears in debuggers
    static uint64_t get_native_thread_id() {
#ifdef _WIN32
        return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__linux__)
        return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
        uint64_t thread_id;
        pthread_threadid_np(NULL, &thread_id);
        return thread_id;
#else
        // Fallback to std::thread::id hash
        return std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    }
    
    // Get both native and std::thread IDs for comparison
    static std::string get_thread_info() {
        std::ostringstream oss;
        oss << "Native ID: " << get_native_thread_id() 
            << ", std::thread ID: " << std::this_thread::get_id();
        return oss.str();
    }

    // Set current thread name (best-effort, platform-specific)
    static void set_current_thread_name(const std::string& name);

private:
    static std::filesystem::path get_executable_path_impl();
};