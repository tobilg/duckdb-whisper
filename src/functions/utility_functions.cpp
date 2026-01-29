#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"

#include "duckdb/common/exception.hpp"

#include "audio_utils.hpp"
#include "whisper_config.hpp"
#include "whisper.h"

namespace duckdb {

// Extension version
#ifndef EXT_VERSION_WHISPER
#define EXT_VERSION_WHISPER "0.1.0"
#endif

// ============================================================================
// whisper_version() - Returns extension and whisper.cpp version info
// ============================================================================

static void WhisperVersionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    std::string version_info = "whisper extension v" + std::string(EXT_VERSION_WHISPER) +
                               " (whisper.cpp: " + std::string(whisper_version()) + ")";
    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<string_t>(result);
    *result_data = StringVector::AddString(result, version_info);
}

// ============================================================================
// whisper_check_audio(file_path) - Validates an audio file
// ============================================================================

static void WhisperCheckAudioFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &file_path_vec = args.data[0];
    idx_t count = args.size();

    UnaryExecutor::Execute<string_t, string_t>(file_path_vec, result, count, [&](string_t file_path_val) {
        std::string file_path = file_path_val.GetString();
        std::string error;

        if (AudioUtils::CheckAudioFile(file_path, error)) {
            return StringVector::AddString(result, "OK");
        } else {
            return StringVector::AddString(result, "Error: " + error);
        }
    });
}

// ============================================================================
// whisper_audio_info(file_path) - Table function with audio metadata
// ============================================================================

struct AudioInfoBindData : public TableFunctionData {
    std::string file_path;
};

struct AudioInfoState : public GlobalTableFunctionState {
    bool returned;

    AudioInfoState() : returned(false) {}

    idx_t MaxThreads() const override {
        return 1;
    }
};

static unique_ptr<FunctionData> AudioInfoBind(ClientContext &context, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {
    auto bind_data = make_uniq<AudioInfoBindData>();
    bind_data->file_path = input.inputs[0].GetValue<string>();

    return_types.push_back(LogicalType::VARCHAR);  // file_path
    names.push_back("file_path");

    return_types.push_back(LogicalType::DOUBLE);   // duration_seconds
    names.push_back("duration_seconds");

    return_types.push_back(LogicalType::INTEGER);  // sample_rate
    names.push_back("sample_rate");

    return_types.push_back(LogicalType::INTEGER);  // channels
    names.push_back("channels");

    return_types.push_back(LogicalType::VARCHAR);  // format
    names.push_back("format");

    return_types.push_back(LogicalType::BIGINT);   // file_size
    names.push_back("file_size");

    return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> AudioInfoInit(ClientContext &context, TableFunctionInitInput &input) {
    return make_uniq<AudioInfoState>();
}

static void AudioInfoExecute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
    auto &bind_data = data.bind_data->Cast<AudioInfoBindData>();
    auto &state = data.global_state->Cast<AudioInfoState>();

    if (state.returned) {
        output.SetCardinality(0);
        return;
    }

    AudioMetadata metadata;
    std::string error;

    if (!AudioUtils::GetAudioMetadata(bind_data.file_path, metadata, error)) {
        throw InvalidInputException("Failed to read audio info: " + error);
    }

    output.SetValue(0, 0, Value(bind_data.file_path));
    output.SetValue(1, 0, Value::DOUBLE(metadata.duration_seconds));
    output.SetValue(2, 0, Value::INTEGER(metadata.sample_rate));
    output.SetValue(3, 0, Value::INTEGER(metadata.channels));
    output.SetValue(4, 0, Value(metadata.format));
    output.SetValue(5, 0, Value::BIGINT(metadata.file_size));

    output.SetCardinality(1);
    state.returned = true;
}

// ============================================================================
// Configuration getter functions (read from DuckDB settings)
// ============================================================================

static void WhisperGetDeviceIdFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    int32_t device_id = WhisperConfig::DEFAULT_DEVICE_ID;

    if (context.TryGetCurrentSetting("whisper_device_id", val)) {
        device_id = val.GetValue<int32_t>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<int32_t>(result);
    *result_data = device_id;
}

static void WhisperGetMaxDurationFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    double max_duration = WhisperConfig::DEFAULT_MAX_DURATION;

    if (context.TryGetCurrentSetting("whisper_max_duration", val)) {
        max_duration = val.GetValue<double>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<double>(result);
    *result_data = max_duration;
}

static void WhisperGetSilenceDurationFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    double silence_duration = WhisperConfig::DEFAULT_SILENCE_DURATION;

    if (context.TryGetCurrentSetting("whisper_silence_duration", val)) {
        silence_duration = val.GetValue<double>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<double>(result);
    *result_data = silence_duration;
}

static void WhisperGetSilenceThresholdFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    double silence_threshold = WhisperConfig::DEFAULT_SILENCE_THRESHOLD;

    if (context.TryGetCurrentSetting("whisper_silence_threshold", val)) {
        silence_threshold = val.GetValue<double>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<double>(result);
    *result_data = silence_threshold;
}

static void WhisperGetConfigFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    auto config = WhisperConfigManager::GetConfig(context);

    std::string device_str = config.device_id < 0 ? "default" : std::to_string(config.device_id);
    std::string config_str = "model=" + config.model +
                             ", model_path=" + config.model_path +
                             ", language=" + config.language +
                             ", threads=" + std::to_string(config.threads) +
                             ", translate=" + (config.translate ? "true" : "false") +
                             ", device_id=" + device_str +
                             ", max_duration=" + std::to_string(config.max_duration) +
                             ", silence_duration=" + std::to_string(config.silence_duration) +
                             ", silence_threshold=" + std::to_string(config.silence_threshold) +
                             ", verbose=" + (config.verbose ? "true" : "false");

#ifdef WHISPER_ENABLE_VOICE_QUERY
    config_str += ", text_to_sql_url=" + config.text_to_sql_url +
                  ", text_to_sql_timeout=" + std::to_string(config.text_to_sql_timeout) +
                  ", voice_query_show_sql=" + (config.voice_query_show_sql ? "true" : "false");
#endif

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<string_t>(result);
    *result_data = StringVector::AddString(result, config_str);
}

#ifdef WHISPER_ENABLE_VOICE_QUERY
static void WhisperGetTextToSqlUrlFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    std::string url = WhisperConfig::DEFAULT_TEXT_TO_SQL_URL;

    if (context.TryGetCurrentSetting("whisper_text_to_sql_url", val)) {
        url = val.GetValue<string>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<string_t>(result);
    *result_data = StringVector::AddString(result, url);
}

static void WhisperGetTextToSqlTimeoutFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    int32_t timeout = WhisperConfig::DEFAULT_TEXT_TO_SQL_TIMEOUT;

    if (context.TryGetCurrentSetting("whisper_text_to_sql_timeout", val)) {
        timeout = val.GetValue<int32_t>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<int32_t>(result);
    *result_data = timeout;
}

static void WhisperGetVoiceQueryShowSqlFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    Value val;
    bool show_sql = WhisperConfig::DEFAULT_VOICE_QUERY_SHOW_SQL;

    if (context.TryGetCurrentSetting("whisper_voice_query_show_sql", val)) {
        show_sql = val.GetValue<bool>();
    }

    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::SetNull(result, false);
    auto result_data = ConstantVector::GetData<bool>(result);
    *result_data = show_sql;
}
#endif

// ============================================================================
// Registration
// ============================================================================

void RegisterUtilityFunctions(ExtensionLoader &loader) {
    // whisper_version()
    auto version_func = ScalarFunction("whisper_version", {}, LogicalType::VARCHAR, WhisperVersionFunction);
    loader.RegisterFunction(version_func);

    // whisper_check_audio(file_path)
    auto check_func = ScalarFunction("whisper_check_audio", {LogicalType::VARCHAR},
                                      LogicalType::VARCHAR, WhisperCheckAudioFunction);
    loader.RegisterFunction(check_func);

    // whisper_audio_info(file_path)
    TableFunction audio_info("whisper_audio_info", {LogicalType::VARCHAR}, AudioInfoExecute, AudioInfoBind, AudioInfoInit);
    loader.RegisterFunction(audio_info);

    // Configuration getter functions
    auto get_device_id = ScalarFunction("whisper_get_device_id", {}, LogicalType::INTEGER, WhisperGetDeviceIdFunction);
    loader.RegisterFunction(get_device_id);

    auto get_max_duration = ScalarFunction("whisper_get_max_duration", {}, LogicalType::DOUBLE, WhisperGetMaxDurationFunction);
    loader.RegisterFunction(get_max_duration);

    auto get_silence_duration = ScalarFunction("whisper_get_silence_duration", {}, LogicalType::DOUBLE, WhisperGetSilenceDurationFunction);
    loader.RegisterFunction(get_silence_duration);

    auto get_silence_threshold = ScalarFunction("whisper_get_silence_threshold", {}, LogicalType::DOUBLE, WhisperGetSilenceThresholdFunction);
    loader.RegisterFunction(get_silence_threshold);

    auto get_config = ScalarFunction("whisper_get_config", {}, LogicalType::VARCHAR, WhisperGetConfigFunction);
    loader.RegisterFunction(get_config);

#ifdef WHISPER_ENABLE_VOICE_QUERY
    // Voice query configuration getter functions
    auto get_text_to_sql_url = ScalarFunction("whisper_get_text_to_sql_url", {},
                                               LogicalType::VARCHAR, WhisperGetTextToSqlUrlFunction);
    loader.RegisterFunction(get_text_to_sql_url);

    auto get_text_to_sql_timeout = ScalarFunction("whisper_get_text_to_sql_timeout", {},
                                                   LogicalType::INTEGER, WhisperGetTextToSqlTimeoutFunction);
    loader.RegisterFunction(get_text_to_sql_timeout);

    auto get_voice_query_show_sql = ScalarFunction("whisper_get_voice_query_show_sql", {},
                                                    LogicalType::BOOLEAN, WhisperGetVoiceQueryShowSqlFunction);
    loader.RegisterFunction(get_voice_query_show_sql);
#endif
}

} // namespace duckdb
