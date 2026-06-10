#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path FindRepoRoot() {
  auto current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "RimeWithWeasel" /
                                "RimeWithWeasel.cpp") &&
        std::filesystem::exists(current / "WeaselTSF" / "EditSession.cpp")) {
      return current;
    }
    const auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  throw std::runtime_error("could not find qiwo-weasel root");
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("could not open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void RequireContains(const std::string& haystack,
                     const std::string& needle,
                     const std::string& label) {
  if (haystack.find(needle) == std::string::npos) {
    throw std::runtime_error(label + " missing: " + needle);
  }
}

void RequireNotContains(const std::string& haystack,
                        const std::string& needle,
                        const std::string& label) {
  if (haystack.find(needle) != std::string::npos) {
    throw std::runtime_error(label + " should not contain: " + needle);
  }
}

}  // namespace

int main() {
  const auto root = FindRepoRoot();
  const auto rime_with_weasel =
      ReadFile(root / "RimeWithWeasel" / "RimeWithWeasel.cpp");

  RequireContains(rime_with_weasel, "kAutoCommitSpacingOption",
                  "Shared Rime option name");
  RequireContains(rime_with_weasel, "\"auto_commit_spacing\"",
                  "Rime option literal");
  RequireContains(rime_with_weasel, "\"var/option/auto_commit_spacing\"",
                  "Saved switcher option path");
  RequireContains(rime_with_weasel, "\"input/auto_commit_spacing\"",
                  "Legacy config fallback path");
  RequireContains(rime_with_weasel, "user_config_open",
                  "user.yaml should be opened through Rime user_config_open");
  RequireContains(rime_with_weasel, "_GetInitialAutoCommitSpacing",
                  "Initial option resolver");
  RequireContains(rime_with_weasel, "_GetLiveAutoCommitSpacing",
                  "Live option resolver");
  RequireContains(rime_with_weasel,
                  "rime_api->set_option(session_id, kAutoCommitSpacingOption",
                  "Session option seed");
  RequireContains(rime_with_weasel,
                  "rime_api->get_option(session_id, kAutoCommitSpacingOption)",
                  "Live session option read");
  RequireContains(rime_with_weasel,
                  "std::to_wstring((int)_GetLiveAutoCommitSpacing(session_id))",
                  "Config response uses live option");
  RequireNotContains(rime_with_weasel,
                     "std::to_wstring((int)_GetAutoCommitSpacing())",
                     "Static config response");

  const auto edit_session = ReadFile(root / "WeaselTSF" / "EditSession.cpp");
  RequireContains(edit_session, "config.auto_commit_spacing",
                  "TSF keeps config transport");

  return 0;
}
