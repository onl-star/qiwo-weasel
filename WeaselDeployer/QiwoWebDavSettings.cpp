#include "stdafx.h"
#include "QiwoWebDavSettings.h"

#include <WeaselUtility.h>
#include <cwctype>
#include <system_error>

namespace {

const wchar_t kSection[] = L"webdav";
const wchar_t kDefaultRemotePath[] = L"qiwo-rime-sync";

std::wstring Trim(std::wstring value) {
  size_t first = 0;
  while (first < value.size() && iswspace(value[first])) {
    ++first;
  }

  size_t last = value.size();
  while (last > first && iswspace(value[last - 1])) {
    --last;
  }

  return value.substr(first, last - first);
}

std::wstring ReadIniString(const std::filesystem::path& file,
                           const wchar_t* key,
                           const wchar_t* default_value = L"") {
  std::wstring buffer(4096, L'\0');
  DWORD length =
      GetPrivateProfileStringW(kSection, key, default_value, buffer.data(),
                               static_cast<DWORD>(buffer.size()), file.c_str());
  buffer.resize(length);
  return buffer;
}

bool WriteIniString(const std::filesystem::path& file,
                    const wchar_t* key,
                    const std::wstring& value) {
  return WritePrivateProfileStringW(kSection, key, value.c_str(),
                                    file.c_str()) != FALSE;
}

bool HasUrlScheme(const std::wstring& value) {
  return value.rfind(L"http://", 0) == 0 || value.rfind(L"https://", 0) == 0;
}

}  // namespace

std::filesystem::path QiwoWebDavSettingsFile() {
  return WeaselUserDataPath() / L".qiwo-sync" / L"webdav.ini";
}

QiwoWebDavSettings LoadQiwoWebDavSettings() {
  auto file = QiwoWebDavSettingsFile();
  QiwoWebDavSettings settings;
  settings.server_url = ReadIniString(file, L"server_url");
  settings.remote_path =
      ReadIniString(file, L"remote_path", kDefaultRemotePath);
  settings.username = ReadIniString(file, L"username");
  settings.password = ReadIniString(file, L"password");
  settings.device_id = ReadIniString(file, L"device_id");
  settings.auto_sync = ReadIniString(file, L"auto_sync", L"0") == L"1";
  settings.sync_interval_minutes =
      _wtoi(ReadIniString(file, L"sync_interval_minutes", L"60").c_str());
  if (settings.sync_interval_minutes < 1)
    settings.sync_interval_minutes = 60;
  return settings;
}

bool SaveQiwoWebDavSettings(const QiwoWebDavSettings& settings) {
  auto file = QiwoWebDavSettingsFile();
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  if (error) {
    return false;
  }

  bool ok = true;
  ok &= WriteIniString(file, L"server_url", Trim(settings.server_url));
  ok &= WriteIniString(file, L"remote_path", Trim(settings.remote_path));
  ok &= WriteIniString(file, L"username", Trim(settings.username));
  ok &= WriteIniString(file, L"password", settings.password);
  ok &= WriteIniString(file, L"device_id", Trim(settings.device_id));
  ok &= WriteIniString(file, L"auto_sync", settings.auto_sync ? L"1" : L"0");
  ok &= WriteIniString(file, L"sync_interval_minutes",
                       std::to_wstring(settings.sync_interval_minutes));
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
  return ok;
}

std::wstring BuildQiwoRemoteUrl(const QiwoWebDavSettings& settings) {
  auto server_url = Trim(settings.server_url);
  auto remote_path = Trim(settings.remote_path);

  if (server_url.empty()) {
    return {};
  }

  if (remote_path.empty()) {
    return server_url;
  }

  if (HasUrlScheme(remote_path)) {
    return remote_path;
  }

  while (!server_url.empty() &&
         (server_url.back() == L'/' || server_url.back() == L'\\')) {
    server_url.pop_back();
  }

  while (!remote_path.empty() &&
         (remote_path.front() == L'/' || remote_path.front() == L'\\')) {
    remote_path.erase(remote_path.begin());
  }

  if (remote_path.empty()) {
    return server_url;
  }

  return server_url + L"/" + remote_path;
}
