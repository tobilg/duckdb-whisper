#include "whisper_config.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/config.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace duckdb {

WhisperConfig::WhisperConfig()
    : model(DEFAULT_MODEL), model_path(GetDefaultModelPath()), language(DEFAULT_LANGUAGE), threads(DEFAULT_THREADS),
      timestamps(DEFAULT_TIMESTAMPS), max_segment_length(DEFAULT_MAX_SEGMENT_LENGTH), translate(DEFAULT_TRANSLATE),
      device_id(DEFAULT_DEVICE_ID), max_duration(DEFAULT_MAX_DURATION), silence_duration(DEFAULT_SILENCE_DURATION),
      silence_threshold(DEFAULT_SILENCE_THRESHOLD), text_to_sql_url(DEFAULT_TEXT_TO_SQL_URL),
      text_to_sql_timeout(DEFAULT_TEXT_TO_SQL_TIMEOUT), voice_query_show_sql(DEFAULT_VOICE_QUERY_SHOW_SQL),
      voice_query_timeout(DEFAULT_VOICE_QUERY_TIMEOUT), verbose(DEFAULT_VERBOSE), ffmpeg_logging(DEFAULT_FFMPEG_LOGGING),
      use_gpu(DEFAULT_USE_GPU) {
}

std::string WhisperConfig::GetDefaultModelPath() {
	std::string home_dir;

#ifdef _WIN32
	char path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) {
		home_dir = path;
	} else {
		home_dir = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "C:\\";
	}
	return home_dir + "\\.duckdb\\whisper\\models";
#else
	const char *home = std::getenv("HOME");
	if (!home) {
		struct passwd *pw = getpwuid(getuid());
		home = pw ? pw->pw_dir : "/tmp";
	}
	home_dir = home;
	return home_dir + "/.duckdb/whisper/models";
#endif
}

void WhisperConfigManager::RegisterSettings(DatabaseInstance &db) {
	auto &config = DBConfig::GetConfig(db);

	// Model settings
	config.AddExtensionOption("whisper_model", "Whisper model name (e.g., tiny.en, base.en, small, medium, large-v3)",
	                          LogicalType::VARCHAR, Value(WhisperConfig::DEFAULT_MODEL));

	config.AddExtensionOption("whisper_model_path", "Path to store Whisper models", LogicalType::VARCHAR,
	                          Value(WhisperConfig::GetDefaultModelPath()));

	config.AddExtensionOption("whisper_language", "Target language code or 'auto' for detection", LogicalType::VARCHAR,
	                          Value(WhisperConfig::DEFAULT_LANGUAGE));

	config.AddExtensionOption("whisper_threads", "Number of processing threads (0 = auto-detect)", LogicalType::INTEGER,
	                          Value::INTEGER(WhisperConfig::DEFAULT_THREADS));

	// Recording settings
	config.AddExtensionOption("whisper_device_id", "Audio input device ID (-1 = system default)", LogicalType::INTEGER,
	                          Value::INTEGER(WhisperConfig::DEFAULT_DEVICE_ID));

	config.AddExtensionOption("whisper_max_duration", "Maximum recording duration in seconds", LogicalType::DOUBLE,
	                          Value::DOUBLE(WhisperConfig::DEFAULT_MAX_DURATION));

	config.AddExtensionOption("whisper_silence_duration", "Silence duration to stop recording (seconds)",
	                          LogicalType::DOUBLE, Value::DOUBLE(WhisperConfig::DEFAULT_SILENCE_DURATION));

	config.AddExtensionOption("whisper_silence_threshold", "Amplitude threshold for silence detection",
	                          LogicalType::DOUBLE, Value::DOUBLE(WhisperConfig::DEFAULT_SILENCE_THRESHOLD));

	config.AddExtensionOption("whisper_verbose", "Show status messages during recording and voice query operations",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(WhisperConfig::DEFAULT_VERBOSE));

	config.AddExtensionOption("whisper_ffmpeg_logging", "Enable FFmpeg log output (warnings, info messages)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(WhisperConfig::DEFAULT_FFMPEG_LOGGING));

	config.AddExtensionOption("whisper_use_gpu", "Use GPU acceleration if available (Metal on macOS)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(WhisperConfig::DEFAULT_USE_GPU));

#ifdef WHISPER_ENABLE_VOICE_QUERY
	// Voice query settings
	config.AddExtensionOption("whisper_text_to_sql_url", "URL of the text-to-sql proxy service", LogicalType::VARCHAR,
	                          Value(WhisperConfig::DEFAULT_TEXT_TO_SQL_URL));

	config.AddExtensionOption("whisper_text_to_sql_timeout", "Timeout for text-to-sql proxy requests (seconds)",
	                          LogicalType::INTEGER, Value::INTEGER(WhisperConfig::DEFAULT_TEXT_TO_SQL_TIMEOUT));

	config.AddExtensionOption("whisper_voice_query_show_sql", "Show generated SQL in voice query output",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(WhisperConfig::DEFAULT_VOICE_QUERY_SHOW_SQL));

	config.AddExtensionOption("whisper_voice_query_timeout", "Timeout for entire voice query operation (seconds)",
	                          LogicalType::INTEGER, Value::INTEGER(WhisperConfig::DEFAULT_VOICE_QUERY_TIMEOUT));
#endif
}

WhisperConfig WhisperConfigManager::GetConfig(ClientContext &context) {
	WhisperConfig config;
	Value val;

	if (context.TryGetCurrentSetting("whisper_model", val)) {
		config.model = val.GetValue<string>();
	}
	if (context.TryGetCurrentSetting("whisper_model_path", val)) {
		config.model_path = val.GetValue<string>();
	}
	if (context.TryGetCurrentSetting("whisper_language", val)) {
		config.language = val.GetValue<string>();
	}
	if (context.TryGetCurrentSetting("whisper_threads", val)) {
		config.threads = val.GetValue<int32_t>();
	}
	if (context.TryGetCurrentSetting("whisper_device_id", val)) {
		config.device_id = val.GetValue<int32_t>();
	}
	if (context.TryGetCurrentSetting("whisper_max_duration", val)) {
		config.max_duration = val.GetValue<double>();
	}
	if (context.TryGetCurrentSetting("whisper_silence_duration", val)) {
		config.silence_duration = val.GetValue<double>();
	}
	if (context.TryGetCurrentSetting("whisper_silence_threshold", val)) {
		config.silence_threshold = val.GetValue<double>();
	}
	if (context.TryGetCurrentSetting("whisper_verbose", val)) {
		config.verbose = val.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("whisper_ffmpeg_logging", val)) {
		config.ffmpeg_logging = val.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("whisper_use_gpu", val)) {
		config.use_gpu = val.GetValue<bool>();
	}

#ifdef WHISPER_ENABLE_VOICE_QUERY
	if (context.TryGetCurrentSetting("whisper_text_to_sql_url", val)) {
		config.text_to_sql_url = val.GetValue<string>();
	}
	if (context.TryGetCurrentSetting("whisper_text_to_sql_timeout", val)) {
		config.text_to_sql_timeout = val.GetValue<int32_t>();
	}
	if (context.TryGetCurrentSetting("whisper_voice_query_show_sql", val)) {
		config.voice_query_show_sql = val.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("whisper_voice_query_timeout", val)) {
		config.voice_query_timeout = val.GetValue<int32_t>();
	}
#endif

	return config;
}

} // namespace duckdb
