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

	whisper_context *Get() const {
		return ctx_;
	}
	bool IsValid() const {
		return ctx_ != nullptr;
	}

	// whisper_full() keeps its results in the context's default state, so a context must not run two
	// transcriptions at once. Hold this lock from whisper_full() until all results have been read.
	std::mutex &GetMutex() {
		return mutex_;
	}

private:
	whisper_context *ctx_;
	std::mutex mutex_;
};

// Cached context manager (singleton pattern per database)
class WhisperContextManager {
public:
	static WhisperContextManager &GetInstance();

	// Get or create a context for the given model
	std::shared_ptr<WhisperContextWrapper> GetContext(const std::string &model_path, bool use_gpu, std::string &error);

	// Clear cached contexts (GPU and CPU) for a model
	void ClearContext(const std::string &model_path);

	// Clear all cached contexts
	void ClearAllContexts();

private:
	WhisperContextManager() = default;

	static std::string GetCacheKey(const std::string &model_path, bool use_gpu);
	~WhisperContextManager() = default;

	std::mutex mutex_;
	std::unordered_map<std::string, std::shared_ptr<WhisperContextWrapper>> contexts_;
};

} // namespace duckdb
