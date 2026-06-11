#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path FindRepoRoot() {
  auto current = std::filesystem::current_path();
  while (!current.empty()) {
    if (std::filesystem::exists(current / "WeaselTSF" / "EditSession.cpp") &&
        std::filesystem::exists(current / "include" / "WeaselIPCData.h")) {
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
    throw std::runtime_error(label + " must not contain: " + needle);
  }
}

}  // namespace

int main() {
  const auto root = FindRepoRoot();

  const auto ipc_data = ReadFile(root / "include" / "WeaselIPCData.h");
  RequireContains(ipc_data, "auto_commit_spacing", "Config transport field");
  RequireContains(ipc_data, "auto_commit_spacing(true)", "Config default");

  const auto configurator = ReadFile(root / "WeaselIPC" / "Configurator.cpp");
  RequireContains(configurator, "auto_commit_spacing", "Configurator parser");

  const auto rime_with_weasel =
      ReadFile(root / "RimeWithWeasel" / "RimeWithWeasel.cpp");
  RequireContains(rime_with_weasel, "config.auto_commit_spacing",
                  "Frontend config response");
  RequireContains(rime_with_weasel, "\"input/auto_commit_spacing\"",
                  "Rime config key");

  const auto formatter_h = ReadFile(root / "WeaselTSF" / "QiwoInputFormatter.h");
  RequireContains(formatter_h, "class QiwoInputFormatter", "Wrapper class");
  RequireContains(formatter_h, "FormatCommitText", "Wrapper method");

  const auto formatter_cpp =
      ReadFile(root / "WeaselTSF" / "QiwoInputFormatter.cpp");
  RequireContains(formatter_cpp, "qiwo_input_format_commit_text",
                  "C ABI call");
  RequireContains(formatter_cpp, "qiwo_input_format_free_string",
                  "C ABI free");
  RequireContains(formatter_cpp, "WideCharToMultiByte",
                  "UTF-16 to UTF-8 conversion");
  RequireContains(formatter_cpp, "MultiByteToWideChar",
                  "UTF-8 to UTF-16 conversion");

  const auto edit_session = ReadFile(root / "WeaselTSF" / "EditSession.cpp");
  RequireContains(edit_session, "QiwoInputFormatter", "Commit hook wrapper");
  RequireContains(edit_session, "config.auto_commit_spacing",
                  "Commit hook setting");
  RequireContains(edit_session, "FormatCommitText(commit",
                  "Commit formatting before insertion");
  RequireContains(edit_session, "_InsertText(_pEditSessionContext, commit)",
                  "Commit insertion remains centralized");

  const auto vcxproj = ReadFile(root / "WeaselTSF" / "WeaselTSF.vcxproj");
  RequireContains(vcxproj, "QiwoInputFormatter.cpp", "Project source entry");
  RequireContains(vcxproj, "QiwoInputFormatter.h", "Project header entry");
  RequireContains(vcxproj, "qiwo_input_format.lib", "Native library link");
  RequireContains(vcxproj, "Ws2_32.lib", "Rust staticlib winsock link");
  RequireContains(vcxproj, "Userenv.lib", "Rust staticlib userenv link");
  RequireContains(vcxproj, "Ntdll.lib", "Rust staticlib ntdll link");
  RequireContains(vcxproj, "Secur32.lib", "Rust staticlib secur32 link");
  RequireContains(vcxproj, "Bcrypt.lib", "Rust staticlib bcrypt link");

  const auto build_bat = ReadFile(root / "build.bat");
  RequireContains(build_bat, ":build_qiwo_input_format",
                  "Input format build routine");
  RequireContains(build_bat, "qiwo-input-format-core",
                  "Input format core discovery");

  const auto ci_workflow = ReadFile(root / ".github" / "workflows" / "ci.yml");
  RequireContains(ci_workflow, "aarch64-pc-windows-msvc",
                  "CI installs Windows ARM64 Rust target");
  RequireNotContains(ci_workflow, "thumbv7a-pc-windows-msvc",
                     "CI Rust target list");
  RequireNotContains(build_bat, "thumbv7a-pc-windows-msvc",
                     "Input format build target list");

  const auto gitmodules = ReadFile(root / ".gitmodules");
  RequireContains(gitmodules, "path = qiwo-input-format-core",
                  "Input format core submodule path");
  RequireContains(gitmodules,
                  "url = https://github.com/onl-star/qiwo-input-format-core.git",
                  "Input format core repository URL");

  return 0;
}
