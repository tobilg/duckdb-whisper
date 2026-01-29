#include "model_manager.hpp"
#include "duckdb/common/file_system.hpp"

#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

namespace duckdb {

// Available Whisper models
static const std::vector<std::string> AVAILABLE_MODELS = {
    "tiny",
    "tiny.en",
    "base",
    "base.en",
    "small",
    "small.en",
    "medium",
    "medium.en",
    "large-v1",
    "large-v2",
    "large-v3",
    "large-v3-turbo"
};

// Model descriptions
static const std::unordered_map<std::string, std::string> MODEL_DESCRIPTIONS = {
    {"tiny", "Tiny multilingual model (~75MB, fastest)"},
    {"tiny.en", "Tiny English-only model (~75MB, fastest)"},
    {"base", "Base multilingual model (~142MB)"},
    {"base.en", "Base English-only model (~142MB)"},
    {"small", "Small multilingual model (~466MB)"},
    {"small.en", "Small English-only model (~466MB)"},
    {"medium", "Medium multilingual model (~1.5GB)"},
    {"medium.en", "Medium English-only model (~1.5GB)"},
    {"large-v1", "Large multilingual model v1 (~2.9GB, most accurate)"},
    {"large-v2", "Large multilingual model v2 (~2.9GB, most accurate)"},
    {"large-v3", "Large multilingual model v3 (~2.9GB, most accurate)"},
    {"large-v3-turbo", "Large multilingual model v3 turbo (~1.6GB, fast + accurate)"}
};

const std::vector<std::string> &ModelManager::GetAvailableModels() {
    return AVAILABLE_MODELS;
}

std::string ModelManager::GetModelUrl(const std::string &model_name) {
    return std::string(HUGGINGFACE_BASE_URL) + "ggml-" + model_name + ".bin";
}

std::string ModelManager::GetModelFileName(const std::string &model_name) {
    return "ggml-" + model_name + ".bin";
}

std::string ModelManager::GetModelPath(const std::string &model_name, const std::string &base_path) {
    return base_path + "/" + GetModelFileName(model_name);
}

bool ModelManager::IsModelDownloaded(const std::string &model_name, const std::string &base_path) {
    std::string path = GetModelPath(model_name, base_path);
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

ModelInfo ModelManager::GetModelInfo(const std::string &model_name, const std::string &base_path) {
    ModelInfo info;
    info.name = model_name;
    info.file_path = GetModelPath(model_name, base_path);
    info.is_downloaded = IsModelDownloaded(model_name, base_path);

    auto desc_it = MODEL_DESCRIPTIONS.find(model_name);
    info.description = desc_it != MODEL_DESCRIPTIONS.end() ? desc_it->second : "";

    if (info.is_downloaded) {
        struct stat buffer;
        if (stat(info.file_path.c_str(), &buffer) == 0) {
            info.file_size = buffer.st_size;
        } else {
            info.file_size = 0;
        }
    } else {
        info.file_size = 0;
    }

    return info;
}

std::vector<ModelInfo> ModelManager::ListModels(const std::string &base_path) {
    std::vector<ModelInfo> models;
    for (const auto &model_name : AVAILABLE_MODELS) {
        models.push_back(GetModelInfo(model_name, base_path));
    }
    return models;
}

// Helper to create directories recursively
static bool CreateDirectories(const std::string &path) {
    std::string current;
    for (size_t i = 0; i < path.size(); i++) {
        current += path[i];
        if (path[i] == '/' || path[i] == '\\' || i == path.size() - 1) {
            struct stat buffer;
            if (stat(current.c_str(), &buffer) != 0) {
#ifdef _WIN32
                if (_mkdir(current.c_str()) != 0 && errno != EEXIST) {
                    return false;
                }
#else
                if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
#endif
            }
        }
    }
    return true;
}

bool ModelManager::DownloadModel(const std::string &model_name, const std::string &base_path, std::string &error) {
    if (!IsValidModelName(model_name)) {
        error = "Invalid model name: " + model_name;
        return false;
    }

    // Create directory if it doesn't exist
    if (!CreateDirectories(base_path)) {
        error = "Failed to create model directory: " + base_path;
        return false;
    }

    // Check if already downloaded
    if (IsModelDownloaded(model_name, base_path)) {
        // Model already exists
        return true;
    }

    // For now, provide instructions for manual download
    // Users can use: COPY (SELECT * FROM read_blob('url')) TO 'path'
    // Or use curl/wget externally
    std::string model_url = GetModelUrl(model_name);
    std::string model_path = GetModelPath(model_name, base_path);

    error = "Please download the model manually:\n"
            "  curl -L -o '" + model_path + "' '" + model_url + "'\n"
            "Or use DuckDB's httpfs:\n"
            "  INSTALL httpfs; LOAD httpfs;\n"
            "  COPY (SELECT content FROM read_blob('" + model_url + "')) TO '" + model_path + "';";

    return false;
}

bool ModelManager::DeleteModel(const std::string &model_name, const std::string &base_path, std::string &error) {
    if (!IsValidModelName(model_name)) {
        error = "Invalid model name: " + model_name;
        return false;
    }

    std::string model_path = GetModelPath(model_name, base_path);

    if (!IsModelDownloaded(model_name, base_path)) {
        error = "Model not found: " + model_name;
        return false;
    }

    if (std::remove(model_path.c_str()) != 0) {
        error = "Failed to delete model file";
        return false;
    }

    return true;
}

bool ModelManager::IsValidModelName(const std::string &model_name) {
    for (const auto &valid_name : AVAILABLE_MODELS) {
        if (valid_name == model_name) {
            return true;
        }
    }
    return false;
}

} // namespace duckdb
