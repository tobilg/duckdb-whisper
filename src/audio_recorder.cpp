#ifdef WHISPER_ENABLE_RECORDING

#include "audio_recorder.hpp"
#include <SDL.h>
#include <cstring>
#include <cmath>
#include <mutex>
#include <chrono>
#include <thread>

namespace duckdb {

bool AudioRecorder::sdl_initialized_ = false;
static std::mutex sdl_mutex_;

AudioRecorder::AudioRecorder()
    : device_id_(0), recording_(false), sample_rate_(16000), current_amplitude_(0.0f), silence_threshold_(0.01f),
      silence_duration_sec_(0.0), silence_start_time_(0.0), silence_detected_(false) {
}

AudioRecorder::~AudioRecorder() {
	if (device_id_ != 0) {
		SDL_CloseAudioDevice(device_id_);
		device_id_ = 0;
	}
}

std::vector<AudioDevice> AudioRecorder::ListDevices() {
	std::lock_guard<std::mutex> lock(sdl_mutex_);

	if (!sdl_initialized_) {
		if (SDL_Init(SDL_INIT_AUDIO) < 0) {
			return {};
		}
		sdl_initialized_ = true;
	}

	std::vector<AudioDevice> devices;

	// Get capture (input) devices
	int count = SDL_GetNumAudioDevices(1); // 1 = capture devices
	for (int i = 0; i < count; i++) {
		const char *name = SDL_GetAudioDeviceName(i, 1);
		if (name) {
			AudioDevice dev;
			dev.id = i;
			dev.name = name;
			dev.is_capture = true;
			devices.push_back(dev);
		}
	}

	return devices;
}

void AudioRecorder::AudioCallback(void *userdata, unsigned char *stream, int len) {
	AudioRecorder *recorder = static_cast<AudioRecorder *>(userdata);
	if (!recorder || !recorder->recording_) {
		return;
	}

	// Convert from int16 to float and append to buffer
	int16_t *samples = reinterpret_cast<int16_t *>(stream);
	int num_samples = len / sizeof(int16_t);

	// Calculate RMS amplitude for this chunk
	float sum_squares = 0.0f;
	for (int i = 0; i < num_samples; i++) {
		float sample = static_cast<float>(samples[i]) / 32768.0f;
		recorder->buffer_.push_back(sample);
		sum_squares += sample * sample;
	}

	if (num_samples > 0) {
		float rms = std::sqrt(sum_squares / num_samples);
		recorder->current_amplitude_.store(rms);
	}
}

bool AudioRecorder::StartRecording(int device_id, std::string &error) {
	std::lock_guard<std::mutex> lock(sdl_mutex_);

	if (!sdl_initialized_) {
		if (SDL_Init(SDL_INIT_AUDIO) < 0) {
			error = std::string("Failed to initialize SDL: ") + SDL_GetError();
			return false;
		}
		sdl_initialized_ = true;
	}

	if (recording_) {
		error = "Already recording";
		return false;
	}

	// Configure audio spec for 16kHz mono (Whisper requirement)
	SDL_AudioSpec desired, obtained;
	SDL_zero(desired);
	desired.freq = 16000;
	desired.format = AUDIO_S16SYS;
	desired.channels = 1;
	desired.samples = 1024;
	desired.callback = AudioCallback;
	desired.userdata = this;

	const char *device_name = nullptr;
	if (device_id >= 0) {
		device_name = SDL_GetAudioDeviceName(device_id, 1);
	}

	SDL_AudioDeviceID dev = SDL_OpenAudioDevice(device_name,
	                                            1, // capture
	                                            &desired, &obtained, 0);

	if (dev == 0) {
		error = std::string("Failed to open audio device: ") + SDL_GetError();
		return false;
	}

	device_id_ = dev;
	sample_rate_ = obtained.freq;
	buffer_.clear();
	recording_ = true;

	// Start capture
	SDL_PauseAudioDevice(dev, 0);

	return true;
}

bool AudioRecorder::StopRecording(std::vector<float> &pcm_data, std::string &error) {
	if (!recording_) {
		error = "Not recording";
		return false;
	}

	recording_ = false;

	if (device_id_ != 0) {
		SDL_PauseAudioDevice(device_id_, 1);
		SDL_CloseAudioDevice(device_id_);
		device_id_ = 0;
	}

	if (buffer_.empty()) {
		error = "No audio data recorded";
		return false;
	}

	// If sample rate differs from 16kHz, we'd need to resample
	// For now, we request 16kHz directly from SDL
	pcm_data = std::move(buffer_);
	buffer_.clear();

	return true;
}

bool AudioRecorder::IsRecording() const {
	return recording_;
}

double AudioRecorder::GetRecordingDuration() const {
	if (sample_rate_ <= 0)
		return 0.0;
	return static_cast<double>(buffer_.size()) / sample_rate_;
}

bool AudioRecorder::RecordUntilSilence(std::vector<float> &pcm_data, double max_duration_sec,
                                       double silence_duration_sec, float silence_threshold, int device_id,
                                       std::string &error) {
	// Start recording
	if (!StartRecording(device_id, error)) {
		return false;
	}

	silence_threshold_ = silence_threshold;
	silence_duration_sec_ = silence_duration_sec;
	silence_detected_ = false;

	auto start_time = std::chrono::steady_clock::now();
	double silence_start = -1.0;
	bool had_sound = false; // Track if we ever detected sound

	while (recording_) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		auto now = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now - start_time).count();

		// Check max duration
		if (elapsed >= max_duration_sec) {
			break;
		}

		// Check amplitude for silence detection
		float amplitude = current_amplitude_.load();

		if (amplitude > silence_threshold) {
			// Sound detected
			had_sound = true;
			silence_start = -1.0; // Reset silence timer
		} else if (had_sound) {
			// Silence after sound was detected
			if (silence_start < 0) {
				silence_start = elapsed;
			} else if (elapsed - silence_start >= silence_duration_sec) {
				// Silence duration exceeded
				silence_detected_ = true;
				break;
			}
		}
	}

	return StopRecording(pcm_data, error);
}

} // namespace duckdb

#endif // WHISPER_ENABLE_RECORDING
