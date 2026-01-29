#pragma once

#include "duckdb.hpp"
#include "whisper.h"
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace duckdb {

// RAII wrapper for whisper_context
class WhisperContextWrapper {
public:
    WhisperContextWrapper(whisper_context *ctx);
    ~WhisperContextWrapper();

    // Non-copyable
    WhisperContextWrapper(const WhisperContextWrapper &) = delete;
    WhisperContextWrapper &operator=(const WhisperContextWrapper &) = delete;

    // Movable
    WhisperContextWrapper(WhisperContextWrapper &&other) noexcept;
    WhisperContextWrapper &operator=(WhisperContextWrapper &&other) noexcept;

    whisper_context *Get() const { return ctx_; }
    bool IsValid() const { return ctx_ != nullptr; }

private:
    whisper_context *ctx_;
};

// Cached context manager (singleton pattern per database)
class WhisperContextManager {
public:
    static WhisperContextManager &GetInstance();

    // Get or create a context for the given model
    std::shared_ptr<WhisperContextWrapper> GetContext(const std::string &model_path, std::string &error);

    // Clear cached context for a model
    void ClearContext(const std::string &model_path);

    // Clear all cached contexts
    void ClearAllContexts();

private:
    WhisperContextManager() = default;
    ~WhisperContextManager() = default;

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<WhisperContextWrapper>> contexts_;
};

} // namespace duckdb
