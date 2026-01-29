#pragma once

#ifdef WHISPER_ENABLE_RECORDING

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

namespace duckdb {

struct AudioDevice {
	int id;
	std::string name;
	bool is_capture; // true for input devices
};

class AudioRecorder {
public:
	AudioRecorder();
	~AudioRecorder();

	// List available audio input devices
	static std::vector<AudioDevice> ListDevices();

	// Start recording from specified device (or default if device_id < 0)
	bool StartRecording(int device_id = -1, std::string &error = *(new std::string()));

	// Stop recording and return PCM data (16kHz mono float32)
	bool StopRecording(std::vector<float> &pcm_data, std::string &error);

	// Record with automatic stop on silence
	// max_duration_sec: maximum recording time
	// silence_threshold: amplitude threshold (0.0-1.0), default 0.01
	// silence_duration_sec: stop after this many seconds of silence
	bool RecordUntilSilence(std::vector<float> &pcm_data, double max_duration_sec, double silence_duration_sec,
	                        float silence_threshold, int device_id, std::string &error);

	// Check if currently recording
	bool IsRecording() const;

	// Get current recording duration in seconds
	double GetRecordingDuration() const;

private:
	static void AudioCallback(void *userdata, unsigned char *stream, int len);

	uint32_t device_id_; // SDL_AudioDeviceID
	std::vector<float> buffer_;
	std::atomic<bool> recording_;
	int sample_rate_;

	// Silence detection
	std::atomic<float> current_amplitude_;
	float silence_threshold_;
	double silence_duration_sec_;
	double silence_start_time_;
	std::atomic<bool> silence_detected_;

	static bool sdl_initialized_;
};

} // namespace duckdb

#endif // WHISPER_ENABLE_RECORDING
