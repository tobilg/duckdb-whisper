#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

#include "duckdb/common/exception.hpp"

#include "function_description.hpp"
#include "transcription_engine.hpp"
#include "whisper_config.hpp"

namespace duckdb {

// Shared implementation of whisper_transcribe and whisper_translate for file path (VARCHAR) and BLOB input.
// The optional model argument is read per row, so a model column applies to its own row only.
static void ExecuteTranscription(DataChunk &args, ExpressionState &state, Vector &result, bool is_blob,
                                 bool translate) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);
	if (translate) {
		config.translate = true; // Enable translation to English
	}

	auto transcribe = [&](string_t audio, const WhisperConfig &local_config) {
		TranscriptionResult transcription;
		if (is_blob) {
			transcription = TranscriptionEngine::TranscribeMemory(reinterpret_cast<const uint8_t *>(audio.GetData()),
			                                                      audio.GetSize(), local_config);
		} else {
			transcription = TranscriptionEngine::TranscribeFile(audio.GetString(), local_config);
		}

		if (!transcription.success) {
			throw InvalidInputException((translate ? "Translation failed: " : "Transcription failed: ") +
			                            transcription.error);
		}

		return StringVector::AddString(result, transcription.full_text);
	};

	if (args.ColumnCount() == 1) {
		UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),
		                                           [&](string_t audio) { return transcribe(audio, config); });
	} else {
		// Override model with the per-row model argument
		BinaryExecutor::Execute<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),
		                                                      [&](string_t audio, string_t model) {
			                                                      WhisperConfig local_config = config;
			                                                      local_config.model = model.GetString();
			                                                      return transcribe(audio, local_config);
		                                                      });
	}
}

static void WhisperTranscribeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteTranscription(args, state, result, false, false);
}

static void WhisperTranscribeBlobFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteTranscription(args, state, result, true, false);
}

// whisper_translate - translates audio to English
static void WhisperTranslateFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteTranscription(args, state, result, false, true);
}

static void WhisperTranslateBlobFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	ExecuteTranscription(args, state, result, true, true);
}

void RegisterTranscribeScalarFunctions(ExtensionLoader &loader) {
	// whisper_transcribe(file_path VARCHAR) -> VARCHAR
	ScalarFunctionSet transcribe_set("whisper_transcribe");

	// Version with just file path
	transcribe_set.AddFunction(
	    MakeIOScalarFunction({LogicalType::VARCHAR}, LogicalType::VARCHAR, WhisperTranscribeFunction));

	// Version with file path and model override
	transcribe_set.AddFunction(MakeIOScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                                WhisperTranscribeFunction));

	// Version with BLOB
	transcribe_set.AddFunction(
	    MakeIOScalarFunction({LogicalType::BLOB}, LogicalType::VARCHAR, WhisperTranscribeBlobFunction));

	// Version with BLOB and model override
	transcribe_set.AddFunction(MakeIOScalarFunction({LogicalType::BLOB, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                                WhisperTranscribeBlobFunction));

	RegisterScalarFunction(
	    loader, std::move(transcribe_set),
	    MakeFunctionDescription({"audio", "model"},
	                            "Transcribes speech from an audio file path (VARCHAR) or from audio bytes (BLOB) "
	                            "and returns the full text. The optional model overrides the whisper_model setting.",
	                            {"whisper_transcribe('audio.wav', 'tiny.en')"}, {"whisper", "transcription"}));

	// whisper_translate(file_path VARCHAR, [model VARCHAR]) -> VARCHAR
	// Translates audio from any language to English
	ScalarFunctionSet translate_set("whisper_translate");

	// Version with just file path
	translate_set.AddFunction(
	    MakeIOScalarFunction({LogicalType::VARCHAR}, LogicalType::VARCHAR, WhisperTranslateFunction));

	// Version with file path and model override
	translate_set.AddFunction(MakeIOScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                               WhisperTranslateFunction));

	// Version with BLOB
	translate_set.AddFunction(
	    MakeIOScalarFunction({LogicalType::BLOB}, LogicalType::VARCHAR, WhisperTranslateBlobFunction));

	// Version with BLOB and model override
	translate_set.AddFunction(MakeIOScalarFunction({LogicalType::BLOB, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                               WhisperTranslateBlobFunction));

	RegisterScalarFunction(
	    loader, std::move(translate_set),
	    MakeFunctionDescription({"audio", "model"},
	                            "Translates speech in any supported language from an audio file path (VARCHAR) or "
	                            "from audio bytes (BLOB) into English text. Requires a multilingual (non-.en) model.",
	                            {"whisper_translate('german_speech.mp3', 'small')"}, {"whisper", "transcription"}));
}

} // namespace duckdb
