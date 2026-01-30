#pragma once

#include "duckdb.hpp"
#include <string>
#include <vector>

namespace duckdb {

struct ModelInfo {
	std::string name;        // Model name (e.g., "base.en")
	std::string file_path;   // Full path to model file
	int64_t file_size;       // File size in bytes
	bool is_downloaded;      // Whether the model exists locally
	std::string description; // Model description
};

class ModelManager {
public:
	// Available model names
	static const std::vector<std::string> &GetAvailableModels();

	// Get HuggingFace URL for model
	static std::string GetModelUrl(const std::string &model_name);

	// Get model file name
	static std::string GetModelFileName(const std::string &model_name);

	// Get full path to model file
	static std::string GetModelPath(const std::string &model_name, const std::string &base_path);

	// Check if model exists locally
	static bool IsModelDownloaded(const std::string &model_name, const std::string &base_path);

	// Get info for a specific model
	static ModelInfo GetModelInfo(const std::string &model_name, const std::string &base_path);

	// List all models with download status
	static std::vector<ModelInfo> ListModels(const std::string &base_path);

	// Download model from HuggingFace
	static bool DownloadModel(const std::string &model_name, const std::string &base_path, std::string &error);

	// Validate model name
	static bool IsValidModelName(const std::string &model_name);

private:
	static constexpr const char *HUGGINGFACE_BASE_URL = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";
};

} // namespace duckdb
