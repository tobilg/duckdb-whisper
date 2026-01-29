#include "whisper_context.hpp"
#include "whisper.h"

namespace duckdb {

// Suppress whisper.cpp log output
static void whisper_log_callback(enum ggml_log_level level, const char *text, void *user_data) {
    // No-op: suppress all whisper/ggml log output
    (void)level;
    (void)text;
    (void)user_data;
}

static bool g_log_suppressed = false;

static void SuppressWhisperLogs() {
    if (!g_log_suppressed) {
        whisper_log_set(whisper_log_callback, nullptr);
        g_log_suppressed = true;
    }
}

WhisperContextWrapper::WhisperContextWrapper(whisper_context *ctx) : ctx_(ctx) {
}

WhisperContextWrapper::~WhisperContextWrapper() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

WhisperContextWrapper::WhisperContextWrapper(WhisperContextWrapper &&other) noexcept : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

WhisperContextWrapper &WhisperContextWrapper::operator=(WhisperContextWrapper &&other) noexcept {
    if (this != &other) {
        if (ctx_) {
            whisper_free(ctx_);
        }
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

WhisperContextManager &WhisperContextManager::GetInstance() {
    static WhisperContextManager instance;
    return instance;
}

std::shared_ptr<WhisperContextWrapper> WhisperContextManager::GetContext(const std::string &model_path,
                                                                          std::string &error) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Suppress verbose logging from whisper.cpp
    SuppressWhisperLogs();

    // Check if already cached
    auto it = contexts_.find(model_path);
    if (it != contexts_.end() && it->second && it->second->IsValid()) {
        return it->second;
    }

    // Load new context
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;

    whisper_context *ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!ctx) {
        error = "Failed to load whisper model from: " + model_path;
        return nullptr;
    }

    auto wrapper = std::make_shared<WhisperContextWrapper>(ctx);
    contexts_[model_path] = wrapper;

    return wrapper;
}

void WhisperContextManager::ClearContext(const std::string &model_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_.erase(model_path);
}

void WhisperContextManager::ClearAllContexts() {
    std::lock_guard<std::mutex> lock(mutex_);
    contexts_.clear();
}

} // namespace duckdb
