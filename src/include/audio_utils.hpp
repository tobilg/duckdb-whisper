#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace duckdb {

struct AudioMetadata {
	double duration_seconds;
	int sample_rate;
	int channels;
	std::string format;
	int64_t file_size;
};

class AudioUtils {
public:
	// Load audio from file and convert to 16kHz mono float32 PCM (whisper requirement)
	static bool LoadAudioFile(const std::string &file_path, std::vector<float> &output, std::string &error);

	// Load audio from memory buffer and convert to 16kHz mono float32 PCM
	static bool LoadAudioFromMemory(const uint8_t *data, size_t size, std::vector<float> &output, std::string &error);

	// Get audio metadata without fully decoding
	static bool GetAudioMetadata(const std::string &file_path, AudioMetadata &metadata, std::string &error);

	// Check if audio file is valid and supported
	static bool CheckAudioFile(const std::string &file_path, std::string &error);

	// Configure FFmpeg logging (true = AV_LOG_INFO, false = AV_LOG_QUIET)
	static void SetFFmpegLogging(bool enabled);

private:
	static constexpr int WHISPER_SAMPLE_RATE = 16000;
};

} // namespace duckdb
