# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(whisper
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    EXTENSION_VERSION v0.4.0
)

# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)
