// libcurl-backed HTTP transports. Only exercised by real network use;
// unit tests substitute fakes through the HttpGet/HttpPostForm seams.
#include "rocket_watcher/http.hpp"

#include <mutex>
#include <stdexcept>

#include <curl/curl.h>

namespace rocket_watcher {

namespace {

constexpr const char* kUserAgent = "rocket-watcher/0.1 (personal launch notifier)";

void ensureCurlGlobalInit() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t writeToString(char* data, size_t size, size_t count, void* destination) {
  auto* body = static_cast<std::string*>(destination);
  body->append(data, size * count);
  return size * count;
}

struct CurlHandle {
  CURL* handle = nullptr;

  CurlHandle() {
    ensureCurlGlobalInit();
    handle = curl_easy_init();
    if (handle == nullptr) throw std::runtime_error("curl_easy_init failed");
  }
  ~CurlHandle() { curl_easy_cleanup(handle); }
  CurlHandle(const CurlHandle&) = delete;
  CurlHandle& operator=(const CurlHandle&) = delete;
};

HttpResponse perform(CurlHandle& curl, const std::string& url, std::chrono::seconds timeout) {
  std::string body;
  curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, static_cast<long>(timeout.count()));
  curl_easy_setopt(curl.handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, writeToString);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &body);

  CURLcode result = curl_easy_perform(curl.handle);
  if (result != CURLE_OK) {
    throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(result));
  }
  long status = 0;
  curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
  return HttpResponse{status, body};
}

std::string urlEncode(CurlHandle& curl, const std::string& text) {
  char* escaped = curl_easy_escape(curl.handle, text.c_str(), static_cast<int>(text.size()));
  if (escaped == nullptr) throw std::runtime_error("curl_easy_escape failed");
  std::string result = escaped;
  curl_free(escaped);
  return result;
}

}  // namespace

HttpGet curlHttpGet() {
  return [](const std::string& url, std::chrono::seconds timeout) -> std::string {
    CurlHandle curl;
    HttpResponse response = perform(curl, url, timeout);
    if (response.status >= 400) {
      throw std::runtime_error("HTTP " + std::to_string(response.status));
    }
    return response.body;
  };
}

HttpPostForm curlHttpPostForm() {
  return [](const std::string& url, const std::vector<std::pair<std::string, std::string>>& fields,
            std::chrono::seconds timeout) -> HttpResponse {
    CurlHandle curl;
    std::string form;
    for (const auto& [key, value] : fields) {
      if (!form.empty()) form += "&";
      form += urlEncode(curl, key) + "=" + urlEncode(curl, value);
    }
    curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, form.c_str());
    return perform(curl, url, timeout);
  };
}

}  // namespace rocket_watcher
