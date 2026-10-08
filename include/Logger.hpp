#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

// A tiny thread-safe logger. Every worker thread calls log() concurrently,
// so writes to the underlying stream(s) are serialized behind one mutex.
// (For higher throughput you'd swap this for a lock-free MPSC queue with a
// single writer thread -- noted in the README as an extension.)
class Logger {
public:
    explicit Logger(const std::string& filePath) : file_(filePath, std::ios::app) {}

    void log(const std::string& line) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::string ts = timestamp();
        std::cout << "[" << ts << "] " << line << std::endl;
        if (file_.is_open()) {
            file_ << "[" << ts << "] " << line << std::endl;
        }
    }

private:
    static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

    std::ofstream file_;
    std::mutex mtx_;
};
