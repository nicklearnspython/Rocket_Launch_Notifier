// HTTP transport seams.
//
// Adapters depend on these function types, not on libcurl, so unit tests
// can substitute fakes. The libcurl-backed implementations live in
// curl_http.cpp and are only exercised by real network use.
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rocket_watcher {

struct HttpResponse {
  long status = 0;
  std::string body;
};

// GET that returns the response body. Throws std::runtime_error on
// network failure or a non-2xx status.
using HttpGet = std::function<std::string(const std::string& url, std::chrono::seconds timeout)>;

// Form-encoded POST. Returns status and body even for HTTP errors;
// throws std::runtime_error only when no response was received.
using HttpPostForm = std::function<HttpResponse(
    const std::string& url, const std::vector<std::pair<std::string, std::string>>& fields,
    std::chrono::seconds timeout)>;

// libcurl-backed implementations.
HttpGet curlHttpGet();
HttpPostForm curlHttpPostForm();

}  // namespace rocket_watcher
