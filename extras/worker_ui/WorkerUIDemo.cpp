#include <deque>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstddef>
#include <array>

#include "worker/ui/IWorkerService.hpp"
#include "worker/ui/WorkerUI.hpp"
#include "logger.hpp"

// Minimal mock that exercises the UI without connecting to a real manager
class WorkerModel : public IWorkerService {
public:
    WorkerModel() = default;

    void start() override {
        exit_requested_.store(false, std::memory_order_relaxed);
        run_loop();
    }

    void shutdown() override {
        exit_requested_.store(true, std::memory_order_relaxed);
    }

    void start_runtime() override {}
    void pause_runtime() override {}
    void disconnect_runtime() override {}

    int GetTaskCount() override {
        std::lock_guard<std::mutex> lock(task_mutex_);
        return task_count_;
    }

    std::string GetBytesSent() override {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        return format_bytes(bytes_sent_);
    }

    std::string GetBytesReceived() override {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        return format_bytes(bytes_received_);
    }

    std::string GetConnectionStatus() override {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return connection_status_;
    }

    int GetNumberOfLogLines() override {
        std::lock_guard<std::mutex> lock(log_mutex_);
        return static_cast<int>(log_lines_.size());
    }

    std::vector<std::string> GetLogLines(int start, int count) override {
        std::lock_guard<std::mutex> lock(log_mutex_);
        const int total = static_cast<int>(log_lines_.size());
        if (total == 0) {
            return {};
        }
        start = std::clamp(start, 0, total - 1);
        const int stop = std::min(start + count, total);
        return std::vector<std::string>(log_lines_.begin() + start, log_lines_.begin() + stop);
    }

private:
    void run_loop() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> connect_dist(50, 120);
        std::uniform_int_distribution<> payload_dist(256, 4096);

        for (int i = 1; i <= 100 && !exit_requested_.load(std::memory_order_relaxed); ++i) {
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                log_lines_.push_back("Connection step " + std::to_string(i) + " of 100...");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        if (exit_requested_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            connection_status_ = "Stopped";
            return;
        }

        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            log_lines_.push_back("Connection established.");
        }
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            connection_status_ = "Connected";
        }

        constexpr std::size_t max_log_lines = 300;
        while (!exit_requested_.load(std::memory_order_relaxed)) {
            std::uint64_t total_sent;
            std::uint64_t total_received;

            {
                std::lock_guard<std::mutex> lock(task_mutex_);
                task_count_ += 1;
            }
            {
                std::lock_guard<std::mutex> lock(resource_mutex_);
                bytes_sent_ += static_cast<std::uint64_t>(payload_dist(gen));
                bytes_received_ += static_cast<std::uint64_t>(payload_dist(gen) / 2);
                total_sent = bytes_sent_;
                total_received = bytes_received_;
            }

            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                log_lines_.push_back(
                    "Totals: " + format_bytes(total_sent) + " sent / " + format_bytes(total_received) + " received");
                if (log_lines_.size() > max_log_lines) {
                    log_lines_.pop_front();
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(connect_dist(gen)));
        }

        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            connection_status_ = "Disconnected";
        }
    }

    int task_count_{0};
    std::uint64_t bytes_sent_{0};
    std::uint64_t bytes_received_{0};
    std::string connection_status_{"Connecting"};
    std::deque<std::string> log_lines_;

    std::mutex task_mutex_;
    std::mutex resource_mutex_;
    std::mutex status_mutex_;
    std::mutex log_mutex_;
    std::atomic<bool> exit_requested_{false};

    static std::string format_bytes(std::uint64_t bytes) {
        constexpr std::size_t unit_count = 5;
        static constexpr const char* units[unit_count] = {"B", "KB", "MB", "GB", "TB"};

        double value = static_cast<double>(bytes);
        std::size_t unit_index = 0;
        while (value >= 1024.0 && unit_index + 1 < unit_count) {
            value /= 1024.0;
            ++unit_index;
        }

        std::ostringstream oss;
        oss.setf(std::ios::fixed, std::ios::floatfield);
        oss.precision(value >= 100.0 || unit_index == 0 ? 0 : 1);
        oss << value << units[unit_index];
        return oss.str();
    }
};

int main() {
    auto worker = std::make_shared<WorkerModel>();
    auto logger = std::make_shared<Logger>("WorkerUIDemo");

    WorkerUI ui(worker, logger);
    ui.Run();

    return 0;
}