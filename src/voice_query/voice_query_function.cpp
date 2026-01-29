#ifdef WHISPER_ENABLE_VOICE_QUERY

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
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
// Shared bind data for voice query table functions
// ============================================================================

struct VoiceQueryBindData : public TableFunctionData {
	std::string generated_sql;
	std::string transcription;
	bool include_metadata; // true for whisper_voice_query_with_sql
	vector<LogicalType> result_types;
	vector<string> result_names;
	// Separate connection to avoid deadlock when executing generated SQL
	shared_ptr<Connection> query_connection;
};

struct VoiceQueryState : public GlobalTableFunctionState {
	unique_ptr<QueryResult> query_result;
	unique_ptr<DataChunk> current_chunk;
	idx_t current_row;
	bool finished;

	VoiceQueryState() : current_row(0), finished(false) {
	}

	idx_t MaxThreads() const override {
		return 1;
	}
};

// ============================================================================
// Helper function to record, transcribe, and generate SQL
// ============================================================================

static void RecordAndGenerateSQL(const WhisperConfig &config, int device_id, const std::string &ddl,
                                 std::string &out_sql, std::string &out_transcription) {
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

	out_transcription = transcription.full_text;
	if (out_transcription.empty()) {
		throw InvalidInputException("No speech detected. Please try again.");
	}

	if (config.verbose) {
		std::string trimmed_transcription = out_transcription;
		StringUtil::Trim(trimmed_transcription);
		Printer::Print(OutputStream::STREAM_STDERR, "Transcribed: '" + trimmed_transcription + "'");
	}

	// Step 3: Call text-to-sql proxy (DDL already extracted on main thread)
	HttpClient client;
	std::string json_body = BuildJsonRequest(ddl, out_transcription);

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
	out_sql = ParseSqlFromJson(response.body);

	if (out_sql.empty()) {
		throw InvalidInputException("Text-to-SQL proxy error: No SQL in response. Response: " + response.body);
	}
}

// ============================================================================
// Wrapper with timeout using std::async
// ============================================================================

static void RecordAndGenerateSQLWithTimeout(ClientContext &context, const WhisperConfig &config, int device_id,
                                            std::string &out_sql, std::string &out_transcription) {
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
	std::string result_transcription;
	std::exception_ptr exception_ptr = nullptr;

	auto future = std::async(std::launch::async, [&]() {
		try {
			RecordAndGenerateSQL(config, device_id, ddl, result_sql, result_transcription);
		} catch (...) {
			exception_ptr = std::current_exception();
		}
	});

	auto timeout = std::chrono::seconds(config.voice_query_timeout);
	if (future.wait_for(timeout) == std::future_status::timeout) {
		throw InvalidInputException("Voice query timed out after " + std::to_string(config.voice_query_timeout) +
		                            " seconds. Increase whisper_voice_query_timeout if needed.");
	}

	// Get the result (this will also propagate any exception)
	future.get();

	if (exception_ptr) {
		std::rethrow_exception(exception_ptr);
	}

	out_sql = std::move(result_sql);
	out_transcription = std::move(result_transcription);
}

// ============================================================================
// whisper_voice_query([model VARCHAR], [device_id INTEGER]) -> TABLE (dynamic)
// ============================================================================

static unique_ptr<FunctionData> VoiceQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<VoiceQueryBindData>();
	bind_data->include_metadata = false;

	// Get configuration
	auto config = WhisperConfigManager::GetConfig(context);

	// Parse optional parameters
	if (input.inputs.size() > 0 && !input.inputs[0].IsNull()) {
		config.model = input.inputs[0].GetValue<string>();
	}

	// Use configured device_id as default, override if parameter provided
	int device_id = config.device_id;
	if (input.inputs.size() > 1 && !input.inputs[1].IsNull()) {
		device_id = input.inputs[1].GetValue<int32_t>();
	}

	// Record, transcribe, and generate SQL (with timeout)
	RecordAndGenerateSQLWithTimeout(context, config, device_id, bind_data->generated_sql, bind_data->transcription);

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "SQL: " + bind_data->generated_sql);
		Printer::Print(OutputStream::STREAM_STDERR, "Preparing SQL...");
	}

	// Create a separate connection to avoid deadlock
	// The current context is busy binding this query, so we need a fresh connection
	auto &db = DatabaseInstance::GetDatabase(context);
	bind_data->query_connection = make_shared_ptr<Connection>(db);

	// Prepare the generated SQL using the new connection to get column types
	auto prepared = bind_data->query_connection->Prepare(bind_data->generated_sql);
	if (prepared->HasError()) {
		throw InvalidInputException("Generated SQL failed: " + prepared->GetError() +
		                            "\nSQL: " + bind_data->generated_sql);
	}

	if (config.verbose) {
		Printer::Print(OutputStream::STREAM_STDERR, "SQL prepared");
	}

	// Get the result types and names from the prepared statement
	auto &types = prepared->GetTypes();
	auto &column_names = prepared->GetNames();

	for (idx_t i = 0; i < types.size(); i++) {
		return_types.push_back(types[i]);
		names.push_back(column_names[i]);
		bind_data->result_types.push_back(types[i]);
		bind_data->result_names.push_back(column_names[i]);
	}

	return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> VoiceQueryInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<VoiceQueryState>();
}

static void VoiceQueryExecute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &bind_data = data.bind_data->Cast<VoiceQueryBindData>();
	auto &state = data.global_state->Cast<VoiceQueryState>();

	if (state.finished) {
		output.SetCardinality(0);
		return;
	}

	// Execute the generated SQL if we haven't already
	// Use the separate connection created during bind to avoid deadlock
	if (!state.query_result) {
		state.query_result = bind_data.query_connection->Query(bind_data.generated_sql);
		if (state.query_result->HasError()) {
			throw InvalidInputException("Generated SQL failed: " + state.query_result->GetError() +
			                            "\nSQL: " + bind_data.generated_sql);
		}
	}

	// Fetch results
	if (!state.current_chunk || state.current_row >= state.current_chunk->size()) {
		state.current_chunk = state.query_result->Fetch();
		state.current_row = 0;

		if (!state.current_chunk || state.current_chunk->size() == 0) {
			state.finished = true;
			output.SetCardinality(0);
			return;
		}
	}

	// Copy data to output chunk
	idx_t output_count = 0;
	idx_t col_offset = bind_data.include_metadata ? 2 : 0;

	while (state.current_row < state.current_chunk->size() && output_count < STANDARD_VECTOR_SIZE) {
		// If including metadata columns, set them for each row
		if (bind_data.include_metadata) {
			output.SetValue(0, output_count, Value(bind_data.generated_sql));
			output.SetValue(1, output_count, Value(bind_data.transcription));
		}

		// Copy result columns
		for (idx_t col = 0; col < state.current_chunk->ColumnCount(); col++) {
			output.SetValue(col + col_offset, output_count, state.current_chunk->GetValue(col, state.current_row));
		}

		state.current_row++;
		output_count++;
	}

	output.SetCardinality(output_count);

	// Check if we need to fetch more
	if (state.current_row >= state.current_chunk->size()) {
		state.current_chunk = state.query_result->Fetch();
		state.current_row = 0;

		if (!state.current_chunk || state.current_chunk->size() == 0) {
			state.finished = true;
		}
	}
}

// ============================================================================
// whisper_voice_query_with_sql([model], [device_id]) -> TABLE (_generated_sql, _transcription, ...)
// ============================================================================

static unique_ptr<FunctionData> VoiceQueryWithSqlBind(ClientContext &context, TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<VoiceQueryBindData>();
	bind_data->include_metadata = true;

	// Get configuration
	auto config = WhisperConfigManager::GetConfig(context);

	// Parse optional parameters
	if (input.inputs.size() > 0 && !input.inputs[0].IsNull()) {
		config.model = input.inputs[0].GetValue<string>();
	}

	// Use configured device_id as default, override if parameter provided
	int device_id = config.device_id;
	if (input.inputs.size() > 1 && !input.inputs[1].IsNull()) {
		device_id = input.inputs[1].GetValue<int32_t>();
	}

	// Record, transcribe, and generate SQL (with timeout)
	RecordAndGenerateSQLWithTimeout(context, config, device_id, bind_data->generated_sql, bind_data->transcription);

	// Add metadata columns first
	return_types.push_back(LogicalType::VARCHAR);
	names.push_back("_generated_sql");
	bind_data->result_types.push_back(LogicalType::VARCHAR);
	bind_data->result_names.push_back("_generated_sql");

	return_types.push_back(LogicalType::VARCHAR);
	names.push_back("_transcription");
	bind_data->result_types.push_back(LogicalType::VARCHAR);
	bind_data->result_names.push_back("_transcription");

	// Create a separate connection to avoid deadlock
	auto &db = DatabaseInstance::GetDatabase(context);
	bind_data->query_connection = make_shared_ptr<Connection>(db);

	// Prepare the generated SQL using the new connection to get column types
	auto prepared = bind_data->query_connection->Prepare(bind_data->generated_sql);
	if (prepared->HasError()) {
		throw InvalidInputException("Generated SQL failed: " + prepared->GetError() +
		                            "\nSQL: " + bind_data->generated_sql);
	}

	// Get the result types and names from the prepared statement
	auto &types = prepared->GetTypes();
	auto &column_names = prepared->GetNames();

	for (idx_t i = 0; i < types.size(); i++) {
		return_types.push_back(types[i]);
		names.push_back(column_names[i]);
		bind_data->result_types.push_back(types[i]);
		bind_data->result_names.push_back(column_names[i]);
	}

	return std::move(bind_data);
}

// ============================================================================
// Registration
// ============================================================================

void RegisterVoiceQueryFunctions(ExtensionLoader &loader) {
	// whisper_voice_query([model VARCHAR], [device_id INTEGER]) -> TABLE (dynamic)
	TableFunctionSet voice_query_set("whisper_voice_query");

	// No parameters
	TableFunction voice_query_no_args({}, VoiceQueryExecute, VoiceQueryBind, VoiceQueryInit);
	voice_query_set.AddFunction(voice_query_no_args);

	// With model parameter
	TableFunction voice_query_with_model({LogicalType::VARCHAR}, VoiceQueryExecute, VoiceQueryBind, VoiceQueryInit);
	voice_query_set.AddFunction(voice_query_with_model);

	// With model and device_id parameters
	TableFunction voice_query_with_device({LogicalType::VARCHAR, LogicalType::INTEGER}, VoiceQueryExecute,
	                                      VoiceQueryBind, VoiceQueryInit);
	voice_query_set.AddFunction(voice_query_with_device);

	loader.RegisterFunction(voice_query_set);

	// whisper_voice_query_with_sql([model VARCHAR], [device_id INTEGER]) -> TABLE (_generated_sql, _transcription, ...)
	TableFunctionSet voice_query_with_sql_set("whisper_voice_query_with_sql");

	// No parameters
	TableFunction voice_query_sql_no_args({}, VoiceQueryExecute, VoiceQueryWithSqlBind, VoiceQueryInit);
	voice_query_with_sql_set.AddFunction(voice_query_sql_no_args);

	// With model parameter
	TableFunction voice_query_sql_with_model({LogicalType::VARCHAR}, VoiceQueryExecute, VoiceQueryWithSqlBind,
	                                         VoiceQueryInit);
	voice_query_with_sql_set.AddFunction(voice_query_sql_with_model);

	// With model and device_id parameters
	TableFunction voice_query_sql_with_device({LogicalType::VARCHAR, LogicalType::INTEGER}, VoiceQueryExecute,
	                                          VoiceQueryWithSqlBind, VoiceQueryInit);
	voice_query_with_sql_set.AddFunction(voice_query_sql_with_device);

	loader.RegisterFunction(voice_query_with_sql_set);
}

} // namespace duckdb

#endif // WHISPER_ENABLE_VOICE_QUERY
