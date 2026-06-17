#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "tweetnacl.h"
}

namespace otomarket::license::detail {

inline constexpr std::array<unsigned char, 12> kEd25519SpkiPrefix = {
  0x30, 0x2a, 0x30, 0x05, 0x06, 0x03,
  0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
};

struct JsonValue {
  enum class Type {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
  };

  Type type = Type::Null;
  bool boolValue = false;
  double numberValue = 0;
  std::string stringValue;
  std::vector<JsonValue> arrayValue;
  std::map<std::string, JsonValue> objectValue;
};

class JsonParser {
public:
  explicit JsonParser(const std::string& source)
    : source_(source) {}

  JsonValue parse() {
    skipWhitespace();
    JsonValue value = parseValue();
    skipWhitespace();

    if (position_ != source_.size()) {
      throw std::runtime_error("Unexpected trailing JSON input.");
    }

    return value;
  }

private:
  JsonValue parseValue() {
    skipWhitespace();

    if (position_ >= source_.size()) {
      throw std::runtime_error("Unexpected end of JSON input.");
    }

    const char ch = source_[position_];

    if (ch == '{') {
      return parseObject();
    }

    if (ch == '[') {
      return parseArray();
    }

    if (ch == '"') {
      JsonValue value;
      value.type = JsonValue::Type::String;
      value.stringValue = parseString();
      return value;
    }

    if (ch == 't') {
      consumeLiteral("true");
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolValue = true;
      return value;
    }

    if (ch == 'f') {
      consumeLiteral("false");
      JsonValue value;
      value.type = JsonValue::Type::Bool;
      value.boolValue = false;
      return value;
    }

    if (ch == 'n') {
      consumeLiteral("null");
      return {};
    }

    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      return parseNumber();
    }

    throw std::runtime_error("Unsupported JSON value.");
  }

  JsonValue parseObject() {
    JsonValue value;
    value.type = JsonValue::Type::Object;
    expect('{');
    skipWhitespace();

    if (peek('}')) {
      ++position_;
      return value;
    }

    while (true) {
      skipWhitespace();
      const std::string key = parseString();
      skipWhitespace();
      expect(':');
      value.objectValue.emplace(key, parseValue());
      skipWhitespace();

      if (peek('}')) {
        ++position_;
        return value;
      }

      expect(',');
    }
  }

  JsonValue parseArray() {
    JsonValue value;
    value.type = JsonValue::Type::Array;
    expect('[');
    skipWhitespace();

    if (peek(']')) {
      ++position_;
      return value;
    }

    while (true) {
      value.arrayValue.push_back(parseValue());
      skipWhitespace();

      if (peek(']')) {
        ++position_;
        return value;
      }

      expect(',');
    }
  }

  JsonValue parseNumber() {
    const size_t start = position_;

    if (peek('-')) {
      ++position_;
    }

    while (position_ < source_.size() &&
           std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
      ++position_;
    }

    if (peek('.')) {
      ++position_;
      while (position_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
        ++position_;
      }
    }

    if (peek('e') || peek('E')) {
      ++position_;
      if (peek('+') || peek('-')) {
        ++position_;
      }
      while (position_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
        ++position_;
      }
    }

    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.numberValue = std::stod(source_.substr(start, position_ - start));
    return value;
  }

  std::string parseString() {
    expect('"');
    std::string result;

    while (position_ < source_.size()) {
      const char ch = source_[position_++];

      if (ch == '"') {
        return result;
      }

      if (ch != '\\') {
        result.push_back(ch);
        continue;
      }

      if (position_ >= source_.size()) {
        throw std::runtime_error("Unexpected end of JSON escape.");
      }

      const char escaped = source_[position_++];

      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u':
          appendUnicodeEscape(result);
          break;
        default:
          throw std::runtime_error("Unsupported JSON escape.");
      }
    }

    throw std::runtime_error("Unterminated JSON string.");
  }

  void appendUnicodeEscape(std::string& output) {
    if (position_ + 4 > source_.size()) {
      throw std::runtime_error("Invalid JSON unicode escape.");
    }

    unsigned int codepoint = 0;
    for (int index = 0; index < 4; ++index) {
      const char ch = source_[position_++];
      codepoint <<= 4;

      if (ch >= '0' && ch <= '9') {
        codepoint += static_cast<unsigned int>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        codepoint += static_cast<unsigned int>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        codepoint += static_cast<unsigned int>(ch - 'A' + 10);
      } else {
        throw std::runtime_error("Invalid JSON unicode escape.");
      }
    }

    if (codepoint <= 0x7f) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }

  void consumeLiteral(const char* literal) {
    const size_t length = std::strlen(literal);

    if (source_.compare(position_, length, literal) != 0) {
      throw std::runtime_error("Invalid JSON literal.");
    }

    position_ += length;
  }

  void expect(char expected) {
    if (position_ >= source_.size() || source_[position_] != expected) {
      throw std::runtime_error("Unexpected JSON character.");
    }
    ++position_;
  }

  bool peek(char expected) const {
    return position_ < source_.size() && source_[position_] == expected;
  }

  void skipWhitespace() {
    while (position_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[position_])) != 0) {
      ++position_;
    }
  }

  const std::string& source_;
  size_t position_ = 0;
};

inline std::string trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(start, end - start);
}

inline std::string replaceEscapedNewlines(std::string value) {
  size_t position = 0;
  while ((position = value.find("\\n", position)) != std::string::npos) {
    value.replace(position, 2, "\n");
    ++position;
  }
  return value;
}

inline const JsonValue* objectField(const JsonValue& value, const std::string& key) {
  if (value.type != JsonValue::Type::Object) {
    return nullptr;
  }

  const auto found = value.objectValue.find(key);
  if (found == value.objectValue.end()) {
    return nullptr;
  }

  return &found->second;
}

inline std::optional<std::string> stringField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr || field->type != JsonValue::Type::String) {
    return std::nullopt;
  }
  return field->stringValue;
}

inline std::optional<bool> boolField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr || field->type != JsonValue::Type::Bool) {
    return std::nullopt;
  }
  return field->boolValue;
}

inline std::optional<int> intField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  if (field == nullptr || field->type != JsonValue::Type::Number) {
    return std::nullopt;
  }
  return static_cast<int>(field->numberValue);
}

inline bool isNullField(const JsonValue& value, const std::string& key) {
  const JsonValue* field = objectField(value, key);
  return field != nullptr && field->type == JsonValue::Type::Null;
}

inline std::vector<std::string> splitToken(const std::string& token) {
  std::vector<std::string> parts;
  size_t start = 0;

  while (true) {
    const size_t dot = token.find('.', start);

    if (dot == std::string::npos) {
      parts.push_back(token.substr(start));
      break;
    }

    parts.push_back(token.substr(start, dot - start));
    start = dot + 1;
  }

  return parts;
}

inline int base64Value(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '+' || ch == '-') {
    return 62;
  }
  if (ch == '/' || ch == '_') {
    return 63;
  }
  return -1;
}

inline std::vector<unsigned char> base64Decode(const std::string& input) {
  std::vector<unsigned char> output;
  int value = 0;
  int bits = -8;

  for (const char ch : input) {
    if (ch == '=') {
      break;
    }

    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }

    const int decoded = base64Value(ch);
    if (decoded < 0) {
      throw std::runtime_error("Invalid base64 input.");
    }

    value = (value << 6) + decoded;
    bits += 6;

    if (bits >= 0) {
      output.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
      bits -= 8;
    }
  }

  return output;
}

inline bool isHexPublicKey(const std::string& value) {
  if (value.size() != 64) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

inline std::array<unsigned char, 32> parseHexPublicKey(const std::string& value) {
  std::array<unsigned char, 32> key{};

  for (size_t index = 0; index < key.size(); ++index) {
    const std::string byte = value.substr(index * 2, 2);
    key[index] = static_cast<unsigned char>(std::stoul(byte, nullptr, 16));
  }

  return key;
}

inline std::string compactBase64(const std::string& value) {
  std::string output;
  output.reserve(value.size());

  for (const char ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
      output.push_back(ch);
    }
  }

  return output;
}

inline std::string publicKeyBase64Body(const std::string& normalized) {
  constexpr const char* kBeginMarker = "-----BEGIN PUBLIC KEY-----";
  constexpr const char* kEndMarker = "-----END PUBLIC KEY-----";

  const size_t begin = normalized.find(kBeginMarker);
  if (begin != std::string::npos) {
    const size_t bodyStart = begin + std::strlen(kBeginMarker);
    const size_t end = normalized.find(kEndMarker, bodyStart);
    if (end != std::string::npos) {
      return compactBase64(normalized.substr(bodyStart, end - bodyStart));
    }
  }

  std::string body;
  std::istringstream input(normalized);
  std::string line;

  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() ||
        line.find("-----BEGIN") == 0 ||
        line.find("-----END") == 0) {
      continue;
    }
    body += line;
  }

  return body.empty() ? normalized : body;
}

inline std::array<unsigned char, 32> parsePublicKey(const std::string& publicKeyPem) {
  std::string normalized = trim(replaceEscapedNewlines(publicKeyPem));

  if (normalized.empty()) {
    throw std::runtime_error("Public key is empty.");
  }

  if (isHexPublicKey(normalized)) {
    return parseHexPublicKey(normalized);
  }

  const std::string body = publicKeyBase64Body(normalized);
  const std::vector<unsigned char> der = base64Decode(body);

  if (der.size() == 32) {
    std::array<unsigned char, 32> raw{};
    std::copy(der.begin(), der.end(), raw.begin());
    return raw;
  }

  if (der.size() == kEd25519SpkiPrefix.size() + 32 &&
      std::equal(kEd25519SpkiPrefix.begin(), kEd25519SpkiPrefix.end(), der.begin())) {
    std::array<unsigned char, 32> raw{};
    std::copy(der.end() - 32, der.end(), raw.begin());
    return raw;
  }

  throw std::runtime_error("Public key is not an Ed25519 SPKI PEM or raw key.");
}

inline bool verifyEd25519Detached(
  const std::string& message,
  const std::vector<unsigned char>& signature,
  const std::array<unsigned char, 32>& publicKey
) {
  if (signature.size() != crypto_sign_BYTES) {
    return false;
  }

  std::vector<unsigned char> signedMessage(signature.size() + message.size());
  std::copy(signature.begin(), signature.end(), signedMessage.begin());
  std::copy(message.begin(), message.end(), signedMessage.begin() + signature.size());

  std::vector<unsigned char> opened(signedMessage.size());
  unsigned long long openedLength = 0;

  const int result = crypto_sign_open(
    opened.data(),
    &openedLength,
    signedMessage.data(),
    static_cast<unsigned long long>(signedMessage.size()),
    publicKey.data()
  );

  if (result != 0 || openedLength != message.size()) {
    return false;
  }

  return std::equal(
    opened.begin(),
    opened.begin() + static_cast<std::ptrdiff_t>(openedLength),
    message.begin()
  );
}

inline std::time_t timegmPortable(std::tm* time) {
#ifdef _WIN32
  return _mkgmtime(time);
#else
  return timegm(time);
#endif
}

inline std::tm gmtimePortable(std::time_t value) {
  std::tm result{};
#ifdef _WIN32
  gmtime_s(&result, &value);
#else
  gmtime_r(&value, &result);
#endif
  return result;
}

inline int parseDigits(const std::string& value, size_t position, size_t length) {
  if (position + length > value.size()) {
    throw std::runtime_error("Invalid ISO date.");
  }

  int parsed = 0;
  for (size_t index = 0; index < length; ++index) {
    const char ch = value[position + index];
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0) {
      throw std::runtime_error("Invalid ISO date.");
    }
    parsed = parsed * 10 + (ch - '0');
  }

  return parsed;
}

inline std::chrono::system_clock::time_point parseIso8601(const std::string& value) {
  if (value.size() < 20 ||
      value[4] != '-' ||
      value[7] != '-' ||
      (value[10] != 'T' && value[10] != 't') ||
      value[13] != ':' ||
      value[16] != ':') {
    throw std::runtime_error("Invalid ISO date.");
  }

  std::tm time{};
  time.tm_year = parseDigits(value, 0, 4) - 1900;
  time.tm_mon = parseDigits(value, 5, 2) - 1;
  time.tm_mday = parseDigits(value, 8, 2);
  time.tm_hour = parseDigits(value, 11, 2);
  time.tm_min = parseDigits(value, 14, 2);
  time.tm_sec = parseDigits(value, 17, 2);

  size_t position = 19;
  int milliseconds = 0;

  if (position < value.size() && value[position] == '.') {
    ++position;
    int scale = 100;
    while (position < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[position])) != 0) {
      if (scale > 0) {
        milliseconds += (value[position] - '0') * scale;
        scale /= 10;
      }
      ++position;
    }
  }

  if (position >= value.size() || value[position] != 'Z') {
    throw std::runtime_error("Only UTC ISO dates are supported.");
  }

  const std::time_t seconds = timegmPortable(&time);
  if (seconds == static_cast<std::time_t>(-1)) {
    throw std::runtime_error("Invalid ISO date.");
  }

  return std::chrono::system_clock::from_time_t(seconds) +
         std::chrono::milliseconds(milliseconds);
}

} // namespace otomarket::license::detail
