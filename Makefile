PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=whisper
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Use the same release triplets locally and in CI. The Linux triplets keep the
# ALSA backend dynamically loaded while embedding FFmpeg and SDL2 themselves.
ifeq ($(shell uname -s),Darwin)
ifeq ($(shell uname -m),arm64)
WHISPER_NATIVE_TRIPLET := arm64-osx-release
else
WHISPER_NATIVE_TRIPLET := x64-osx-release
endif
else ifeq ($(shell uname -m),aarch64)
WHISPER_NATIVE_TRIPLET := arm64-linux-release
else
WHISPER_NATIVE_TRIPLET := x64-linux-release
endif
VCPKG_TARGET_TRIPLET ?= $(WHISPER_NATIVE_TRIPLET)
VCPKG_HOST_TRIPLET ?= $(WHISPER_NATIVE_TRIPLET)

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

.PHONY: bump-version
bump-version:
	@test -n "$(V)" || { echo "Usage: make bump-version V=X.Y.Z" >&2; exit 2; }
	@python3 "$(PROJ_DIR)scripts/bump_version.py" "$(V)"

# Custom test targets for whisper extension
test_whisper: release
	@echo "Running whisper extension tests..."
	@WHISPER_TEST_MODEL=1 ./build/release/test/unittest --keep-home 'test/sql/whisper*'

test_whisper_quick: release
	@echo "Running whisper tests (without transcription)..."
	@./build/release/test/unittest "test/sql/whisper.test"
	@./build/release/test/unittest "test/sql/whisper_models.test"
	@./build/release/test/unittest "test/sql/whisper_audio.test"
	@./build/release/test/unittest "test/sql/whisper_audio_formats.test"
