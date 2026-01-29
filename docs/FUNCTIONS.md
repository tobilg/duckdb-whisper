# DuckDB Whisper Extension - Function Reference

Complete reference for all functions provided by the whisper extension.

## Table of Contents

- [Transcription Functions](#transcription-functions)
  - [whisper_transcribe](#whisper_transcribe)
  - [whisper_translate](#whisper_translate)
  - [whisper_transcribe_segments](#whisper_transcribe_segments)
- [Recording Functions](#recording-functions)
  - [whisper_list_devices](#whisper_list_devices)
  - [whisper_record](#whisper_record)
  - [whisper_record_translate](#whisper_record_translate)
  - [whisper_record_auto](#whisper_record_auto)
- [Model Management Functions](#model-management-functions)
  - [whisper_list_models](#whisper_list_models)
  - [whisper_download_model](#whisper_download_model)
  - [whisper_delete_model](#whisper_delete_model)
- [Utility Functions](#utility-functions)
  - [whisper_version](#whisper_version)
  - [whisper_check_audio](#whisper_check_audio)
  - [whisper_audio_info](#whisper_audio_info)

---

## Transcription Functions

### whisper_transcribe

Transcribes audio and returns the complete transcription as a single string.

#### Signatures

```sql
whisper_transcribe(file_path VARCHAR) -> VARCHAR
whisper_transcribe(file_path VARCHAR, model VARCHAR) -> VARCHAR
whisper_transcribe(audio_data BLOB) -> VARCHAR
whisper_transcribe(audio_data BLOB, model VARCHAR) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| file_path | VARCHAR | Yes* | Path to an audio file |
| audio_data | BLOB | Yes* | Raw audio data in memory |
| model | VARCHAR | No | Model name to use (e.g., 'tiny.en', 'base.en') |

*Either `file_path` or `audio_data` is required.

#### Returns

VARCHAR containing the full transcription text.

#### Examples

```sql
-- Transcribe from file with model
SELECT whisper_transcribe('interview.wav', 'tiny.en');

-- Transcribe from file with larger model for better accuracy
SELECT whisper_transcribe('lecture.mp3', 'small.en');

-- Transcribe BLOB data from a table
SELECT id, whisper_transcribe(audio_column, 'base.en') as transcript
FROM recordings;

-- Batch transcribe multiple files
SELECT file, whisper_transcribe(file, 'tiny.en') as text
FROM glob('audio/*.wav');
```

#### Errors

- `Failed to load audio` - The audio file could not be read or decoded
- `Failed to load whisper model` - The specified model is not downloaded
- `Transcription failed` - An error occurred during transcription

---

### whisper_translate

Translates audio from any language to English. Uses Whisper's built-in translation capability.

#### Signatures

```sql
whisper_translate(file_path VARCHAR) -> VARCHAR
whisper_translate(file_path VARCHAR, model VARCHAR) -> VARCHAR
whisper_translate(audio_data BLOB) -> VARCHAR
whisper_translate(audio_data BLOB, model VARCHAR) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| file_path | VARCHAR | Yes* | Path to an audio file |
| audio_data | BLOB | Yes* | Raw audio data in memory |
| model | VARCHAR | No | Model name (multilingual models recommended) |

*Either `file_path` or `audio_data` is required.

#### Returns

VARCHAR containing the English translation of the audio.

#### Notes

- **Whisper only supports translation TO English.** It cannot translate to other languages.
- Use multilingual models (without `.en` suffix) for best results when translating from other languages.
- English-only models (`.en`) will still work but are optimized for English input.

#### Examples

```sql
-- Translate German audio to English
SELECT whisper_translate('interview_german.mp3', 'small');

-- Translate French podcast to English
SELECT whisper_translate('podcast_french.mp3', 'medium');

-- Translate from BLOB
SELECT id, whisper_translate(audio_column, 'base') as english_text
FROM foreign_recordings;
```

#### Errors

- `Translation failed: Failed to load audio` - The audio file could not be read
- `Translation failed: Failed to load whisper model` - The model is not downloaded

---

### whisper_transcribe_segments

Transcribes audio and returns detailed segments with timestamps, confidence scores, and language information.

#### Signature

```sql
whisper_transcribe_segments(file_path VARCHAR, [model VARCHAR], [language VARCHAR], [translate BOOLEAN]) -> TABLE
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| file_path | VARCHAR | Yes | Path to an audio file |
| model | VARCHAR | No | Model name to use (default: 'base.en') |
| language | VARCHAR | No | Language hint (e.g., 'en', 'de', 'fr') or 'auto' |
| translate | BOOLEAN | No | If true, translate to English (default: false) |

#### Returns

A table with the following columns:

| Column | Type | Description |
|--------|------|-------------|
| segment_id | INTEGER | 0-based index of the segment |
| start_time | DOUBLE | Start time in seconds |
| end_time | DOUBLE | End time in seconds |
| text | VARCHAR | Transcribed text for this segment |
| confidence | DOUBLE | Average confidence score (0.0 to 1.0) |
| language | VARCHAR | Detected language code (ISO 639-1) |

#### Examples

```sql
-- Basic segment transcription
SELECT * FROM whisper_transcribe_segments('podcast.mp3', 'base.en');

-- Translate to English with segments
SELECT * FROM whisper_transcribe_segments('german_interview.mp3', 'small', 'de', true);

-- Get only high-confidence segments
SELECT * FROM whisper_transcribe_segments('interview.wav', 'small.en')
WHERE confidence > 0.85;

-- Calculate total speaking time
SELECT SUM(end_time - start_time) as total_seconds
FROM whisper_transcribe_segments('meeting.wav', 'tiny.en');

-- Find segments containing specific words
SELECT segment_id, start_time, text
FROM whisper_transcribe_segments('lecture.mp3', 'base.en')
WHERE text ILIKE '%important%';

-- Export as subtitle format
SELECT
    segment_id + 1 as id,
    start_time,
    end_time,
    trim(text) as text
FROM whisper_transcribe_segments('video.mp4', 'small.en');
```

---

## Recording Functions

These functions require SDL2 and are enabled by default at compile time. Build with `-DWHISPER_ENABLE_RECORDING=OFF` to disable.

### whisper_list_devices

Lists available audio input (capture) devices.

#### Signature

```sql
whisper_list_devices() -> TABLE
```

#### Returns

A table with the following columns:

| Column | Type | Description |
|--------|------|-------------|
| device_id | INTEGER | Device ID for use with recording functions |
| device_name | VARCHAR | Human-readable device name |

#### Examples

```sql
-- List all microphones
SELECT * FROM whisper_list_devices();

-- Find a specific device
SELECT device_id FROM whisper_list_devices()
WHERE device_name LIKE '%USB%';
```

---

### whisper_record

Records audio from microphone for a specified duration and transcribes it.

#### Signatures

```sql
whisper_record(duration_seconds INTEGER) -> VARCHAR
whisper_record(duration_seconds INTEGER, model VARCHAR) -> VARCHAR
whisper_record(duration_seconds INTEGER, model VARCHAR, device_id INTEGER) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| duration_seconds | INTEGER | Yes | How long to record in seconds |
| model | VARCHAR | No | Model to use (default: 'base.en') |
| device_id | INTEGER | No | Device from whisper_list_devices() (-1 for default) |

#### Returns

VARCHAR containing the transcribed text.

#### Examples

```sql
-- Record 5 seconds from default microphone
SELECT whisper_record(5, 'tiny.en');

-- Record from specific device
SELECT whisper_record(10, 'base.en', 0);

-- Quick voice note
SELECT whisper_record(30, 'small.en') as note;
```

---

### whisper_record_translate

Records audio and translates it to English.

#### Signatures

```sql
whisper_record_translate(duration_seconds INTEGER) -> VARCHAR
whisper_record_translate(duration_seconds INTEGER, model VARCHAR) -> VARCHAR
whisper_record_translate(duration_seconds INTEGER, model VARCHAR, device_id INTEGER) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| duration_seconds | INTEGER | Yes | How long to record in seconds |
| model | VARCHAR | No | Multilingual model to use (default: 'base') |
| device_id | INTEGER | No | Device from whisper_list_devices() |

#### Returns

VARCHAR containing the English translation.

#### Notes

- Use multilingual models (without `.en` suffix) for translation
- English-only models will error when used with this function

#### Examples

```sql
-- Record and translate to English
SELECT whisper_record_translate(10, 'small');

-- From specific device
SELECT whisper_record_translate(30, 'medium', 0);
```

---

### whisper_record_auto

Records audio and automatically stops when silence is detected. Ideal for voice commands, dictation, or hands-free transcription.

#### Signatures

```sql
whisper_record_auto(max_seconds INTEGER) -> VARCHAR
whisper_record_auto(max_seconds INTEGER, silence_seconds DOUBLE) -> VARCHAR
whisper_record_auto(max_seconds INTEGER, silence_seconds DOUBLE, model VARCHAR) -> VARCHAR
whisper_record_auto(max_seconds INTEGER, silence_seconds DOUBLE, model VARCHAR, threshold DOUBLE) -> VARCHAR
whisper_record_auto(max_seconds INTEGER, silence_seconds DOUBLE, model VARCHAR, threshold DOUBLE, device_id INTEGER) -> VARCHAR
```

#### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| max_seconds | INTEGER | required | Maximum recording time in seconds |
| silence_seconds | DOUBLE | 2.0 | Duration of silence to trigger stop |
| model | VARCHAR | 'base.en' | Model to use for transcription |
| threshold | DOUBLE | 0.01 | Silence threshold (0.0-1.0), higher = less sensitive |
| device_id | INTEGER | -1 | Device ID from whisper_list_devices() |

#### Returns

VARCHAR containing the transcribed text, or NULL if no audio was captured.

#### How It Works

1. Recording starts and monitors audio amplitude
2. Once sound is detected, the silence timer activates
3. Recording stops when:
   - Silence duration exceeds `silence_seconds` (after sound was detected)
   - Maximum duration `max_seconds` is reached
4. Audio is transcribed and returned

#### Examples

```sql
-- Voice command: up to 10 seconds, stop after 2 seconds silence
SELECT whisper_record_auto(10, 2.0, 'tiny.en');

-- Dictation: longer max, shorter silence gap
SELECT whisper_record_auto(60, 1.5, 'base.en');

-- Quick response: stop quickly after silence
SELECT whisper_record_auto(10, 1.0, 'tiny.en');

-- Noisy environment: higher threshold
SELECT whisper_record_auto(30, 2.0, 'small.en', 0.05);
```

#### Notes

- The function waits for sound before starting the silence timer
- If no sound is ever detected, it will run until `max_seconds`
- Adjust `threshold` for noisy environments (higher = requires louder sound)

---

## Model Management Functions

### whisper_list_models

Lists all available Whisper models with their download status and metadata.

#### Signature

```sql
whisper_list_models() -> TABLE
```

#### Returns

A table with the following columns:

| Column | Type | Description |
|--------|------|-------------|
| name | VARCHAR | Model identifier (e.g., 'tiny.en', 'base') |
| is_downloaded | BOOLEAN | TRUE if model exists locally |
| file_size | BIGINT | File size in bytes (NULL if not downloaded) |
| file_path | VARCHAR | Full path to the model file |
| description | VARCHAR | Human-readable model description |

#### Examples

```sql
-- List all models
SELECT * FROM whisper_list_models();

-- Show only downloaded models
SELECT name, file_size / 1e6 as size_mb
FROM whisper_list_models()
WHERE is_downloaded = true;

-- Find English-only models
SELECT * FROM whisper_list_models()
WHERE name LIKE '%.en';

-- Check if a specific model is available
SELECT is_downloaded FROM whisper_list_models()
WHERE name = 'small.en';
```

---

### whisper_download_model

Provides instructions for downloading a Whisper model.

#### Signature

```sql
whisper_download_model(model_name VARCHAR) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| model_name | VARCHAR | Yes | Name of the model to download |

#### Returns

Instructions for downloading the model using curl or DuckDB's httpfs extension.

#### Valid Model Names

- `tiny`, `tiny.en`
- `base`, `base.en`
- `small`, `small.en`
- `medium`, `medium.en`
- `large-v1`, `large-v2`, `large-v3`, `large-v3-turbo`

#### Examples

```sql
-- Get download instructions
SELECT whisper_download_model('small.en');

-- Returns something like:
-- Please download the model manually:
--   curl -L -o '~/.duckdb/whisper/models/ggml-small.en.bin' 'https://...'
```

#### Errors

- `Invalid model name` - The specified model name is not recognized

---

### whisper_delete_model

Deletes a locally stored model file.

#### Signature

```sql
whisper_delete_model(model_name VARCHAR) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| model_name | VARCHAR | Yes | Name of the model to delete |

#### Returns

'OK' on success.

#### Examples

```sql
-- Delete a model
SELECT whisper_delete_model('large-v3');

-- Verify deletion
SELECT * FROM whisper_list_models() WHERE name = 'large-v3';
```

#### Errors

- `Invalid model name` - The specified model name is not recognized
- `Model not found` - The model is not downloaded

---

## Utility Functions

### whisper_version

Returns version information about the extension and underlying whisper.cpp library.

#### Signature

```sql
whisper_version() -> VARCHAR
```

#### Returns

A string containing version information in the format:
`whisper extension v<version> (whisper.cpp: <whisper_version>)`

#### Examples

```sql
SELECT whisper_version();
-- Returns: whisper extension v1.0.0 (whisper.cpp: 1.8.3)
```

---

### whisper_check_audio

Validates that an audio file can be read and processed by the extension.

#### Signature

```sql
whisper_check_audio(file_path VARCHAR) -> VARCHAR
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| file_path | VARCHAR | Yes | Path to the audio file to check |

#### Returns

- `'OK'` if the file is valid and can be processed
- `'Error: <message>'` if there's a problem with the file

#### Examples

```sql
-- Check a single file
SELECT whisper_check_audio('audio.wav');

-- Validate multiple files
SELECT file, whisper_check_audio(file) as status
FROM glob('uploads/*.mp3');

-- Find invalid files
SELECT file FROM glob('audio/*')
WHERE whisper_check_audio(file) != 'OK';
```

---

### whisper_audio_info

Returns detailed metadata about an audio file.

#### Signature

```sql
whisper_audio_info(file_path VARCHAR) -> TABLE
```

#### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| file_path | VARCHAR | Yes | Path to the audio file |

#### Returns

A single-row table with the following columns:

| Column | Type | Description |
|--------|------|-------------|
| file_path | VARCHAR | The input file path |
| duration_seconds | DOUBLE | Duration of the audio in seconds |
| sample_rate | INTEGER | Sample rate in Hz |
| channels | INTEGER | Number of audio channels |
| format | VARCHAR | Audio format/codec name |
| file_size | BIGINT | File size in bytes |

#### Examples

```sql
-- Get info for a single file
SELECT * FROM whisper_audio_info('podcast.mp3');

-- Get duration in minutes
SELECT duration_seconds / 60.0 as minutes
FROM whisper_audio_info('lecture.wav');

-- Analyze multiple files
SELECT
    file,
    info.duration_seconds,
    info.format
FROM glob('audio/*') g,
LATERAL whisper_audio_info(g.file) info;

-- Find stereo files
SELECT file FROM glob('audio/*.wav') g,
LATERAL whisper_audio_info(g.file) info
WHERE info.channels = 2;
```

#### Errors

- `Failed to read audio info` - The file could not be opened or is not a valid audio file
