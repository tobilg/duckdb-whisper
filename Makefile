PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=whisper
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Custom test targets for whisper extension
test_whisper: release
	@echo "Running whisper extension tests..."
	@for test in test/sql/whisper*.test; do \
		echo "=== $$test ==="; \
		WHISPER_TEST_MODEL=1 ./build/release/test/unittest "$$test"; \
	done

test_whisper_quick: release
	@echo "Running whisper tests (without transcription)..."
	@./build/release/test/unittest "test/sql/whisper.test"
	@./build/release/test/unittest "test/sql/whisper_models.test"
	@./build/release/test/unittest "test/sql/whisper_audio.test"