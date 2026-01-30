# Changelog

All notable changes to the DuckDB Whisper Extension will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2025-01-28

### Added

- Initial release of the DuckDB Whisper Extension
- Core transcription functions:
  - `whisper_transcribe()` - Transcribe audio to text (scalar function)
  - `whisper_transcribe_segments()` - Transcribe with timestamps and metadata (table function)
- Model management functions:
  - `whisper_list_models()` - List all available models with download status
  - `whisper_download_model()` - Get download instructions for a model
- Utility functions:
  - `whisper_version()` - Get extension and whisper.cpp version info
  - `whisper_check_audio()` - Validate audio files
  - `whisper_audio_info()` - Get audio file metadata
- Configuration functions (session-persistent):
  - `whisper_set_model()` - Set default model
  - `whisper_set_model_path()` - Set model storage directory
  - `whisper_set_language()` - Set default language
  - `whisper_set_threads()` - Set thread count
  - `whisper_get_config()` - View current configuration
- Recording functions (requires SDL2, enabled by default):
  - `whisper_list_devices()` - List audio input devices
  - `whisper_record()` - Record from microphone and transcribe
  - `whisper_record_translate()` - Record and translate to English
  - `whisper_record_auto()` - Record with automatic silence detection (configurable)
- Support for all Whisper models (tiny through large-v3-turbo)
- Support for multiple audio formats via FFmpeg (WAV, MP3, FLAC, OGG, AAC, etc.)
- BLOB input support for transcribing audio from memory
- Automatic audio conversion to 16kHz mono (Whisper requirement)
- Model caching for improved performance on repeated transcriptions
- SQL test suite with 34 test assertions
- Comprehensive documentation
- Compile-time option `WHISPER_ENABLE_RECORDING` (ON by default)

### Technical Details

- Built on [whisper.cpp](https://github.com/ggml-org/whisper.cpp) v1.8.3
- Compatible with DuckDB v1.4.x
- Uses FFmpeg for audio decoding
- Uses SDL2 for audio recording (optional)
- Supports macOS (ARM64, x86_64) and Linux
