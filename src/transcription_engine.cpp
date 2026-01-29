#include "transcription_engine.hpp"
#include "audio_utils.hpp"
#include "model_manager.hpp"
#include "whisper_context.hpp"
#include "whisper.h"

#include <cstring>
#include <thread>

namespace duckdb {

// Convert whisper language ID to language code
static std::string GetLanguageCode(int lang_id) {
	const char *lang = whisper_lang_str(lang_id);
	return lang ? std::string(lang) : "unknown";
}

// Calculate confidence from token probabilities
static double CalculateSegmentConfidence(whisper_context *ctx, int segment_idx) {
	int n_tokens = whisper_full_n_tokens(ctx, segment_idx);
	if (n_tokens == 0)
		return 0.0;

	double sum_prob = 0.0;
	int count = 0;

	for (int i = 0; i < n_tokens; i++) {
		whisper_token_data token = whisper_full_get_token_data(ctx, segment_idx, i);
		// Skip special tokens
		if (token.id < whisper_token_eot(ctx)) {
			sum_prob += token.p;
			count++;
		}
	}

	return count > 0 ? sum_prob / count : 0.0;
}

TranscriptionResult TranscriptionEngine::TranscribePCM(const std::vector<float> &pcm_data,
                                                       const WhisperConfig &config) {
	TranscriptionResult result;
	result.success = false;

	if (pcm_data.empty()) {
		result.error = "Empty audio data";
		return result;
	}

	// Get model path
	std::string model_path = ModelManager::GetModelPath(config.model, config.model_path);

	// Get or create whisper context
	std::string ctx_error;
	auto ctx_wrapper = WhisperContextManager::GetInstance().GetContext(model_path, ctx_error);
	if (!ctx_wrapper || !ctx_wrapper->IsValid()) {
		result.error = ctx_error.empty() ? "Failed to load model" : ctx_error;
		return result;
	}

	whisper_context *ctx = ctx_wrapper->Get();

	// Check if translation is requested but model doesn't support it
	if (config.translate && !whisper_is_multilingual(ctx)) {
		result.error =
		    "Translation requires a multilingual model. English-only models (.en) do not support translation. "
		    "Please use a multilingual model like 'tiny', 'base', 'small', 'medium', or 'large-v3'.";
		return result;
	}

	// Configure whisper parameters
	whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

	// Set language
	if (config.language != "auto") {
		wparams.language = config.language.c_str();
	} else {
		wparams.language = nullptr; // Auto-detect
	}

	// Set thread count
	if (config.threads > 0) {
		wparams.n_threads = config.threads;
	} else {
		wparams.n_threads = std::min(8, static_cast<int>(std::thread::hardware_concurrency()));
	}

	// Other settings
	wparams.print_progress = false;
	wparams.print_special = false;
	wparams.print_realtime = false;
	wparams.print_timestamps = config.timestamps;
	wparams.translate = config.translate;
	wparams.single_segment = false;
	wparams.max_len = config.max_segment_length / 10; // max_len is in tokens, rough approximation

	// Run transcription
	int ret = whisper_full(ctx, wparams, pcm_data.data(), pcm_data.size());
	if (ret != 0) {
		result.error = "Transcription failed with error code: " + std::to_string(ret);
		return result;
	}

	// Extract results
	int n_segments = whisper_full_n_segments(ctx);
	result.segments.reserve(n_segments);

	std::string full_text;

	for (int i = 0; i < n_segments; i++) {
		TranscriptionSegment segment;
		segment.segment_id = i;
		segment.start_time = static_cast<double>(whisper_full_get_segment_t0(ctx, i)) / 100.0;
		segment.end_time = static_cast<double>(whisper_full_get_segment_t1(ctx, i)) / 100.0;

		const char *text = whisper_full_get_segment_text(ctx, i);
		segment.text = text ? text : "";

		segment.confidence = CalculateSegmentConfidence(ctx, i);

		// Get language for this segment
		int lang_id = whisper_full_lang_id(ctx);
		segment.language = GetLanguageCode(lang_id);

		result.segments.push_back(segment);

		// Build full text
		if (!full_text.empty() && !segment.text.empty()) {
			full_text += " ";
		}
		full_text += segment.text;
	}

	result.full_text = full_text;
	result.detected_language = !result.segments.empty() ? result.segments[0].language : "unknown";
	result.success = true;

	return result;
}

TranscriptionResult TranscriptionEngine::TranscribeFile(const std::string &file_path, const WhisperConfig &config) {
	TranscriptionResult result;
	result.success = false;

	// Load and convert audio
	std::vector<float> pcm_data;
	std::string load_error;

	if (!AudioUtils::LoadAudioFile(file_path, pcm_data, load_error)) {
		result.error = "Failed to load audio: " + load_error;
		return result;
	}

	return TranscribePCM(pcm_data, config);
}

TranscriptionResult TranscriptionEngine::TranscribeMemory(const uint8_t *data, size_t size,
                                                          const WhisperConfig &config) {
	TranscriptionResult result;
	result.success = false;

	// Load and convert audio from memory
	std::vector<float> pcm_data;
	std::string load_error;

	if (!AudioUtils::LoadAudioFromMemory(data, size, pcm_data, load_error)) {
		result.error = "Failed to load audio from memory: " + load_error;
		return result;
	}

	return TranscribePCM(pcm_data, config);
}

} // namespace duckdb
