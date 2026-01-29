#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

#include "duckdb/common/exception.hpp"

#include "transcription_engine.hpp"
#include "whisper_config.hpp"

namespace duckdb {

static void WhisperTranscribeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    auto config = WhisperConfigManager::GetConfig(context);

    auto &input = args.data[0];
    idx_t count = args.size();

    // Check if optional model parameter is provided
    bool has_model_param = args.ColumnCount() > 1;

    UnaryExecutor::Execute<string_t, string_t>(input, result, count, [&](string_t input_val) {
        // Override model if parameter provided
        WhisperConfig local_config = config;
        if (has_model_param) {
            auto &model_vec = args.data[1];
            if (model_vec.GetType().id() == LogicalTypeId::VARCHAR) {
                auto model_val = FlatVector::GetData<string_t>(model_vec)[0];
                local_config.model = model_val.GetString();
            }
        }

        // Check if input is a file path (VARCHAR) or BLOB
        TranscriptionResult transcription;

        std::string input_str = input_val.GetString();

        // Try to transcribe as file path
        transcription = TranscriptionEngine::TranscribeFile(input_str, local_config);

        if (!transcription.success) {
            throw InvalidInputException("Transcription failed: " + transcription.error);
        }

        return StringVector::AddString(result, transcription.full_text);
    });
}

static void WhisperTranscribeBlobFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    auto config = WhisperConfigManager::GetConfig(context);

    auto &input = args.data[0];
    idx_t count = args.size();

    // Check if optional model parameter is provided
    bool has_model_param = args.ColumnCount() > 1;

    UnaryExecutor::Execute<string_t, string_t>(input, result, count, [&](string_t blob_val) {
        // Override model if parameter provided
        WhisperConfig local_config = config;
        if (has_model_param) {
            auto &model_vec = args.data[1];
            if (model_vec.GetType().id() == LogicalTypeId::VARCHAR) {
                auto model_val = FlatVector::GetData<string_t>(model_vec)[0];
                local_config.model = model_val.GetString();
            }
        }

        // Transcribe from BLOB
        TranscriptionResult transcription = TranscriptionEngine::TranscribeMemory(
            reinterpret_cast<const uint8_t *>(blob_val.GetData()),
            blob_val.GetSize(),
            local_config
        );

        if (!transcription.success) {
            throw InvalidInputException("Transcription failed: " + transcription.error);
        }

        return StringVector::AddString(result, transcription.full_text);
    });
}

// whisper_translate - translates audio to English
static void WhisperTranslateFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    auto config = WhisperConfigManager::GetConfig(context);
    config.translate = true;  // Enable translation to English

    auto &input = args.data[0];
    idx_t count = args.size();

    // Check if optional model parameter is provided
    bool has_model_param = args.ColumnCount() > 1;

    UnaryExecutor::Execute<string_t, string_t>(input, result, count, [&](string_t input_val) {
        WhisperConfig local_config = config;
        if (has_model_param) {
            auto &model_vec = args.data[1];
            if (model_vec.GetType().id() == LogicalTypeId::VARCHAR) {
                auto model_val = FlatVector::GetData<string_t>(model_vec)[0];
                local_config.model = model_val.GetString();
            }
        }

        std::string input_str = input_val.GetString();
        TranscriptionResult transcription = TranscriptionEngine::TranscribeFile(input_str, local_config);

        if (!transcription.success) {
            throw InvalidInputException("Translation failed: " + transcription.error);
        }

        return StringVector::AddString(result, transcription.full_text);
    });
}

static void WhisperTranslateBlobFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &context = state.GetContext();
    auto config = WhisperConfigManager::GetConfig(context);
    config.translate = true;  // Enable translation to English

    auto &input = args.data[0];
    idx_t count = args.size();

    bool has_model_param = args.ColumnCount() > 1;

    UnaryExecutor::Execute<string_t, string_t>(input, result, count, [&](string_t blob_val) {
        WhisperConfig local_config = config;
        if (has_model_param) {
            auto &model_vec = args.data[1];
            if (model_vec.GetType().id() == LogicalTypeId::VARCHAR) {
                auto model_val = FlatVector::GetData<string_t>(model_vec)[0];
                local_config.model = model_val.GetString();
            }
        }

        TranscriptionResult transcription = TranscriptionEngine::TranscribeMemory(
            reinterpret_cast<const uint8_t *>(blob_val.GetData()),
            blob_val.GetSize(),
            local_config
        );

        if (!transcription.success) {
            throw InvalidInputException("Translation failed: " + transcription.error);
        }

        return StringVector::AddString(result, transcription.full_text);
    });
}

void RegisterTranscribeScalarFunctions(ExtensionLoader &loader) {
    // whisper_transcribe(file_path VARCHAR) -> VARCHAR
    ScalarFunctionSet transcribe_set("whisper_transcribe");

    // Version with just file path
    transcribe_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        WhisperTranscribeFunction
    ));

    // Version with file path and model override
    transcribe_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR, LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        WhisperTranscribeFunction
    ));

    // Version with BLOB
    transcribe_set.AddFunction(ScalarFunction(
        {LogicalType::BLOB},
        LogicalType::VARCHAR,
        WhisperTranscribeBlobFunction
    ));

    // Version with BLOB and model override
    transcribe_set.AddFunction(ScalarFunction(
        {LogicalType::BLOB, LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        WhisperTranscribeBlobFunction
    ));

    loader.RegisterFunction(transcribe_set);

    // whisper_translate(file_path VARCHAR, [model VARCHAR]) -> VARCHAR
    // Translates audio from any language to English
    ScalarFunctionSet translate_set("whisper_translate");

    // Version with just file path
    translate_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        WhisperTranslateFunction
    ));

    // Version with file path and model override
    translate_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR, LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        WhisperTranslateFunction
    ));

    // Version with BLOB
    translate_set.AddFunction(ScalarFunction(
        {LogicalType::BLOB},
        LogicalType::VARCHAR,
        WhisperTranslateBlobFunction
    ));

    // Version with BLOB and model override
    translate_set.AddFunction(ScalarFunction(
        {LogicalType::BLOB, LogicalType::VARCHAR},
        LogicalType::VARCHAR,
        WhisperTranslateBlobFunction
    ));

    loader.RegisterFunction(translate_set);
}

} // namespace duckdb
