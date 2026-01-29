#define DUCKDB_EXTENSION_MAIN

#include "whisper_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"

#include "whisper_config.hpp"

#ifdef WHISPER_ENABLE_VOICE_QUERY
#include <curl/curl.h>
#endif

namespace duckdb {

// Forward declarations for function registration
void RegisterModelFunctions(ExtensionLoader &loader);
void RegisterTranscribeScalarFunctions(ExtensionLoader &loader);
void RegisterTranscribeTableFunctions(ExtensionLoader &loader);
void RegisterUtilityFunctions(ExtensionLoader &loader);

#ifdef WHISPER_ENABLE_RECORDING
void RegisterRecordFunctions(ExtensionLoader &loader);
#endif

#ifdef WHISPER_ENABLE_VOICE_QUERY
void RegisterVoiceToSqlFunction(ExtensionLoader &loader);
void RegisterVoiceQueryFunctions(ExtensionLoader &loader);
#endif

static void LoadInternal(ExtensionLoader &loader) {
#ifdef WHISPER_ENABLE_VOICE_QUERY
	// Initialize libcurl globally (required before any curl operations)
	curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

	// Register configuration settings FIRST
	WhisperConfigManager::RegisterSettings(loader.GetDatabaseInstance());

	// Register all functions
	RegisterModelFunctions(loader);
	RegisterTranscribeScalarFunctions(loader);
	RegisterTranscribeTableFunctions(loader);
	RegisterUtilityFunctions(loader);

#ifdef WHISPER_ENABLE_RECORDING
	RegisterRecordFunctions(loader);
#endif

#ifdef WHISPER_ENABLE_VOICE_QUERY
	RegisterVoiceToSqlFunction(loader);
	RegisterVoiceQueryFunctions(loader);
#endif
}

void WhisperExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string WhisperExtension::Name() {
	return "whisper";
}

std::string WhisperExtension::Version() const {
#ifdef EXT_VERSION_WHISPER
	return EXT_VERSION_WHISPER;
#else
	return "0.1.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(whisper, loader) {
	duckdb::LoadInternal(loader);
}
}
