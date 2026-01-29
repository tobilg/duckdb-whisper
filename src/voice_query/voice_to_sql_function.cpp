#ifdef WHISPER_ENABLE_VOICE_QUERY

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/printer.hpp"
#include "duckdb/common/string_util.hpp"

#include "whisper_config.hpp"
#include "audio_recorder.hpp"
#include "transcription_engine.hpp"
#include "http_client.hpp"

#include <future>
#include <chrono>

namespace duckdb {

// Forward declaration from ddl_extractor.cpp
std::string ExtractDatabaseDDL(ClientContext &context);

// ============================================================================
// Helper function to perform voice-to-sql operation
// ============================================================================

static std::string PerformVoiceToSql(const WhisperConfig &config, int device_id, const std::string &ddl) {
	// Step 1: Record audio until silence
	AudioRecorder recorder;
	std::string error;
	std::vector<float> pcm_data;

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Listening...");
	}

	// Record with automatic silence detection using configured values
	if (!recorder.RecordUntilSilence(pcm_data, config.max_duration, config.silence_duration,
	                                 static_cast<float>(config.silence_threshold), device_id, error)) {
		throw InvalidInputException("Failed to record audio: " + error);
	}

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Stopped");
	}

	if (pcm_data.empty()) {
		throw InvalidInputException("No speech detected. Please try again.");
	}

	// Step 2: Transcribe the audio
	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Transcribing...");
	}

	TranscriptionResult transcription = TranscriptionEngine::TranscribePCM(pcm_data, config);

	if (!transcription.success) {
		throw InvalidInputException("Transcription failed: " + transcription.error);
	}

	std::string question = transcription.full_text;
	if (question.empty()) {
		throw InvalidInputException("No speech detected. Please try again.");
	}

	if (config.verbose) {
		std::string trimmed_question = question;
		StringUtil::Trim(trimmed_question);
		Printer::Print(OutputStream::STREAM_STDERR, "Transcribed: '" + trimmed_question + "'");
	}

	// Step 3: Call text-to-sql proxy (DDL already extracted on main thread)
	HttpClient client;
	std::string json_body = BuildJsonRequest(ddl, question);

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Text-to-SQL request sent...");
	}

	HttpResponse response = client.Post(config.text_to_sql_url, json_body, config.text_to_sql_timeout);

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Text-to-SQL response received");
	}

	if (!response.success) {
		throw InvalidInputException(response.error);
	}

	// Step 4: Parse SQL from response
	std::string generated_sql = ParseSqlFromJson(response.body);

	if (generated_sql.empty()) {
		throw InvalidInputException("Text-to-SQL proxy error: No SQL in response. Response: " + response.body);
	}

	return generated_sql;
}

// ============================================================================
// Wrapper with timeout using std::async
// ============================================================================

static std::string PerformVoiceToSqlWithTimeout(ClientContext &context, const WhisperConfig &config, int device_id) {
	// Extract DDL on main thread first (ClientContext is not thread-safe)
	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Reading schema...");
	}

	std::string ddl = ExtractDatabaseDDL(context);

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "Schema read");
	}

	// Use std::async to run the rest of the operation with a timeout
	std::string result_sql;
	std::exception_ptr exception_ptr = nullptr;

	auto future = std::async(std::launch::async, [&]() {
		try {
			result_sql = PerformVoiceToSql(config, device_id, ddl);
		} catch (...) {
			exception_ptr = std::current_exception();
		}
	});

	auto timeout = std::chrono::seconds(config.voice_query_timeout);
	if (future.wait_for(timeout) == std::future_status::timeout) {
		throw InvalidInputException("Voice query timed out after " + std::to_string(config.voice_query_timeout) +
		                            " seconds. Increase whisper_voice_query_timeout if needed.");
	}

	future.get();

	if (exception_ptr) {
		std::rethrow_exception(exception_ptr);
	}

	return result_sql;
}

// ============================================================================
// whisper_voice_to_sql([model VARCHAR], [device_id INTEGER]) -> VARCHAR
// Records audio, transcribes it, and returns the generated SQL
// ============================================================================

static void VoiceToSqlFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);

	idx_t count = args.size();
	idx_t col_count = args.ColumnCount();

	// Parse optional parameters
	bool has_model = col_count > 0;
	bool has_device = col_count > 1;

	for (idx_t i = 0; i < count; i++) {
		WhisperConfig local_config = config;

		// Get optional model parameter
		if (has_model && !FlatVector::IsNull(args.data[0], i)) {
			auto model_val = FlatVector::GetData<string_t>(args.data[0])[i];
			local_config.model = model_val.GetString();
		}

		// Use configured device_id as default, override if parameter provided
		int device_id = local_config.device_id;
		if (has_device && !FlatVector::IsNull(args.data[1], i)) {
			device_id = FlatVector::GetData<int32_t>(args.data[1])[i];
		}

		// Perform voice-to-sql with timeout
		std::string generated_sql = PerformVoiceToSqlWithTimeout(context, local_config, device_id);

		FlatVector::GetData<string_t>(result)[i] = StringVector::AddString(result, generated_sql);
	}
}

// ============================================================================
// Registration
// ============================================================================

void RegisterVoiceToSqlFunction(ExtensionLoader &loader) {
	ScalarFunctionSet voice_to_sql_set("whisper_voice_to_sql");

	// No parameters - use defaults
	voice_to_sql_set.AddFunction(ScalarFunction({}, LogicalType::VARCHAR, VoiceToSqlFunction));

	// With model parameter
	voice_to_sql_set.AddFunction(ScalarFunction({LogicalType::VARCHAR}, LogicalType::VARCHAR, VoiceToSqlFunction));

	// With model and device_id parameters
	voice_to_sql_set.AddFunction(
	    ScalarFunction({LogicalType::VARCHAR, LogicalType::INTEGER}, LogicalType::VARCHAR, VoiceToSqlFunction));

	loader.RegisterFunction(voice_to_sql_set);
}

} // namespace duckdb

#endif // WHISPER_ENABLE_VOICE_QUERY
