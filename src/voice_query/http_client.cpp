#ifdef WHISPER_ENABLE_VOICE_QUERY

#include "http_client.hpp"
#include <curl/curl.h>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace duckdb {

// Callback for libcurl to write response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t total_size = size * nmemb;
	std::string *response = static_cast<std::string *>(userp);
	response->append(static_cast<char *>(contents), total_size);
	return total_size;
}

HttpClient::HttpClient() : curl_handle(nullptr) {
	curl_handle = curl_easy_init();
}

HttpClient::~HttpClient() {
	if (curl_handle) {
		curl_easy_cleanup(static_cast<CURL *>(curl_handle));
		curl_handle = nullptr;
	}
}

HttpResponse HttpClient::Post(const std::string &url, const std::string &json_body, int32_t timeout_seconds) {
	HttpResponse response;

	if (!curl_handle) {
		response.error = "Failed to initialize HTTP client";
		return response;
	}

	CURL *curl = static_cast<CURL *>(curl_handle);
	std::string response_body;

	// Reset curl handle for reuse
	curl_easy_reset(curl);

	// Set URL
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	// Set POST data
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));

	// Set headers
	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	// Set response callback
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

	// Set timeout (total operation) and connect timeout (just for reaching the server)
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);

	// Required for multi-threaded environments - don't use signals for timeout
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	// Force IPv4 to avoid potential IPv6 issues with localhost
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

	// Follow redirects
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	// Perform request
	CURLcode res = curl_easy_perform(curl);

	// Clean up headers
	curl_slist_free_all(headers);

	if (res != CURLE_OK) {
		if (res == CURLE_OPERATION_TIMEDOUT) {
			response.error = "Request timed out after " + std::to_string(timeout_seconds) + " seconds";
		} else if (res == CURLE_COULDNT_CONNECT) {
			response.error = "Cannot connect to text-to-sql proxy at " + url;
		} else {
			response.error = std::string("HTTP request failed: ") + curl_easy_strerror(res);
		}
		return response;
	}

	// Get status code
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);

	response.body = std::move(response_body);

	if (response.status_code >= 200 && response.status_code < 300) {
		response.success = true;
	} else {
		response.error = "Text-to-SQL proxy error: HTTP " + std::to_string(response.status_code);
		if (!response.body.empty()) {
			response.error += " - " + response.body;
		}
	}

	return response;
}

std::string EscapeJsonString(const std::string &str) {
	std::ostringstream escaped;
	for (char c : str) {
		switch (c) {
		case '"':
			escaped << "\\\"";
			break;
		case '\\':
			escaped << "\\\\";
			break;
		case '\b':
			escaped << "\\b";
			break;
		case '\f':
			escaped << "\\f";
			break;
		case '\n':
			escaped << "\\n";
			break;
		case '\r':
			escaped << "\\r";
			break;
		case '\t':
			escaped << "\\t";
			break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				// Control character - encode as \u00XX
				escaped << "\\u00";
				escaped << std::hex << ((c >> 4) & 0xf) << (c & 0xf);
			} else {
				escaped << c;
			}
		}
	}
	return escaped.str();
}

std::string BuildJsonRequest(const std::string &ddl, const std::string &question) {
	std::ostringstream json;
	json << "{\"ddl\":\"" << EscapeJsonString(ddl) << "\",\"question\":\"" << EscapeJsonString(question) << "\"}";
	return json.str();
}

// Helper to skip whitespace
static const char *SkipWhitespace(const char *ptr, const char *end) {
	while (ptr < end && std::isspace(static_cast<unsigned char>(*ptr))) {
		ptr++;
	}
	return ptr;
}

// Helper to parse a JSON string value (assumes we're positioned at the opening quote)
static std::string ParseJsonStringValue(const char *&ptr, const char *end) {
	if (ptr >= end || *ptr != '"') {
		return "";
	}
	ptr++; // Skip opening quote

	std::string result;
	while (ptr < end && *ptr != '"') {
		if (*ptr == '\\' && ptr + 1 < end) {
			ptr++;
			switch (*ptr) {
			case '"':
				result += '"';
				break;
			case '\\':
				result += '\\';
				break;
			case '/':
				result += '/';
				break;
			case 'b':
				result += '\b';
				break;
			case 'f':
				result += '\f';
				break;
			case 'n':
				result += '\n';
				break;
			case 'r':
				result += '\r';
				break;
			case 't':
				result += '\t';
				break;
			case 'u':
				// Unicode escape - simplified handling
				if (ptr + 4 < end) {
					// Just skip for now - proper handling would convert to UTF-8
					ptr += 4;
				}
				break;
			default:
				result += *ptr;
			}
		} else {
			result += *ptr;
		}
		ptr++;
	}

	if (ptr < end && *ptr == '"') {
		ptr++; // Skip closing quote
	}

	return result;
}

std::string ParseSqlFromJson(const std::string &json_str) {
	// Simple JSON parser for {"sql": "..."} or {"sql": "...", ...}
	const char *ptr = json_str.c_str();
	const char *end = ptr + json_str.size();

	// Skip to opening brace
	ptr = SkipWhitespace(ptr, end);
	if (ptr >= end || *ptr != '{') {
		return "";
	}
	ptr++;

	// Look for "sql" key
	while (ptr < end) {
		ptr = SkipWhitespace(ptr, end);

		if (ptr >= end)
			break;

		if (*ptr == '}') {
			break; // End of object
		}

		if (*ptr == ',') {
			ptr++;
			continue;
		}

		// Parse key
		if (*ptr != '"') {
			ptr++;
			continue;
		}

		std::string key = ParseJsonStringValue(ptr, end);

		// Skip to colon
		ptr = SkipWhitespace(ptr, end);
		if (ptr >= end || *ptr != ':') {
			continue;
		}
		ptr++;

		// Skip whitespace after colon
		ptr = SkipWhitespace(ptr, end);

		if (key == "sql") {
			if (ptr < end && *ptr == '"') {
				return ParseJsonStringValue(ptr, end);
			}
			break;
		} else {
			// Skip this value - could be string, number, object, array, etc.
			if (*ptr == '"') {
				ParseJsonStringValue(ptr, end);
			} else if (*ptr == '{') {
				// Skip nested object
				int depth = 1;
				ptr++;
				while (ptr < end && depth > 0) {
					if (*ptr == '{')
						depth++;
					else if (*ptr == '}')
						depth--;
					else if (*ptr == '"')
						ParseJsonStringValue(ptr, end);
					else
						ptr++;
				}
			} else if (*ptr == '[') {
				// Skip array
				int depth = 1;
				ptr++;
				while (ptr < end && depth > 0) {
					if (*ptr == '[')
						depth++;
					else if (*ptr == ']')
						depth--;
					else if (*ptr == '"')
						ParseJsonStringValue(ptr, end);
					else
						ptr++;
				}
			} else {
				// Skip number, boolean, null
				while (ptr < end && *ptr != ',' && *ptr != '}' && !std::isspace(static_cast<unsigned char>(*ptr))) {
					ptr++;
				}
			}
		}
	}

	return "";
}

} // namespace duckdb

#endif // WHISPER_ENABLE_VOICE_QUERY
