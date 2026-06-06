#pragma once

#include <otomarket/license/LicenseSdk.h>

#include <juce_core/juce_core.h>

namespace otomarket::license {

class JuceHttpTransport final : public HttpTransport {
public:
  explicit JuceHttpTransport(int timeoutMs = 10000)
    : timeoutMs_(timeoutMs) {}

  HttpResponse postJson(
    const std::string& url,
    const std::string& body,
    const std::vector<HttpHeader>& headers
  ) override {
    juce::String extraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n");

    for (const auto& header : headers) {
      if (header.name == "Content-Type" || header.name == "Accept") {
        continue;
      }
      extraHeaders << juce::String(header.name) << ": " << juce::String(header.value) << "\r\n";
    }

    int statusCode = 0;
    auto inputStream = juce::URL(juce::String(url))
      .withPOSTData(juce::String(body))
      .createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
          .withConnectionTimeoutMs(timeoutMs_)
          .withExtraHeaders(extraHeaders)
          .withStatusCode(&statusCode)
      );

    if (inputStream == nullptr) {
      return {statusCode, {}};
    }

    return {
      statusCode,
      inputStream->readEntireStreamAsString().toStdString()
    };
  }

private:
  int timeoutMs_ = 10000;
};

} // namespace otomarket::license
