#include "stdafx.h"
#include "QiwoInstallationHelper.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

bool IsSpace(char ch) {
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::string Trim(std::string value) {
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(),
                           [](char ch) { return !IsSpace(ch); }));
  value.erase(
      std::find_if(value.rbegin(), value.rend(),
                   [](char ch) { return !IsSpace(ch); })
          .base(),
      value.end());
  return value;
}

std::string EscapeYamlDoubleQuoted(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string ParseYamlScalar(const std::string& line, size_t colon_pos) {
  auto value = Trim(line.substr(colon_pos + 1));
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool UpsertYamlString(std::string& content,
                      const std::string& key,
                      const std::string& value,
                      std::string* previous_value = nullptr) {
  std::string updated;
  updated.reserve(content.size() + key.size() + value.size() + 8);
  bool found = false;

  size_t pos = 0;
  while (pos < content.size()) {
    auto next = content.find('\n', pos);
    auto line_end = next == std::string::npos ? content.size() : next;
    auto eol = next == std::string::npos ? "" : "\n";
    auto line = content.substr(pos, line_end - pos);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
      eol = "\r\n";
    }

    auto key_start = line.find_first_not_of(" \t");
    if (key_start != std::string::npos &&
        line.compare(key_start, key.size(), key) == 0 &&
        key_start + key.size() < line.size() &&
        line[key_start + key.size()] == ':') {
      found = true;
      if (previous_value && previous_value->empty()) {
        *previous_value = ParseYamlScalar(line, key_start + key.size());
      }
      updated += line.substr(0, key_start);
      updated += key;
      updated += ": \"";
      updated += EscapeYamlDoubleQuoted(value);
      updated += "\"";
      updated += eol;
    } else {
      updated += line;
      updated += eol;
    }

    if (next == std::string::npos) break;
    pos = next + 1;
  }

  if (!found) {
    if (!updated.empty() && updated.back() != '\n') updated += '\n';
    updated += key;
    updated += ": \"";
    updated += EscapeYamlDoubleQuoted(value);
    updated += "\"\n";
  }

  content.swap(updated);
  return found;
}

}  // namespace

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

    std::string old_id;
    UpsertYamlString(content, "installation_id", safe_id, &old_id);
    UpsertYamlString(content, "sync_dir", sync_dir);

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
