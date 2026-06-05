#include "stdafx.h"
#include "QiwoInstallationHelper.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

std::string QiwoInstallationHelper::SyncDirName() {
  return "sync";
}

std::string QiwoInstallationHelper::MakeSafeId(const std::string& device_id) {
  std::string result = device_id;
  for (auto& ch : result) {
    if (ch == ' ' || ch == ':' || ch == '\\' || ch == '/') {
      ch = '-';
    }
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return result.empty() ? "unknown" : result;
}

std::string QiwoInstallationHelper::SyncDirForYaml(
    const std::string& rime_user_dir) {
  auto path = (fs::path(rime_user_dir) / SyncDirName()).u8string();
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

void QiwoInstallationHelper::Ensure(const std::string& rime_user_dir,
                                    const std::string& device_id) {
  auto file = fs::path(rime_user_dir) / "installation.yaml";
  auto safe_id = MakeSafeId(device_id);
  auto sync_dir = SyncDirForYaml(rime_user_dir);

  if (fs::exists(file)) {
    std::ifstream in(file);
    if (!in.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    in.close();

    // 提取旧的 installation_id
    std::string old_id;
    std::regex id_regex(R"(^\s*installation_id:\s*(?:"([^"]*)"|([^\r\n]*))\s*$)",
                        std::regex_constants::multiline);
    std::smatch match;
    if (std::regex_search(content, match, id_regex)) {
      old_id = match[1].matched ? match[1].str() : match[2].str();
    }

    std::regex sync_regex(R"(^\s*sync_dir:\s*(?:"[^"]*"|[^\r\n]*)\s*$)",
                          std::regex_constants::multiline);

    // 替换或添加 installation_id
    if (content.find("installation_id:") != std::string::npos) {
      content = std::regex_replace(content, id_regex,
                                   "installation_id: \"" + safe_id + "\"");
    } else {
      if (!content.empty() && content.back() != '\n') content += '\n';
      content += "installation_id: \"" + safe_id + "\"\n";
    }

    if (content.find("sync_dir:") != std::string::npos) {
      content = std::regex_replace(content, sync_regex,
                                   "sync_dir: \"" + sync_dir + "\"");
    } else {
      if (!content.empty() && content.back() != '\n') content += '\n';
      content += "sync_dir: \"" + sync_dir + "\"\n";
    }

    {
      std::ofstream out(file, std::ios::trunc);
      if (out.is_open()) {
        out << content;
      }
    }

    // 迁移旧的 sync 数据
    if (!old_id.empty() && old_id != safe_id) {
      MigrateSyncData(rime_user_dir, old_id, safe_id);
    }
    auto export_dir = fs::path(rime_user_dir) / SyncDirName() / safe_id;
    std::error_code ec;
    fs::create_directories(export_dir, ec);
    return;
  }

  // 新建
  std::ofstream out(file);
  if (out.is_open()) {
    out << "distribution: \"Qiwo\"\n"
        << "distribution_version: \"1.0\"\n"
        << "installation_id: \"" << safe_id << "\"\n"
        << "sync_dir: \"" << sync_dir << "\"\n";
  }

  // 确保 sync 导出目录存在
  auto export_dir = fs::path(rime_user_dir) / SyncDirName() / safe_id;
  std::error_code ec;
  fs::create_directories(export_dir, ec);
}

void QiwoInstallationHelper::MigrateSyncData(const std::string& rime_user_dir,
                                             const std::string& old_id,
                                             const std::string& new_id) {
  auto sync_dir = fs::path(rime_user_dir) / SyncDirName();
  auto old_dir = sync_dir / old_id;
  auto new_dir = sync_dir / new_id;

  if (!fs::exists(old_dir)) return;

  std::error_code ec;
  fs::create_directories(new_dir, ec);

  for (const auto& entry : fs::directory_iterator(old_dir, ec)) {
    if (ec) break;
    auto dest = new_dir / entry.path().filename();
    fs::rename(entry.path(), dest, ec);
  }

  // 删除旧的空目录
  fs::remove(old_dir, ec);
}
