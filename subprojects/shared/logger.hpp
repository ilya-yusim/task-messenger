#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <streambuf>
#include <climits>

// Fix Windows macro conflicts
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// Usage:
//   g++ -DLOGGER_ENABLE_TRACE ...
//   logger.trace("init", "Starting system");
// Traces are emitted regardless of per-sink log level thresholds.

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Critical
#ifdef LOGGER_ENABLE_TRACE
    , Trace  // Highest so it survives retrieval filters
#endif
};

inline std::string to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARNING";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
#ifdef LOGGER_ENABLE_TRACE
        case LogLevel::Trace:    return "TRACE";
#endif
        default:                 return "UNKNOWN";
    }
}

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void log(LogLevel level, const std::string& message) = 0;
    void set_level(LogLevel level) { min_level_ = level; }
    LogLevel level() const { return min_level_; }

#ifdef LOGGER_ENABLE_TRACE
    // Trace bypasses level filtering; default no-op if not overridden.
    virtual void trace(const std::string& id, const std::string& message) { 
        (void)id; (void)message;
    }
#endif
protected:
    LogLevel min_level_ = LogLevel::Info; // Default level set to INFO
};

class StdoutSink : public LogSink {
public:
    void log(LogLevel level, const std::string& message) override {
        if (level < min_level_) return;
        std::cout << "[" << to_string(level) << "] " << message << std::endl;
    }
#ifdef LOGGER_ENABLE_TRACE
    void trace(const std::string& id, const std::string& message) override {
        std::cout << "[TRACE][" << id << "] " << message << std::endl;
    }
#endif
};

class VectorSink : public LogSink {
public:
    void log(LogLevel level, const std::string& message) override {
        if (level < min_level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "[" << to_string(level) << "] " << message;
        lines_.push_back(oss.str());
        levels_.push_back(level);
    }
#ifdef LOGGER_ENABLE_TRACE
    void trace(const std::string& id, const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "[TRACE][" << id << "] " << message;
        lines_.push_back(oss.str());
        levels_.push_back(LogLevel::Trace);
    }
#endif
    std::vector<std::string> get_lines(size_t start = 0, size_t count = SIZE_MAX, LogLevel min_level = LogLevel::Debug) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> filtered;
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (levels_[i] >= min_level) {
                filtered.push_back(lines_[i]);
            }
        }
        if (start >= filtered.size()) return {};
        // Use (std::min) with parentheses to avoid macro conflicts
        size_t end = (std::min)(start + count, filtered.size());
        return std::vector<std::string>(filtered.begin() + start, filtered.begin() + end);
    }
    // Get the number of log lines 
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_.size();
    }
private:
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
    std::vector<LogLevel> levels_;
};

class Logger {
public:
    // Default constructor with empty name
    Logger() : name_("Default") {}
    
    // Constructor with logger name
    Logger(const std::string& name) : name_(name) {}

    void add_sink(std::shared_ptr<LogSink> sink) {
        sinks_.push_back(std::move(sink));
    }
    
    void log(LogLevel level, const std::string& message) {
        for (const auto& sink : sinks_) {
            sink->log(level, message);
        }
    }

#ifdef LOGGER_ENABLE_TRACE
    void trace(const std::string& id, const std::string& message) {
        for (const auto& sink : sinks_) {
            sink->trace(id, message); // Bypass level filtering
        }
    }
#endif
    
    void debug(const std::string& message)    { log(LogLevel::Debug, message); }
    void info(const std::string& message)     { log(LogLevel::Info, message); }
    void warning(const std::string& message)  { log(LogLevel::Warning, message); }
    void error(const std::string& message)    { log(LogLevel::Error, message); }
    void critical(const std::string& message) { log(LogLevel::Critical, message); }

    // Get the logger name
    const std::string& name() const { return name_; }
    
    // Set the logger name
    void set_name(const std::string& name) { name_ = name; }

    std::vector<std::string> get_lines(int start = 0, int count = INT_MAX, LogLevel min_level = LogLevel::Debug) const {
        for (const auto& sink : sinks_) {
            auto vector_sink = std::dynamic_pointer_cast<VectorSink>(sink);
            if (vector_sink) {
                return vector_sink->get_lines(static_cast<size_t>(start), static_cast<size_t>(count), min_level);
            }
        }
        return {};
    }

    int get_number_of_lines() const {
        for (const auto& sink : sinks_) {
            auto vector_sink = std::dynamic_pointer_cast<VectorSink>(sink);
            if (vector_sink) {
                return static_cast<int>(vector_sink->size());
            }
        }
        return 0;
    }

private:
    std::string name_;
    std::vector<std::shared_ptr<LogSink>> sinks_;
};