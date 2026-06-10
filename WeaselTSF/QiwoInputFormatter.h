#pragma once

#include <string>

class QiwoInputFormatter {
 public:
  static std::wstring FormatCommitText(
      const std::wstring& commit_text,
      bool auto_commit_spacing,
      const std::wstring& before_cursor = std::wstring(),
      const std::wstring& after_cursor = std::wstring());
};
