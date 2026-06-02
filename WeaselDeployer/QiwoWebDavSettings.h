#pragma once

#include <filesystem>
#include <string>

struct QiwoWebDavSettings {
  std::wstring server_url;
  std::wstring remote_path;
  std::wstring username;
  std::wstring password;
  std::wstring device_id;
  bool auto_sync = false;
  int sync_interval_minutes = 60;
};

std::filesystem::path QiwoWebDavSettingsFile();
QiwoWebDavSettings LoadQiwoWebDavSettings();
bool SaveQiwoWebDavSettings(const QiwoWebDavSettings& settings);
std::wstring BuildQiwoRemoteUrl(const QiwoWebDavSettings& settings);
