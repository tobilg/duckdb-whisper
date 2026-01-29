# DuckDB Whisper Extension

A DuckDB extension for speech-to-text transcription using [whisper.cpp](https://github.com/ggml-org/whisper.cpp), the C/C++ port of OpenAI's Whisper model.

Transcribe audio files directly from SQL queries in DuckDB, making it easy to process and analyze audio data alongside your other data.

## Features

- Transcribe audio files (WAV, MP3, FLAC, OGG, and more)
- Live recording and transcription from microphone
- Voice-to-SQL: speak natural language questions, get query results
- Support for all Whisper models (tiny, base, small, medium, large)
- Detailed transcription segments with timestamps and confidence scores
- Automatic language detection or specify target language
- Works with file paths, BLOB data, or remote URLs

## Installation

### Community Extension (Recommended)

```sql
INSTALL whisper FROM community;
LOAD whisper;
```

### Download a Model

Models are downloaded automatically to `~/.duckdb/whisper/models/`. You can also download manually:

```bash
mkdir -p ~/.duckdb/whisper/models

# Download tiny.en model (~75MB, fastest)
curl -L -o ~/.duckdb/whisper/models/ggml-tiny.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin
```

Check available models and download status:

```sql
SELECT * FROM whisper_list_models();
```

## Quick Start

### Transcribe an Audio File

```sql
-- Simple transcription
SELECT whisper_transcribe('audio.wav', 'tiny.en');

-- Get detailed segments with timestamps
SELECT * FROM whisper_transcribe_segments('audio.wav', 'tiny.en');
```

### Setup Audio Device (Required for Recording)

Before using microphone recording or voice query features:

```sql
-- List all available audio input devices
SELECT * FROM whisper_list_devices();

-- Set the device ID (use a device_id from the list above)
SET whisper_device_id = 0;

-- Verify your microphone is working
SELECT whisper_mic_level(3);
```

### Record and Transcribe

```sql
-- Record for 5 seconds
SELECT whisper_record(5, 'tiny.en');

-- Record until silence (max 30 seconds)
SELECT whisper_record_auto(30);
```

### Voice-to-SQL (Experimental)

Speak natural language questions and get SQL results:

```sql
-- Create test data
CREATE TABLE customers (id INT, name VARCHAR, revenue DECIMAL);
INSERT INTO customers VALUES (1, 'Acme', 100000), (2, 'Beta', 50000);

-- Speak your question (e.g., "show all customers")
FROM whisper_voice_query();
```

Requires [text-to-sql-proxy](https://github.com/tobilg/text-to-sql-proxy) running locally. See [Voice-to-SQL Feature](#voice-to-sql-feature) for details.

## Example Use Cases

### Transcribe Remote Audio

```sql
INSTALL httpfs;
LOAD httpfs;

SELECT whisper_transcribe(content, 'tiny.en')
FROM read_blob('https://example.com/audio.mp3');
```

### Batch Transcribe Multiple Files

```sql
SELECT file, whisper_transcribe(file, 'tiny.en') as transcript
FROM glob('audio/*.wav');
```

### Search Within Transcriptions

```sql
SELECT * FROM whisper_transcribe_segments('meeting.wav', 'base.en')
WHERE text ILIKE '%action item%';
```

### Translate Foreign Audio to English

```sql
SELECT whisper_translate('german_speech.mp3', 'small');
```

### Generate Subtitles (SRT Format)

```sql
SELECT
    segment_id + 1 as id,
    printf('%02d:%02d:%02d,%03d',
        (start_time/3600)::int, ((start_time%3600)/60)::int,
        (start_time%60)::int, ((start_time - start_time::int) * 1000)::int
    ) || ' --> ' ||
    printf('%02d:%02d:%02d,%03d',
        (end_time/3600)::int, ((end_time%3600)/60)::int,
        (end_time%60)::int, ((end_time - end_time::int) * 1000)::int
    ) as timestamp,
    trim(text) as text
FROM whisper_transcribe_segments('video.mp4', 'small.en');
```

## Available Models

| Model | Size | Description |
|-------|------|-------------|
| `tiny` | ~75MB | Fastest, multilingual |
| `tiny.en` | ~75MB | Fastest, English-only |
| `base` | ~142MB | Fast, multilingual |
| `base.en` | ~142MB | Fast, English-only |
| `small` | ~466MB | Good balance, multilingual |
| `small.en` | ~466MB | Good balance, English-only |
| `medium` | ~1.5GB | High quality, multilingual |
| `medium.en` | ~1.5GB | High quality, English-only |
| `large-v1` | ~2.9GB | Best quality, multilingual |
| `large-v2` | ~2.9GB | Best quality, multilingual |
| `large-v3` | ~2.9GB | Best quality, multilingual |
| `large-v3-turbo` | ~1.6GB | Fast + accurate, multilingual |

**Tip:** English-only models (`.en` suffix) are optimized for English and perform better for English audio.

## Supported Audio Formats

The extension uses FFmpeg for audio decoding:

- WAV, MP3, FLAC, OGG/Vorbis, AAC/M4A, and many more

Audio is automatically converted to 16kHz mono as required by Whisper.

## Function Reference

### Transcription Functions

#### `whisper_transcribe(audio, [model])`

Transcribes audio and returns the full text.

```sql
SELECT whisper_transcribe('audio.wav', 'tiny.en');
SELECT whisper_transcribe(audio_blob, 'base.en') FROM audio_table;
```

#### `whisper_translate(audio, [model])`

Translates audio from any language to English.

```sql
SELECT whisper_translate('german_speech.mp3', 'small');
```

#### `whisper_transcribe_segments(audio, [model], [language], [translate])`

Returns a table of transcription segments with timestamps.

| Column | Type | Description |
|--------|------|-------------|
| segment_id | INTEGER | 0-based segment index |
| start_time | DOUBLE | Start time in seconds |
| end_time | DOUBLE | End time in seconds |
| text | VARCHAR | Transcribed text |
| confidence | DOUBLE | Confidence score (0.0-1.0) |
| language | VARCHAR | Detected language code |

### Recording Functions

#### `whisper_list_devices()`

Lists available audio input devices.

```sql
SELECT * FROM whisper_list_devices();
```

#### `whisper_record(duration_seconds, [model], [device_id])`

Records audio from microphone and transcribes it.

```sql
SELECT whisper_record(5, 'tiny.en');
```

#### `whisper_record_auto(max_seconds, [silence_seconds], [model], [threshold], [device_id])`

Records until silence is detected or max duration reached.

```sql
SELECT whisper_record_auto(30, 2.0, 'tiny.en');
```

#### `whisper_record_translate(duration_seconds, [model], [device_id])`

Records audio and translates to English.

```sql
SELECT whisper_record_translate(5, 'small');
```

#### `whisper_mic_level(duration_seconds, [device_id])`

Check microphone amplitude levels. Use to determine appropriate silence threshold.

```sql
SELECT whisper_mic_level(3);
-- Returns: "Peak: 0.15, RMS: 0.02 (suggested threshold: 0.01)"
```

### Model Management Functions

#### `whisper_list_models()`

Lists all available Whisper models and their download status.

```sql
SELECT * FROM whisper_list_models();
SELECT * FROM whisper_list_models() WHERE is_downloaded = true;
```

#### `whisper_download_model(model_name)`

Returns download instructions for a model.

#### `whisper_delete_model(model_name)`

Deletes a locally stored model file.

### Utility Functions

#### `whisper_version()`

Returns extension and whisper.cpp version info.

#### `whisper_check_audio(file_path)`

Validates that an audio file can be read.

#### `whisper_audio_info(file_path)`

Returns metadata about an audio file (duration, sample rate, channels, format).

### Configuration

Configure settings using standard `SET` statements:

```sql
-- Model settings
SET whisper_model = 'small.en';
SET whisper_model_path = '/custom/path/models';
SET whisper_language = 'en';
SET whisper_threads = 4;

-- Recording settings
SET whisper_device_id = 0;
SET whisper_max_duration = 30;
SET whisper_silence_duration = 2;
SET whisper_silence_threshold = 0.005;

-- Voice query settings (if enabled)
SET whisper_text_to_sql_url = 'http://localhost:8080/generate-sql';
SET whisper_text_to_sql_timeout = 60;
SET whisper_voice_query_show_sql = true;

-- View current value
SELECT current_setting('whisper_device_id');

-- View all whisper settings
SELECT * FROM duckdb_settings() WHERE name LIKE 'whisper_%';

-- View all settings with whisper_get_config()
SELECT whisper_get_config();

-- Reset a setting to default
RESET whisper_device_id;
```

#### Available Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `whisper_model` | VARCHAR | "base.en" | Whisper model name |
| `whisper_model_path` | VARCHAR | ~/.duckdb/whisper/models | Model storage path |
| `whisper_language` | VARCHAR | "auto" | Target language code |
| `whisper_threads` | INTEGER | 0 | Processing threads (0=auto) |
| `whisper_device_id` | INTEGER | -1 | Audio device ID (-1=default) |
| `whisper_max_duration` | DOUBLE | 15.0 | Max recording duration (seconds) |
| `whisper_silence_duration` | DOUBLE | 1.0 | Silence to stop recording (seconds) |
| `whisper_silence_threshold` | DOUBLE | 0.001 | Silence detection threshold |
| `whisper_verbose` | BOOLEAN | false | Show status messages during operations |
| `whisper_text_to_sql_url` | VARCHAR | "http://localhost:4000/generate-sql" | Text-to-SQL proxy URL |
| `whisper_text_to_sql_timeout` | INTEGER | 15 | Proxy request timeout (seconds) |
| `whisper_voice_query_show_sql` | BOOLEAN | false | Show generated SQL in output |
| `whisper_voice_query_timeout` | INTEGER | 30 | Timeout for entire voice query operation (seconds) |

## Voice-to-SQL Feature

Speak natural language questions about your data and receive SQL query results.

### Prerequisites

- [text-to-sql-proxy](https://github.com/tobilg/text-to-sql-proxy) running locally
- Audio device configured (see [Setup Audio Device](#setup-audio-device-required-for-recording))

### Voice Query Functions

#### `whisper_voice_to_sql([model], [device_id])`

Records voice, transcribes, and returns generated SQL without executing.

```sql
SELECT whisper_voice_to_sql();
```

#### `whisper_voice_query([model], [device_id])`

Records voice, generates SQL, executes it, and returns results.

```sql
FROM whisper_voice_query();
```

#### `whisper_voice_query_with_sql([model], [device_id])`

Same as above but includes `_generated_sql` and `_transcription` columns.

```sql
FROM whisper_voice_query_with_sql();
```

### Voice Query Configuration

```sql
-- Set text-to-sql proxy URL (default: http://localhost:4000/generate-sql)
SET whisper_text_to_sql_url = 'http://localhost:8080/generate-sql';
SELECT current_setting('whisper_text_to_sql_url');

-- Set timeout (default: 30 seconds)
SET whisper_text_to_sql_timeout = 60;
SELECT current_setting('whisper_text_to_sql_timeout');

-- Show generated SQL in output
SET whisper_voice_query_show_sql = true;
SELECT current_setting('whisper_voice_query_show_sql');
```

## Building from Source

For developers who want to build the extension locally.

### Prerequisites

- CMake 3.14+
- C++17 compiler
- [vcpkg](https://github.com/microsoft/vcpkg) package manager

### Setup vcpkg

First, clone and bootstrap vcpkg (one-time setup):

```bash
# Clone vcpkg (outside the project directory)
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg

# Bootstrap vcpkg
./bootstrap-vcpkg.sh  # macOS/Linux
# or
.\bootstrap-vcpkg.bat  # Windows
```

### Install Dependencies

```bash
# Install required dependencies
~/vcpkg/vcpkg install ffmpeg sdl2 curl
```

### Clone and Build

```bash
# Clone the repository
git clone --recursive https://github.com/tobilg/duckdb-whisper.git
cd duckdb-whisper

# Build with vcpkg toolchain
VCPKG_TOOLCHAIN_PATH=~/vcpkg/scripts/buildsystems/vcpkg.cmake make release -j8
```

### Platform-Specific Notes

#### macOS

You may need to install additional tools:

```bash
brew install cmake ninja pkg-config
```

#### Ubuntu/Debian

Install build essentials:

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
    nasm autoconf automake libtool
```

#### Windows

Use the Developer Command Prompt or PowerShell with vcpkg:

```powershell
$env:VCPKG_TOOLCHAIN_PATH="C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake"
make release
```

### Build Options

```bash
# Build without audio recording
EXT_FLAGS="-DWHISPER_ENABLE_RECORDING=OFF" make release

# Build without voice-to-SQL
EXT_FLAGS="-DWHISPER_ENABLE_VOICE_QUERY=OFF" make release
```

### Running Tests

```bash
make test_whisper        # All tests (requires tiny.en model)
make test_whisper_quick  # Quick tests (no model needed)
```

## License

This extension is licensed under the MIT License.

## Acknowledgments

- [OpenAI Whisper](https://github.com/openai/whisper) - Original Whisper model
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp) - C/C++ port of Whisper
- [DuckDB](https://duckdb.org) - The database engine
- [FFmpeg](https://ffmpeg.org) - Audio decoding
