#ifdef WHISPER_ENABLE_RECORDING

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/printer.hpp"

#include "audio_recorder.hpp"
#include "transcription_engine.hpp"
#include "whisper_config.hpp"

#include <thread>
#include <chrono>
#include <cmath>

namespace duckdb {

// ============================================================================
// whisper_list_devices() - Lists available audio input devices
// ============================================================================

struct ListDevicesState : public GlobalTableFunctionState {
	std::vector<AudioDevice> devices;
	idx_t current_idx;

	ListDevicesState() : current_idx(0) {
		devices = AudioRecorder::ListDevices();
	}

	idx_t MaxThreads() const override {
		return 1;
	}
};

static unique_ptr<FunctionData> ListDevicesBind(ClientContext &context, TableFunctionBindInput &input,
                                                vector<LogicalType> &return_types, vector<string> &names) {
	return_types.push_back(LogicalType::INTEGER);
	names.push_back("device_id");

	return_types.push_back(LogicalType::VARCHAR);
	names.push_back("device_name");

	return nullptr;
}

static unique_ptr<GlobalTableFunctionState> ListDevicesInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<ListDevicesState>();
}

static void ListDevicesExecute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &state = data.global_state->Cast<ListDevicesState>();

	idx_t output_idx = 0;
	while (state.current_idx < state.devices.size() && output_idx < STANDARD_VECTOR_SIZE) {
		const auto &device = state.devices[state.current_idx];

		output.SetValue(0, output_idx, Value::INTEGER(device.id));
		output.SetValue(1, output_idx, Value(device.name));

		state.current_idx++;
		output_idx++;
	}

	output.SetCardinality(output_idx);
}

// ============================================================================
// whisper_record(duration_seconds, [model], [device_id]) - Records and transcribes
// ============================================================================

static void WhisperRecordFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);

	idx_t count = args.size();

	// Get duration from first argument
	auto &duration_vec = args.data[0];

	// Check for optional model parameter
	bool has_model = args.ColumnCount() > 1;

	// Check for optional device_id parameter
	bool has_device = args.ColumnCount() > 2;

	UnaryExecutor::Execute<int32_t, string_t>(duration_vec, result, count, [&](int32_t duration_seconds) {
		WhisperConfig local_config = config;

		if (has_model) {
			auto &model_vec = args.data[1];
			if (model_vec.GetType().id() == LogicalTypeId::VARCHAR) {
				auto model_val = FlatVector::GetData<string_t>(model_vec)[0];
				local_config.model = model_val.GetString();
			}
		}

		// Use configured device_id as default, override if parameter provided
		int device_id = local_config.device_id;
		if (has_device) {
			auto &device_vec = args.data[2];
			if (device_vec.GetType().id() == LogicalTypeId::INTEGER) {
				device_id = FlatVector::GetData<int32_t>(device_vec)[0];
			}
		}

		// Start recording
		AudioRecorder recorder;
		std::string error;

		if (!recorder.StartRecording(device_id, error)) {
			throw InvalidInputException("Failed to start recording: " + error);
		}

		if (local_config.verbose) {
			Printer::Print(OutputStream::STREAM_STDERR, "Listening...");
		}

		// Record for specified duration
		std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));

		// Stop and get audio data
		std::vector<float> pcm_data;
		if (!recorder.StopRecording(pcm_data, error)) {
			throw InvalidInputException("Failed to stop recording: " + error);
		}

		if (local_config.verbose) {
			Printer::Print(OutputStream::STREAM_STDERR, "Stopped");
		}

		// Transcribe
		TranscriptionResult transcription = TranscriptionEngine::TranscribePCM(pcm_data, local_config);

		if (!transcription.success) {
			throw InvalidInputException("Transcription failed: " + transcription.error);
		}

		return StringVector::AddString(result, transcription.full_text);
	});
}

// ============================================================================
// whisper_record_translate(duration_seconds, [model], [device_id]) - Records and translates to English
// ============================================================================

static void WhisperRecordTranslateFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);
	config.translate = true;

	idx_t count = args.size();

	auto &duration_vec = args.data[0];
	bool has_model = args.ColumnCount() > 1;
	bool has_device = args.ColumnCount() > 2;

	UnaryExecutor::Execute<int32_t, string_t>(duration_vec, result, count, [&](int32_t duration_seconds) {
		WhisperConfig local_config = config;

		if (has_model) {
			auto &model_vec = args.data[1];
			if (model_vec.GetType().id() == LogicalTypeId::VARCHAR) {
				auto model_val = FlatVector::GetData<string_t>(model_vec)[0];
				local_config.model = model_val.GetString();
			}
		}

		// Use configured device_id as default, override if parameter provided
		int device_id = local_config.device_id;
		if (has_device) {
			auto &device_vec = args.data[2];
			if (device_vec.GetType().id() == LogicalTypeId::INTEGER) {
				device_id = FlatVector::GetData<int32_t>(device_vec)[0];
			}
		}

		AudioRecorder recorder;
		std::string error;

		if (!recorder.StartRecording(device_id, error)) {
			throw InvalidInputException("Failed to start recording: " + error);
		}

		if (local_config.verbose) {
			Printer::Print(OutputStream::STREAM_STDERR, "Listening...");
		}

		std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));

		std::vector<float> pcm_data;
		if (!recorder.StopRecording(pcm_data, error)) {
			throw InvalidInputException("Failed to stop recording: " + error);
		}

		if (local_config.verbose) {
			Printer::Print(OutputStream::STREAM_STDERR, "Stopped");
		}

		TranscriptionResult transcription = TranscriptionEngine::TranscribePCM(pcm_data, local_config);

		if (!transcription.success) {
			throw InvalidInputException("Translation failed: " + transcription.error);
		}

		return StringVector::AddString(result, transcription.full_text);
	});
}

// ============================================================================
// whisper_mic_level(duration_seconds, [device_id]) - Check microphone amplitude levels
// ============================================================================

static void WhisperMicLevelFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);

	idx_t count = args.size();
	auto &duration_vec = args.data[0];
	bool has_device = args.ColumnCount() > 1;

	for (idx_t i = 0; i < count; i++) {
		int32_t duration_seconds = FlatVector::GetData<int32_t>(duration_vec)[i];

		// Use configured device_id as default, override if parameter provided
		int device_id = config.device_id;
		if (has_device) {
			device_id = FlatVector::GetData<int32_t>(args.data[1])[i];
		}

		AudioRecorder recorder;
		std::string error;

		if (!recorder.StartRecording(device_id, error)) {
			throw InvalidInputException("Failed to start recording: " + error);
		}

		float max_amplitude = 0.0f;
		float sum_amplitude = 0.0f;
		int samples = 0;

		auto start = std::chrono::steady_clock::now();
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			auto now = std::chrono::steady_clock::now();
			double elapsed = std::chrono::duration<double>(now - start).count();

			if (elapsed >= duration_seconds)
				break;

			// This is a simplified check - we'd need to expose amplitude from recorder
			// For now, we'll record and analyze after
			samples++;
		}

		std::vector<float> pcm_data;
		recorder.StopRecording(pcm_data, error);

		// Calculate RMS and peak amplitude from recorded data
		if (!pcm_data.empty()) {
			float sum_squares = 0.0f;
			for (float sample : pcm_data) {
				float abs_sample = std::abs(sample);
				if (abs_sample > max_amplitude) {
					max_amplitude = abs_sample;
				}
				sum_squares += sample * sample;
			}
			float rms = std::sqrt(sum_squares / pcm_data.size());

			std::string result_str = "Peak: " + std::to_string(max_amplitude) + ", RMS: " + std::to_string(rms) +
			                         " (suggested threshold: " + std::to_string(rms * 0.5f) + ")";
			FlatVector::GetData<string_t>(result)[i] = StringVector::AddString(result, result_str);
		} else {
			FlatVector::GetData<string_t>(result)[i] = StringVector::AddString(result, "No audio captured");
		}
	}
}

// ============================================================================
// whisper_record_auto(max_seconds, silence_seconds, [model], [threshold], [device_id])
// Records until silence is detected or max duration reached
// ============================================================================

static void WhisperRecordAutoFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);

	idx_t count = args.size();
	idx_t col_count = args.ColumnCount();

	auto &max_duration_vec = args.data[0];

	// Determine which parameters are present based on column count and types
	// Signatures:
	// 1: (max_seconds)
	// 2: (max_seconds, silence_seconds)
	// 3: (max_seconds, silence_seconds, model)
	// 4: (max_seconds, silence_seconds, model, threshold)
	// 5: (max_seconds, silence_seconds, model, threshold, device_id)

	bool has_silence = col_count > 1;
	bool has_model = col_count > 2;
	bool has_threshold = col_count > 3;
	bool has_device = col_count > 4;

	for (idx_t i = 0; i < count; i++) {
		int32_t max_duration = FlatVector::GetData<int32_t>(max_duration_vec)[i];

		WhisperConfig local_config = config;

		// Use configured silence_duration as default, override if parameter provided
		double silence_duration = local_config.silence_duration;
		if (has_silence) {
			silence_duration = FlatVector::GetData<double>(args.data[1])[i];
		}

		if (has_model) {
			idx_t model_idx = has_silence ? 2 : 1;
			auto &model_vec = args.data[model_idx];
			if (model_vec.GetType().id() == LogicalTypeId::VARCHAR && !FlatVector::IsNull(model_vec, i)) {
				auto model_val = FlatVector::GetData<string_t>(model_vec)[i];
				local_config.model = model_val.GetString();
			}
		}

		// Use configured silence_threshold as default, override if parameter provided
		float threshold = static_cast<float>(local_config.silence_threshold);
		if (has_threshold) {
			idx_t threshold_idx = has_silence ? 3 : 2;
			auto &threshold_vec = args.data[threshold_idx];
			if (!FlatVector::IsNull(threshold_vec, i)) {
				threshold = static_cast<float>(FlatVector::GetData<double>(threshold_vec)[i]);
			}
		}

		// Use configured device_id as default, override if parameter provided
		int device_id = local_config.device_id;
		if (has_device) {
			idx_t device_idx = has_silence ? 4 : 3;
			auto &device_vec = args.data[device_idx];
			if (!FlatVector::IsNull(device_vec, i)) {
				device_id = FlatVector::GetData<int32_t>(device_vec)[i];
			}
		}

		AudioRecorder recorder;
		std::string error;
		std::vector<float> pcm_data;

		if (local_config.verbose) {
			Printer::Print(OutputStream::STREAM_STDERR, "Listening...");
		}

		if (!recorder.RecordUntilSilence(pcm_data, max_duration, silence_duration, threshold, device_id, error)) {
			throw InvalidInputException("Failed to record: " + error);
		}

		if (local_config.verbose) {
			Printer::Print(OutputStream::STREAM_STDERR, "Stopped");
		}

		if (pcm_data.empty()) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		TranscriptionResult transcription = TranscriptionEngine::TranscribePCM(pcm_data, local_config);

		if (!transcription.success) {
			throw InvalidInputException("Transcription failed: " + transcription.error);
		}

		FlatVector::GetData<string_t>(result)[i] = StringVector::AddString(result, transcription.full_text);
	}
}

// ============================================================================
// Registration
// ============================================================================

void RegisterRecordFunctions(ExtensionLoader &loader) {
	// whisper_list_devices() -> TABLE(device_id INTEGER, device_name VARCHAR)
	TableFunction list_devices("whisper_list_devices", {}, ListDevicesExecute, ListDevicesBind, ListDevicesInit);
	loader.RegisterFunction(list_devices);

	// whisper_record(duration_seconds INTEGER, [model VARCHAR], [device_id INTEGER]) -> VARCHAR
	ScalarFunctionSet record_set("whisper_record");

	record_set.AddFunction(ScalarFunction({LogicalType::INTEGER}, LogicalType::VARCHAR, WhisperRecordFunction));

	record_set.AddFunction(
	    ScalarFunction({LogicalType::INTEGER, LogicalType::VARCHAR}, LogicalType::VARCHAR, WhisperRecordFunction));

	record_set.AddFunction(ScalarFunction({LogicalType::INTEGER, LogicalType::VARCHAR, LogicalType::INTEGER},
	                                      LogicalType::VARCHAR, WhisperRecordFunction));

	loader.RegisterFunction(record_set);

	// whisper_record_translate(duration_seconds INTEGER, [model VARCHAR], [device_id INTEGER]) -> VARCHAR
	ScalarFunctionSet record_translate_set("whisper_record_translate");

	record_translate_set.AddFunction(
	    ScalarFunction({LogicalType::INTEGER}, LogicalType::VARCHAR, WhisperRecordTranslateFunction));

	record_translate_set.AddFunction(ScalarFunction({LogicalType::INTEGER, LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                                WhisperRecordTranslateFunction));

	record_translate_set.AddFunction(ScalarFunction({LogicalType::INTEGER, LogicalType::VARCHAR, LogicalType::INTEGER},
	                                                LogicalType::VARCHAR, WhisperRecordTranslateFunction));

	loader.RegisterFunction(record_translate_set);

	// whisper_record_auto(max_seconds INTEGER, [silence_seconds DOUBLE], [model VARCHAR], [threshold DOUBLE],
	// [device_id INTEGER]) -> VARCHAR Default silence_seconds = 2.0
	ScalarFunctionSet record_auto_set("whisper_record_auto");

	// Just max_seconds (uses 2s default silence)
	record_auto_set.AddFunction(
	    ScalarFunction({LogicalType::INTEGER}, LogicalType::VARCHAR, WhisperRecordAutoFunction));

	// max_seconds, silence_seconds
	record_auto_set.AddFunction(
	    ScalarFunction({LogicalType::INTEGER, LogicalType::DOUBLE}, LogicalType::VARCHAR, WhisperRecordAutoFunction));

	// With model
	record_auto_set.AddFunction(ScalarFunction({LogicalType::INTEGER, LogicalType::DOUBLE, LogicalType::VARCHAR},
	                                           LogicalType::VARCHAR, WhisperRecordAutoFunction));

	// With model and threshold
	record_auto_set.AddFunction(
	    ScalarFunction({LogicalType::INTEGER, LogicalType::DOUBLE, LogicalType::VARCHAR, LogicalType::DOUBLE},
	                   LogicalType::VARCHAR, WhisperRecordAutoFunction));

	// With model, threshold, and device_id
	record_auto_set.AddFunction(ScalarFunction(
	    {LogicalType::INTEGER, LogicalType::DOUBLE, LogicalType::VARCHAR, LogicalType::DOUBLE, LogicalType::INTEGER},
	    LogicalType::VARCHAR, WhisperRecordAutoFunction));

	loader.RegisterFunction(record_auto_set);

	// whisper_mic_level(duration_seconds INTEGER, [device_id INTEGER]) -> VARCHAR
	// Utility to check microphone amplitude levels
	ScalarFunctionSet mic_level_set("whisper_mic_level");

	mic_level_set.AddFunction(ScalarFunction({LogicalType::INTEGER}, LogicalType::VARCHAR, WhisperMicLevelFunction));

	mic_level_set.AddFunction(
	    ScalarFunction({LogicalType::INTEGER, LogicalType::INTEGER}, LogicalType::VARCHAR, WhisperMicLevelFunction));

	loader.RegisterFunction(mic_level_set);
}

} // namespace duckdb

#endif // WHISPER_ENABLE_RECORDING
