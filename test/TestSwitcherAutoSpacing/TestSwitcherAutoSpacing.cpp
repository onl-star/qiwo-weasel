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
  const auto build_bat = ReadFile(root / "build.bat");
  const auto build_qiwo_ps1 =
      ReadFile(root / "tools" / "build-qiwo.ps1");
  const auto gitmodules = ReadFile(root / ".gitmodules");

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
  RequireContains(edit_session, "GetTextBeforeComposition",
                  "TSF passes before-cursor context to formatter");
  RequireContains(edit_session, "GetTextAfterComposition",
                  "TSF passes after-cursor context to formatter");
  const auto deployer_configurator =
      ReadFile(root / "WeaselDeployer" / "Configurator.cpp");
  RequireContains(deployer_configurator, "switcher/hotkeys/@next: F4",
                  "Default custom YAML includes F4 switcher hotkey patch");
  RequireContains(deployer_configurator,
                  "switcher/save_options/@next: auto_commit_spacing",
                  "Default custom YAML persists auto spacing switcher option");
  RequireContains(deployer_configurator, "AppendYamlPatchEntries",
                  "Default custom YAML merges missing Qiwo patches");
  RequireNotContains(deployer_configurator, "file_size(file_path) > 0) {\n    return true;",
                     "Default custom YAML must not skip non-empty files");
  RequireContains(gitmodules, "path = rime-frost",
                  "rime-frost submodule path");
  RequireContains(gitmodules, "gaboolic/rime-frost.git",
                  "rime-frost submodule URL");
  RequireContains(build_bat,
                  R"(set QIWO_FROST_ROOT=%WEASEL_ROOT%\rime-frost)",
                  "Default batch frost root");
  RequireContains(build_bat, "submodule update --init rime-frost",
                  "Batch initializes rime-frost submodule");
  RequireContains(build_qiwo_ps1,
                  R"($WeaselRoot "rime-frost")",
                  "PowerShell frost root");
  RequireContains(build_qiwo_ps1, "submodule update --init rime-frost",
                  "PowerShell initializes rime-frost submodule");
  RequireNotContains(build_bat, "qiwo-ibusr\\rime-frost",
                     "Batch frost root must not depend on Linux repository");
  RequireNotContains(build_qiwo_ps1, "qiwo-ibusr/rime-frost",
                     "PowerShell frost root must not depend on Linux repository");

  return 0;
}
