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
	// Intentionally don't call whisper_free() to avoid Metal cleanup assertion
	// at program exit. The OS will reclaim resources anyway.
	// This is a workaround for: https://github.com/ggml-org/llama.cpp/issues/17869
	ctx_ = nullptr;
}

WhisperContextWrapper::WhisperContextWrapper(WhisperContextWrapper &&other) noexcept : ctx_(other.ctx_) {
	other.ctx_ = nullptr;
}

WhisperContextWrapper &WhisperContextWrapper::operator=(WhisperContextWrapper &&other) noexcept {
	if (this != &other) {
		// Don't free old context (see destructor comment)
		ctx_ = other.ctx_;
		other.ctx_ = nullptr;
	}
	return *this;
}

WhisperContextManager &WhisperContextManager::GetInstance() {
	// Use a pointer that intentionally leaks to avoid Metal cleanup assertion
	// at program exit. The OS will reclaim resources anyway.
	static WhisperContextManager *instance = new WhisperContextManager();
	return *instance;
}

std::shared_ptr<WhisperContextWrapper> WhisperContextManager::GetContext(const std::string &model_path, bool use_gpu,
                                                                         std::string &error) {
	std::lock_guard<std::mutex> lock(mutex_);

	// Suppress verbose logging from whisper.cpp
	SuppressWhisperLogs();

	// Create cache key that includes GPU setting
	std::string cache_key = model_path + (use_gpu ? ":gpu" : ":cpu");

	// Check if already cached
	auto it = contexts_.find(cache_key);
	if (it != contexts_.end() && it->second && it->second->IsValid()) {
		return it->second;
	}

	// Load new context
	whisper_context_params cparams = whisper_context_default_params();
	cparams.use_gpu = use_gpu;

	whisper_context *ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
	if (!ctx) {
		error = "Failed to load whisper model from: " + model_path;
		return nullptr;
	}

	auto wrapper = std::make_shared<WhisperContextWrapper>(ctx);
	contexts_[cache_key] = wrapper;

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
