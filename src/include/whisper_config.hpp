#pragma once

#include "duckdb.hpp"
#include "duckdb/main/database.hpp"
#include <string>

namespace duckdb {

struct WhisperConfig {
    // Model settings
    std::string model;            // Model name (e.g., "base.en", "small", "medium")
    std::string model_path;       // Path to store models
    std::string language;         // Language code or "auto"

    // Processing settings
    int threads;                  // Number of threads to use
    bool timestamps;              // Include timestamps in output
    int max_segment_length;       // Maximum segment length in milliseconds
    bool translate;               // Translate to English instead of transcribe

    // Recording settings
    int device_id;                // Audio input device ID (-1 = default)
    double max_duration;          // Maximum recording duration in seconds
    double silence_duration;      // Silence duration to stop recording
    double silence_threshold;     // Amplitude threshold for silence detection

    // Voice query settings
    std::string text_to_sql_url;  // URL of text-to-sql proxy
    int text_to_sql_timeout;      // Timeout in seconds for proxy requests
    bool voice_query_show_sql;    // Show generated SQL in output
    int voice_query_timeout;      // Timeout in seconds for entire voice query operation

    // Verbose mode
    bool verbose;                 // Enable verbose status messages

    // Default values
    static constexpr const char *DEFAULT_MODEL = "base.en";
    static constexpr const char *DEFAULT_LANGUAGE = "auto";
    static constexpr int DEFAULT_THREADS = 0;  // 0 = auto-detect
    static constexpr bool DEFAULT_TIMESTAMPS = true;
    static constexpr int DEFAULT_MAX_SEGMENT_LENGTH = 30000;  // 30 seconds
    static constexpr bool DEFAULT_TRANSLATE = false;
    static constexpr int DEFAULT_DEVICE_ID = -1;  // -1 = default device
    static constexpr double DEFAULT_MAX_DURATION = 15.0;  // 15 seconds
    static constexpr double DEFAULT_SILENCE_DURATION = 1.0;  // 1 second
    static constexpr double DEFAULT_SILENCE_THRESHOLD = 0.001;  // Very sensitive
    static constexpr const char *DEFAULT_TEXT_TO_SQL_URL = "http://localhost:4000/generate-sql";
    static constexpr int DEFAULT_TEXT_TO_SQL_TIMEOUT = 15;
    static constexpr bool DEFAULT_VOICE_QUERY_SHOW_SQL = false;
    static constexpr int DEFAULT_VOICE_QUERY_TIMEOUT = 30;
    static constexpr bool DEFAULT_VERBOSE = false;

    WhisperConfig();

    // Get default model path based on platform
    static std::string GetDefaultModelPath();
};

class WhisperConfigManager {
public:
    // Register DuckDB extension settings via AddExtensionOption
    static void RegisterSettings(DatabaseInstance &db);

    // Get current configuration from context (reads from DuckDB settings)
    static WhisperConfig GetConfig(ClientContext &context);
};

} // namespace duckdb
