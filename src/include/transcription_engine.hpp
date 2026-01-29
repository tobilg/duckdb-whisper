#pragma once

#include "duckdb.hpp"
#include "whisper_config.hpp"
#include <string>
#include <vector>

namespace duckdb {

struct TranscriptionSegment {
	int segment_id;
	double start_time; // in seconds
	double end_time;   // in seconds
	std::string text;
	double confidence; // 0.0-1.0
	std::string language;
};

struct TranscriptionResult {
	std::string full_text;
	std::vector<TranscriptionSegment> segments;
	std::string detected_language;
	bool success;
	std::string error;
};

class TranscriptionEngine {
public:
	// Transcribe audio file
	static TranscriptionResult TranscribeFile(const std::string &file_path, const WhisperConfig &config);

	// Transcribe audio from memory (BLOB)
	static TranscriptionResult TranscribeMemory(const uint8_t *data, size_t size, const WhisperConfig &config);

	// Transcribe from already-loaded PCM data
	static TranscriptionResult TranscribePCM(const std::vector<float> &pcm_data, const WhisperConfig &config);
};

} // namespace duckdb
