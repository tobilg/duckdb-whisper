#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include "transcription_engine.hpp"
#include "whisper_config.hpp"

namespace duckdb {

struct TranscribeSegmentsBindData : public TableFunctionData {
    std::string file_path;
    std::vector<uint8_t> blob_data;
    bool is_blob;
    std::string model_override;
    std::string language_override;
    bool translate;  // Translate to English instead of transcribe
};

struct TranscribeSegmentsState : public GlobalTableFunctionState {
    TranscriptionResult result;
    idx_t current_segment;
    bool initialized;

    TranscribeSegmentsState() : current_segment(0), initialized(false) {}

    idx_t MaxThreads() const override {
        return 1;  // Single-threaded for now
    }
};

static unique_ptr<FunctionData> TranscribeSegmentsBind(ClientContext &context, TableFunctionBindInput &input,
                                                        vector<LogicalType> &return_types, vector<string> &names) {
    auto bind_data = make_uniq<TranscribeSegmentsBindData>();

    // Get input argument
    if (input.inputs[0].type().id() == LogicalTypeId::BLOB) {
        bind_data->is_blob = true;
        auto blob = input.inputs[0].GetValue<string>();
        bind_data->blob_data.assign(blob.begin(), blob.end());
    } else {
        bind_data->is_blob = false;
        bind_data->file_path = input.inputs[0].GetValue<string>();
    }

    // Check for optional parameters
    if (input.inputs.size() > 1 && !input.inputs[1].IsNull()) {
        bind_data->model_override = input.inputs[1].GetValue<string>();
    }

    if (input.inputs.size() > 2 && !input.inputs[2].IsNull()) {
        bind_data->language_override = input.inputs[2].GetValue<string>();
    }

    // Check for translate parameter (4th argument)
    bind_data->translate = false;
    if (input.inputs.size() > 3 && !input.inputs[3].IsNull()) {
        bind_data->translate = input.inputs[3].GetValue<bool>();
    }

    // Define output columns
    return_types.push_back(LogicalType::INTEGER);   // segment_id
    names.push_back("segment_id");

    return_types.push_back(LogicalType::DOUBLE);    // start_time
    names.push_back("start_time");

    return_types.push_back(LogicalType::DOUBLE);    // end_time
    names.push_back("end_time");

    return_types.push_back(LogicalType::VARCHAR);   // text
    names.push_back("text");

    return_types.push_back(LogicalType::DOUBLE);    // confidence
    names.push_back("confidence");

    return_types.push_back(LogicalType::VARCHAR);   // language
    names.push_back("language");

    return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> TranscribeSegmentsInit(ClientContext &context,
                                                                    TableFunctionInitInput &input) {
    return make_uniq<TranscribeSegmentsState>();
}

static void TranscribeSegmentsExecute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
    auto &bind_data = data.bind_data->Cast<TranscribeSegmentsBindData>();
    auto &state = data.global_state->Cast<TranscribeSegmentsState>();

    // Initialize on first call
    if (!state.initialized) {
        auto config = WhisperConfigManager::GetConfig(context);

        // Apply overrides
        if (!bind_data.model_override.empty()) {
            config.model = bind_data.model_override;
        }
        if (!bind_data.language_override.empty()) {
            config.language = bind_data.language_override;
        }
        config.translate = bind_data.translate;

        // Perform transcription
        if (bind_data.is_blob) {
            state.result = TranscriptionEngine::TranscribeMemory(
                bind_data.blob_data.data(),
                bind_data.blob_data.size(),
                config
            );
        } else {
            state.result = TranscriptionEngine::TranscribeFile(bind_data.file_path, config);
        }

        if (!state.result.success) {
            throw InvalidInputException("Transcription failed: " + state.result.error);
        }

        state.initialized = true;
    }

    // Output segments
    idx_t output_idx = 0;
    while (state.current_segment < state.result.segments.size() && output_idx < STANDARD_VECTOR_SIZE) {
        const auto &segment = state.result.segments[state.current_segment];

        output.SetValue(0, output_idx, Value::INTEGER(segment.segment_id));
        output.SetValue(1, output_idx, Value::DOUBLE(segment.start_time));
        output.SetValue(2, output_idx, Value::DOUBLE(segment.end_time));
        output.SetValue(3, output_idx, Value(segment.text));
        output.SetValue(4, output_idx, Value::DOUBLE(segment.confidence));
        output.SetValue(5, output_idx, Value(segment.language));

        state.current_segment++;
        output_idx++;
    }

    output.SetCardinality(output_idx);
}

void RegisterTranscribeTableFunctions(ExtensionLoader &loader) {
    // whisper_transcribe_segments(file_path VARCHAR, model? VARCHAR, language? VARCHAR, translate? BOOLEAN) -> TABLE
    TableFunctionSet transcribe_segments_set("whisper_transcribe_segments");

    // Version with just file path
    TableFunction tf1({LogicalType::VARCHAR}, TranscribeSegmentsExecute, TranscribeSegmentsBind,
                      TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf1);

    // Version with file path and model
    TableFunction tf2({LogicalType::VARCHAR, LogicalType::VARCHAR}, TranscribeSegmentsExecute,
                      TranscribeSegmentsBind, TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf2);

    // Version with file path, model, and language
    TableFunction tf3({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
                      TranscribeSegmentsExecute, TranscribeSegmentsBind, TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf3);

    // Version with file path, model, language, and translate
    TableFunction tf3t({LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BOOLEAN},
                       TranscribeSegmentsExecute, TranscribeSegmentsBind, TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf3t);

    // BLOB versions
    TableFunction tf4({LogicalType::BLOB}, TranscribeSegmentsExecute, TranscribeSegmentsBind,
                      TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf4);

    TableFunction tf5({LogicalType::BLOB, LogicalType::VARCHAR}, TranscribeSegmentsExecute,
                      TranscribeSegmentsBind, TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf5);

    TableFunction tf6({LogicalType::BLOB, LogicalType::VARCHAR, LogicalType::VARCHAR},
                      TranscribeSegmentsExecute, TranscribeSegmentsBind, TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf6);

    // BLOB version with translate
    TableFunction tf6t({LogicalType::BLOB, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::BOOLEAN},
                       TranscribeSegmentsExecute, TranscribeSegmentsBind, TranscribeSegmentsInit);
    transcribe_segments_set.AddFunction(tf6t);

    loader.RegisterFunction(transcribe_segments_set);
}

} // namespace duckdb
