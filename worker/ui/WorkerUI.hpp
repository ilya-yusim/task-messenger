/**
 * \file worker/ui/WorkerUI.hpp
 * \brief Terminal UI for monitoring worker status via FTXUI.
 */

#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "logger.hpp"
#include "processUtils.hpp"
#include "worker/ui/IWorkerService.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <thread>
#include <memory>
#include <iostream>
#include <utility>

using namespace ftxui;

/// Clear the terminal screen using ANSI escape sequences.
inline void ClearTerminal() {
    std::cout << "\033[2J\033[3J\033[H" << std::flush;
}

/// Save terminal state by enabling the alternate screen buffer.
inline void SaveTerminalState() {
    std::cout << "\033[?1049h" << std::flush;  // Switch to alternate screen
}

/// Restore terminal state and return to the primary screen buffer.
inline void RestoreTerminalState() {
    std::cout << "\033[?1049l" << std::flush;  // Switch back to main screen
}

/** \brief Interactive terminal dashboard for a single worker instance. */
class WorkerUI
{
public:
    /**
     * \brief Construct the UI tied to a worker service.
     * \param worker Session service exposing metrics and control hooks.
     * \param logger Logger providing append-only log access.
     */
    WorkerUI(std::shared_ptr<IWorkerService> worker, std::shared_ptr<Logger> logger)
        : worker_(std::move(worker)), logger_(std::move(logger)), exit_requested_(false) {}

    /** \brief Ensure background threads join on destruction. */
    ~WorkerUI() {
        exit_requested_ = true;
        if (runtime_thread_.joinable()) {
            runtime_thread_.join();
        }
    }

    /** \brief Launch the interactive UI loop and manage worker lifecycle. */
    void Run()
    {
        // Start runtime in background thread
        runtime_thread_ = std::thread([this] {
            ProcessUtils::set_current_thread_name("WorkerRuntime");
            if (worker_) {
                worker_->start();
            }
            runtime_completed_.store(true, std::memory_order_release);
        });

        // Save terminal state and clear screen before starting UI
        SaveTerminalState();
        ClearTerminal();
        
        auto screen = ScreenInteractive::TerminalOutput();

        // --- UI Controls (buttons) ---
        auto start_btn = Button("Start", [this] { if (worker_) worker_->start_runtime(); });
        auto pause_btn = Button("Pause", [this] { if (worker_) worker_->pause_runtime(); });
        auto disconnect_btn = Button("Disconnect", [this] { if (worker_) worker_->disconnect_runtime(); });
        auto quit_btn = Button("Quit", [this, &screen]
                               { 
                                   if (worker_) worker_->shutdown();
                                   // Set flag to trigger delayed exit from refresher
                                   exit_requested_ = true;
                               });

        auto buttons = std::vector<Component>{
            start_btn, pause_btn, disconnect_btn, quit_btn};
        int selected_button = 0;

        // Prepare a vector of boxes, one for each button
        button_boxes_.resize(buttons.size());

        // Controls as a single focusable component with arrow navigation and space to select
        auto controls = Renderer([&](bool is_focused)
            {
              auto border_decorator = is_focused ? borderStyled(Color::Yellow) : border;
              Elements btn_elements;
              for (size_t i = 0; i < buttons.size(); ++i) {
                  auto btn = buttons[i]->Render() | reflect(button_boxes_[i]);
                  if (is_focused && int(i) == selected_button)
                      btn = btn | color(Color::White) | bgcolor(Color::Yellow) | bold | inverted;
                  else
                      btn = btn | color(Color::White) | bgcolor(Color::Black) | inverted;
                  btn_elements.push_back(btn);
              }
              return hbox(std::move(btn_elements)) | border_decorator | bgcolor(Color::Black);
            });

        // Add event handler for arrow keys and space bar
        controls |= CatchEvent([&](const Event &event)
                               {
              // Handle mouse movement and clicks
              if (event.is_mouse()) {
                  // Copy the event to a local variable to access mouse data
                  Event event_copy = event;
                  int mouse_x = event_copy.mouse().x;
                  int x = 0;
                  int hovered = -1;
                  for (size_t i = 0; i < buttons.size(); ++i) {
                      int btn_width = button_boxes_[i].x_max - button_boxes_[i].x_min + 1;
                      if (mouse_x >= x && mouse_x < x + btn_width) {
                          hovered = int(i);
                          break;
                      }
                      x += btn_width;
                  }
                  if (hovered >= 0 && hovered < int(buttons.size())) {
                      selected_button = hovered;
                      if (event_copy.mouse().button == Mouse::Left && event_copy.mouse().motion == Mouse::Pressed) {
                          buttons[selected_button]->OnEvent(Event::Return);
                          return true;
                      }
                  }
                  return false;
              }
              // keyboard navigation...
              if (event == Event::ArrowLeft) {
                  if (selected_button > 0)
                      --selected_button;
                  return true;
              }
              if (event == Event::ArrowRight) {
                  if (selected_button < int(buttons.size()) - 1)
                      ++selected_button;
                  return true;
              }
              if (event == Event::Character(' ') || event == Event::Return) {
                  buttons[selected_button]->OnEvent(Event::Return);
                  return true;
              }
              return false; });

        // --- Log area renderer with vertical scrollbar ---
        auto log_renderer = Renderer([this](bool is_focused)
                                     {
              std::vector<Element> log_elements;
              {
                  // Only show the visible lines
                  std::lock_guard<std::mutex> lock(ui_mutex_);
                  for (const auto& line : log_lines_display_)
                      log_elements.push_back(text(line));
              }
  
              // Highlight border if focused
              auto border_decorator = is_focused ? borderStyled(Color::Yellow) : border;

              auto log_box = vbox(
                  text("Connection Log:"),
                  separator(),
                  std::move(log_elements));

              return log_box 
                      | size(HEIGHT, EQUAL, log_height_+ 2) // +2 for header and separator
                      | border_decorator
                      | vscroll_indicator
                      | flex; });

        // Make log area interactive for scrolling
        log_renderer |= CatchEvent([&](const Event &event)
                                   {
              int log_lines_count = logger_ ? logger_->get_number_of_lines() : 0;
              int max_scroll = std::max(0, log_lines_count - log_height_);
  
              // Handle mouse wheel scrolling 
              if (event.is_mouse()) {
                  // "event" is a constant reference, so we need to copy it to access mouse data
                  Event event_copy = event;
                  auto mouse_event = event_copy.mouse();

                  if (mouse_event.button == Mouse::WheelUp) {
                      int new_scroll = std::max(0, log_scroll_.load() - 1);
                      log_scroll_.store(new_scroll);
                      scrolling_ = true;
                      return true;
                  }
                  if (mouse_event.button == Mouse::WheelDown) {
                      int new_scroll = std::min(log_scroll_.load() + 1, max_scroll);
                      log_scroll_.store(new_scroll);
                      scrolling_ = true;
                      return true;
                  }
              }

              if (event == Event::ArrowUp) {
                  int new_scroll = std::max(0, log_scroll_.load() - 1);
                  log_scroll_.store(new_scroll);
                  scrolling_ = true;
                  return true;
              }
              if (event == Event::ArrowDown) {
                  int new_scroll = std::min(log_scroll_.load() + 1, max_scroll);
                  log_scroll_.store(new_scroll);
                  scrolling_ = true;
                  return true;
              }
  
              // Handle any other key presses
              if (event.is_character() )
              {
                  // Turn off scrolling when any other key is pressed
                  // The refresher function will reset the log scroll position to the tail
                  scrolling_ = false;
                  return true;
              }
              return false; });

        // --- Main status and metrics area ---
        auto main_status = Renderer([this]
                                    {
              Color status_color = Color::Yellow;
              std::string connection_status_copy;
              std::string bytes_sent_copy;
              std::string bytes_received_copy;
              int tasks_completed = task_count_.load(std::memory_order_relaxed);
              {
                  std::lock_guard<std::mutex> lock(ui_mutex_);
                  connection_status_copy = connection_status_;
                  bytes_sent_copy = bytes_sent_display_;
                  bytes_received_copy = bytes_received_display_;
              }
              if (connection_status_copy == "Connected") status_color = Color::Green;
              if (connection_status_copy == "Disconnected") status_color = Color::Red;
  
              // Format CPU, memory, and bandwidth usage
              std::ostringstream cpu_stream;
              cpu_stream.precision(2);
              cpu_stream << std::fixed << cpu_usage_.load();
              std::string cpu_str = cpu_stream.str() + "%";
              std::string mem_str = std::to_string(mem_usage_.load()) + "MB";
              return vbox({
                  hbox({ text("Worker Status: "), text(connection_status_copy) | color(status_color) }),
                  separator(),
                  text("Tasks completed: " + std::to_string(tasks_completed)),
                  hbox({ text("CPU Usage:   "), text(cpu_str) }),
                  hbox({ text("Memory Usage:"), text(mem_str) }),
                  hbox({ text("Bytes Sent:  "), text(bytes_sent_copy.empty() ? "0B" : bytes_sent_copy) }),
                  hbox({ text("Bytes Recv:  "), text(bytes_received_copy.empty() ? "0B" : bytes_received_copy) }),
                  separator(),
              }) | flex; });

        // --- Compose status, controls, and log area in a vertical container ---
        auto root = Container::Vertical({main_status, controls, log_renderer});

        // --- Background thread to refresh UI state from Worker every 500ms ---
        refresher_ = std::thread([&screen, this]
        { 
            ProcessUtils::set_current_thread_name("UIRefresher");
            // Keep refreshing while runtime is running OR while exit is in progress
            while (!exit_requested_ || !runtime_completed_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (worker_) {
                    task_count_.store(worker_->GetTaskCount(), std::memory_order_relaxed);
                    ProcessUsage usage = worker_->GetProcessUsage();
                    cpu_usage_.store(usage.cpu_percent, std::memory_order_relaxed);
                    mem_usage_.store(usage.memory_bytes / (1024.0f * 1024.0f), std::memory_order_relaxed);
                    auto bytes_sent = worker_->GetBytesSent();      // Already formatted (e.g., 1.5MB)
                    auto bytes_received = worker_->GetBytesReceived();
                    {
                        std::lock_guard<std::mutex> lock(ui_mutex_);
                        connection_status_ = worker_->GetConnectionStatus();
                        bytes_sent_display_ = std::move(bytes_sent);
                        bytes_received_display_ = std::move(bytes_received);
                    }
                }
                // Get log lines directly from the logger
                {
                    std::lock_guard<std::mutex> lock(ui_mutex_);
                    int log_lines_count = logger_ ? logger_->get_number_of_lines() : 0;
                    if(scrolling_) {
                        log_lines_display_ = logger_ ? logger_->get_lines(log_scroll_, log_height_) : std::vector<std::string>{};
                    } else {
                        int start = std::max(0, log_lines_count - log_height_);
                        log_scroll_.store(start, std::memory_order_relaxed);
                        int num_lines_to_show = std::min(log_height_, log_lines_count - start);
                        log_lines_display_ = logger_ ? logger_->get_lines(start, num_lines_to_show) : std::vector<std::string>{};
                    }
                }
                screen.PostEvent(ftxui::Event::Custom);
            }
            
            // Runtime thread has completed, now exit the UI
            screen.Exit();
        });

        // --- Start the FTXUI event loop with the root container ---
        screen.Loop(root);

        // Set exit flag to stop refresher
        exit_requested_ = true;

        // Join both threads
        if (runtime_thread_.joinable()) {
            runtime_thread_.join();
        }
        if (refresher_.joinable())
            refresher_.join();
        
        // Restore terminal state on exit
        RestoreTerminalState();
    }

private:
    std::shared_ptr<IWorkerService> worker_;
    std::shared_ptr<Logger> logger_;
    std::vector<ftxui::Box> button_boxes_;
    std::atomic<int> task_count_{0};
    std::atomic<float> cpu_usage_{0.0f};
    std::atomic<float> mem_usage_{0.0f};
    std::string connection_status_;              // Protected by ui_mutex_
    std::string bytes_sent_display_;             // Protected by ui_mutex_
    std::string bytes_received_display_;         // Protected by ui_mutex_
    std::vector<std::string> log_lines_display_; // Protected by ui_mutex_
    std::mutex ui_mutex_;                        // Mutex for UI state (log/status)
    std::atomic<int> log_scroll_{0};             // Scroll position for log area (now atomic)
    int log_height_ = 10;                        // Height of the log area
    bool scrolling_ = false;                     // Whether the log area is currently scrolling
    std::atomic<bool> exit_requested_;           // Flag to request UI exit
    std::atomic<bool> runtime_completed_{false}; // Flag set by runtime when it completes
    std::thread refresher_;                      // Thread for refreshing UI
    std::thread runtime_thread_;                 // Thread for running worker runtime
};
