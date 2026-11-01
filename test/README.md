# Testing this extension
This directory contains all the tests for this extension. The `sql` directory holds tests that are written as [SQLLogicTests](https://duckdb.org/dev/sqllogictest/intro.html). DuckDB aims to have most its tests in this format as SQL statements, so for the quack extension, this should probably be the goal too.

The root makefile contains targets to build and run all of these tests. To run the SQLLogicTests:
```bash
make test
```
or 
```bash
make test_debug
```

`test/sql/whisper_audio_formats.test` checks all packaged audio formats through
both file and BLOB input. It requires no model: successful decoding must reach
the deliberately missing model, rather than failing to decode or returning empty
audio. The short 440 Hz stereo fixtures are synthetic and can be regenerated with
`python3 scripts/generate_audio_fixtures.py` using an FFmpeg CLI with MP3, native
Vorbis, and Opus encoders. No encoder or FFmpeg executable is needed to run tests.
