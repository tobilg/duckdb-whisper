#pragma once

#ifdef WHISPER_ENABLE_VOICE_QUERY

#include <string>
#include <cstdint>

namespace duckdb {

struct HttpResponse {
    bool success = false;
    std::string body;
    std::string error;
    long status_code = 0;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Perform HTTP POST with JSON body
    HttpResponse Post(const std::string &url, const std::string &json_body, int32_t timeout_seconds = 30);

private:
    void *curl_handle;
};

// JSON helpers (no external library dependency)
// Parse SQL from {"sql": "..."} response
std::string ParseSqlFromJson(const std::string &json_str);

// Build JSON request body for text-to-sql proxy
std::string BuildJsonRequest(const std::string &ddl, const std::string &question);

// Escape string for JSON
std::string EscapeJsonString(const std::string &str);

} // namespace duckdb

#endif // WHISPER_ENABLE_VOICE_QUERY
