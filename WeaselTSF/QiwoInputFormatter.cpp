#include "stdafx.h"
#include "QiwoInputFormatter.h"

#include <qiwo_input_format.h>

#include <cstring>

namespace {

bool WideToUtf8(const std::wstring& value, std::string* output) {
  output->clear();
  if (value.empty()) {
    return true;
  }
  int length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(),
                          static_cast<int>(value.size()), nullptr, 0, nullptr,
                          nullptr);
  if (length <= 0) {
    return false;
  }
  output->resize(length);
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(),
                             static_cast<int>(value.size()), output->data(),
                             length, nullptr, nullptr) == length;
}

bool Utf8ToWide(const char* value, std::wstring* output) {
  output->clear();
  if (!value || !*value) {
    return true;
  }
  int source_length = static_cast<int>(std::strlen(value));
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
                                   source_length, nullptr, 0);
  if (length <= 0) {
    return false;
  }
  output->resize(length);
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
                             source_length,
                             output->data(), length) == length;
}

}  // namespace

std::wstring QiwoInputFormatter::FormatCommitText(
    const std::wstring& commit_text,
    bool auto_commit_spacing,
    const std::wstring& before_cursor,
    const std::wstring& after_cursor) {
  if (commit_text.empty()) {
    return commit_text;
  }

  std::string commit_text_utf8;
  std::string before_cursor_utf8;
  std::string after_cursor_utf8;
  if (!WideToUtf8(commit_text, &commit_text_utf8) ||
      !WideToUtf8(before_cursor, &before_cursor_utf8) ||
      !WideToUtf8(after_cursor, &after_cursor_utf8)) {
    return commit_text;
  }

  QiwoInputFormatOptions options = {auto_commit_spacing};
  char* formatted = qiwo_input_format_commit_text(
      commit_text_utf8.c_str(),
      before_cursor.empty() ? nullptr : before_cursor_utf8.c_str(),
      after_cursor.empty() ? nullptr : after_cursor_utf8.c_str(), options);
  if (!formatted) {
    return commit_text;
  }

  std::wstring result;
  bool converted = Utf8ToWide(formatted, &result);
  qiwo_input_format_free_string(formatted);
  return converted ? result : commit_text;
}
