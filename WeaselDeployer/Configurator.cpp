#include "stdafx.h"
#include "WeaselDeployer.h"
#include "Configurator.h"
#include "SwitcherSettingsDialog.h"
#include "UIStyleSettings.h"
#include "UIStyleSettingsDialog.h"
#include "DictManagementDialog.h"
#include "QiwoWebDavSettings.h"
#include "QiwoInstallationHelper.h"
#include "WebDavSettingsDialog.h"
#include <WeaselConstants.h>
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include <WeaselUtility.h>
#pragma warning(disable : 4005)
#include <rime_api.h>
#include <rime_levers_api.h>
#pragma warning(default : 4005)
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include "WeaselDeployer.h"

namespace {

static std::filesystem::path InstallDir() {
  WCHAR exe_path[MAX_PATH] = {0};
  GetModuleFileNameW(GetModuleHandle(NULL), exe_path, _countof(exe_path));
  return std::filesystem::path(exe_path).remove_filename();
}

static std::filesystem::path QiwoSyncToolPath() {
  auto dir = InstallDir();
  auto bundled = dir / L"qiwo-sync" / L"qiwo-rime-sync.exe";
  if (std::filesystem::exists(bundled)) {
    return bundled;
  }
  return dir / L"qiwo-rime-sync.exe";
}

static std::wstring GetEnvVar(const wchar_t* name) {
  DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
  if (len == 0) {
    return {};
  }

  std::wstring value(len, L'\0');
  DWORD written = GetEnvironmentVariableW(name, value.data(), len);
  if (written == 0) {
    return {};
  }

  value.resize(written);
  return value;
}

static std::wstring Hostname() {
  WCHAR buffer[MAX_COMPUTERNAME_LENGTH + 1] = {0};
  DWORD size = _countof(buffer);
  if (GetComputerNameW(buffer, &size) && size > 0) {
    return std::wstring(buffer, size);
  }
  return L"unknown";
}

static std::wstring ResolveQiwoDeviceId(
    const std::wstring& configured_device_id = std::wstring()) {
  auto device_id = GetEnvVar(L"QIWO_DEVICE_ID");
  if (device_id.empty()) {
    device_id = configured_device_id;
  }
  if (device_id.empty()) {
    device_id = Hostname();
  }
  return device_id;
}

static std::wstring QuoteArg(const std::wstring& value) {
  if (value.empty()) {
    return L"\"\"";
  }

  std::wstring quoted = L"\"";
  size_t backslashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') {
      ++backslashes;
    } else if (ch == L'"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted += ch;
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      backslashes = 0;
      quoted += ch;
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted += L"\"";
  return quoted;
}

struct ProcessResult {
  int exit_code = 1;
  std::wstring output;
};

static std::wstring ProcessOutputToWide(const std::string& output) {
  if (output.empty()) {
    return {};
  }
  auto wide = string_to_wstring(output, CP_UTF8);
  if (wide.empty()) {
    wide = string_to_wstring(output);
  }
  return wide;
}

static ProcessResult RunProcess(const std::filesystem::path& exe,
                                const std::wstring& args) {
  std::wstring command_line = QuoteArg(exe.wstring());
  if (!args.empty()) {
    command_line += L" ";
    command_line += args;
  }

  SECURITY_ATTRIBUTES sa = {0};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
    return {(int)GetLastError(), L""};
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si = {0};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = write_pipe;
  si.hStdError = write_pipe;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi = {0};
  std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
  buffer.push_back(L'\0');

  BOOL ok = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(write_pipe);
  if (!ok) {
    auto error = GetLastError();
    CloseHandle(read_pipe);
    return {(int)error, L""};
  }

  std::string output;
  char chunk[4096];
  for (;;) {
    DWORD available = 0;
    while (PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) &&
           available > 0) {
      DWORD bytes_read = 0;
      if (!ReadFile(read_pipe, chunk, sizeof(chunk), &bytes_read, nullptr) ||
          bytes_read == 0) {
        break;
      }
      output.append(chunk, chunk + bytes_read);
      available = 0;
    }

    if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
      break;
    }
  }

  DWORD bytes_read = 0;
  while (ReadFile(read_pipe, chunk, sizeof(chunk), &bytes_read, nullptr) &&
         bytes_read > 0) {
    output.append(chunk, chunk + bytes_read);
  }

  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(read_pipe);
  return {(int)exit_code, ProcessOutputToWide(output)};
}

static std::wstring BuildSyncFailureMessage(const ProcessResult& result) {
  std::wstring message = L"Qiwo WebDAV sync failed.\n\nExit code: ";
  message += std::to_wstring(result.exit_code);
  if (!result.output.empty()) {
    message += L"\n\n";
    auto output = result.output;
    if (output.size() > 1800) {
      output = output.substr(0, 1800);
      output += L"\n...";
    }
    message += output;
  }
  return message;
}

static std::wstring CommonQiwoArgs(
    const std::wstring& configured_device_id = std::wstring()) {
  std::wstring args = L"--frontend weasel --rime-user-dir ";
  args += QuoteArg(WeaselUserDataPath().wstring());

  args += L" --device-id ";
  args += QuoteArg(ResolveQiwoDeviceId(configured_device_id));

  return args;
}

static bool EnsureDefaultCustomYaml() {
  std::filesystem::path file_path =
      WeaselUserDataPath() / L"default.custom.yaml";
  if (std::filesystem::exists(file_path) &&
      std::filesystem::file_size(file_path) > 0) {
    return true;
  }

  std::ofstream out(file_path, std::ios::trunc);
  if (!out) {
    return false;
  }

  out << "patch:\n"
      << "  schema_list:\n"
      << "    - schema: rime_frost\n"
      << "  switcher/hotkeys/@next: F4\n"
      << "  switcher/save_options/@next: auto_commit_spacing\n";
  return true;
}

static void CreateFileIfNotExist(std::string filename) {
  std::filesystem::path file_path = WeaselUserDataPath() / u8tow(filename);
  DWORD dwAttrib = GetFileAttributes(file_path.c_str());
  if (!(INVALID_FILE_ATTRIBUTES != dwAttrib &&
        0 == (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))) {
    std::wofstream o(file_path.c_str(), std::ios::app);
    o.close();
  }
}

}  // namespace

Configurator::Configurator() {
  EnsureDefaultCustomYaml();
  CreateFileIfNotExist("weasel.custom.yaml");
}

void Configurator::Initialize() {
  RIME_STRUCT(RimeTraits, weasel_traits);
  std::string shared_dir = wtou8(WeaselSharedDataPath().wstring());
  std::string user_dir = wtou8(WeaselUserDataPath().wstring());
  weasel_traits.shared_data_dir = shared_dir.c_str();
  weasel_traits.user_data_dir = user_dir.c_str();
  weasel_traits.prebuilt_data_dir = weasel_traits.shared_data_dir;
  std::string distribution_name = wtou8(get_weasel_ime_name());
  weasel_traits.distribution_name = distribution_name.c_str();
  weasel_traits.distribution_code_name = WEASEL_CODE_NAME;
  weasel_traits.distribution_version = WEASEL_VERSION;
  weasel_traits.app_name = "rime.weasel";
  std::string log_dir = WeaselLogPath().u8string();
  weasel_traits.log_dir = log_dir.c_str();
  RimeApi* rime_api = rime_get_api();
  assert(rime_api);
  rime_api->setup(&weasel_traits);
  LOG(INFO) << "WeaselDeployer reporting.";
  rime_api->deployer_initialize(NULL);
}

static bool configure_switcher(RimeLeversApi* api,
                               RimeSwitcherSettings* switchcer_settings,
                               bool* reconfigured) {
  RimeCustomSettings* settings = (RimeCustomSettings*)switchcer_settings;
  if (!api->load_settings(settings))
    return false;
  SwitcherSettingsDialog dialog(switchcer_settings);
  if (dialog.DoModal() == IDOK) {
    if (api->save_settings(settings))
      *reconfigured = true;
    return true;
  }
  return false;
}

static bool configure_ui(RimeLeversApi* api,
                         UIStyleSettings* ui_style_settings,
                         bool* reconfigured) {
  RimeCustomSettings* settings = ui_style_settings->settings();
  if (!api->load_settings(settings))
    return false;
  UIStyleSettingsDialog dialog(ui_style_settings);
  if (dialog.DoModal() == IDOK) {
    if (api->save_settings(settings))
      *reconfigured = true;
    return true;
  }
  return false;
}

int Configurator::Run(bool installing) {
  if (installing) {
    InitFrost();
  }

  RimeModule* levers = rime_get_api()->find_module("levers");
  if (!levers)
    return 1;
  RimeLeversApi* api = (RimeLeversApi*)levers->get_api();
  if (!api)
    return 1;

  bool reconfigured = false;

  RimeSwitcherSettings* switcher_settings = api->switcher_settings_init();
  UIStyleSettings ui_style_settings;

  bool skip_switcher_settings =
      installing && !api->is_first_run((RimeCustomSettings*)switcher_settings);
  bool skip_ui_style_settings =
      installing && !api->is_first_run(ui_style_settings.settings());

  (skip_switcher_settings ||
   configure_switcher(api, switcher_settings, &reconfigured)) &&
      (skip_ui_style_settings ||
       configure_ui(api, &ui_style_settings, &reconfigured));

  api->custom_settings_destroy((RimeCustomSettings*)switcher_settings);

  if (installing || reconfigured) {
    return UpdateWorkspace(reconfigured);
  }
  return 0;
}

int Configurator::UpdateWorkspace(bool report_errors) {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    if (report_errors) {
      // MessageBox(NULL,
      // L"正在執行另一項部署任務，方纔所做的修改將在輸入法再次啓動後生效。",
      // L"【小狼毫】", MB_OK | MB_ICONINFORMATION);
      MSG_BY_IDS(IDS_STR_DEPLOYING_RESTARTREQ, IDS_STR_WEASEL,
                 MB_OK | MB_ICONINFORMATION);
    }
    return 1;
  }

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
  }

  {
    RimeApi* rime = rime_get_api();
    // initialize default config, preset schemas
    rime->deploy();
    // initialize weasel config
    rime->deploy_config_file("weasel.yaml", "config_version");
  }

  CloseHandle(hMutex);  // should be closed before resuming service.

  if (client.Connect()) {
    LOG(INFO) << "Resuming service.";
    client.EndMaintenance();
  }
  return 0;
}

int Configurator::DictManagement() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
  }

  {
    RimeApi* rime = rime_get_api();
    if (RIME_API_AVAILABLE(rime, run_task)) {
      rime->run_task("installation_update");  // setup user data sync dir
    }
    DictManagementDialog dlg;
    dlg.DoModal();
  }

  CloseHandle(hMutex);  // should be closed before resuming service.

  if (client.Connect()) {
    LOG(INFO) << "Resuming service.";
    client.EndMaintenance();
  }
  return 0;
}

int Configurator::SyncUserData() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!hMutex) {
    LOG(ERROR) << "Error creating WeaselDeployerMutex.";
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(WARNING) << "another deployer process is running; aborting operation.";
    CloseHandle(hMutex);
    // MessageBox(NULL, L"正在執行另一項部署任務，請稍候再試。", L"【小狼毫】",
    // MB_OK | MB_ICONINFORMATION);
    MSG_BY_IDS(IDS_STR_DEPLOYING_WAIT, IDS_STR_WEASEL,
               MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
  }

  int result = 0;
  {
    auto sync_tool = QiwoSyncToolPath();
    auto settings = LoadQiwoWebDavSettings();
    auto remote_url = GetEnvVar(L"QIWO_WEBDAV_URL");
    if (remote_url.empty()) {
      remote_url = BuildQiwoRemoteUrl(settings);
    }

    if (!std::filesystem::exists(sync_tool)) {
      MessageBoxW(NULL,
                  L"qiwo-rime-sync.exe was not found beside WeaselDeployer.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
      result = 1;
    } else if (remote_url.empty()) {
      MessageBoxW(NULL,
                  L"WebDAV settings are not configured. Open WebDAV sync "
                  L"settings or set QIWO_WEBDAV_URL.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
      result = 1;
    } else {
      // 确保 installation.yaml 配置了 sync_dir 和 installation_id
      auto user_data_dir = WeaselUserDataPath();
      auto device_id = ResolveQiwoDeviceId(settings.device_id);
      QiwoInstallationHelper::Ensure(user_data_dir.u8string(),
                                     wtou8(device_id));

      // 先导出用户词库
      RimeApi* rime = rime_get_api();
      rime->sync_user_data();

      std::wstring args = L"sync ";
      args += CommonQiwoArgs(device_id);
      args += L" --remote-url ";
      args += QuoteArg(remote_url);

      auto username = GetEnvVar(L"QIWO_WEBDAV_USERNAME");
      if (username.empty()) {
        username = settings.username;
      }
      if (!username.empty()) {
        args += L" --username ";
        args += QuoteArg(username);
      }

      auto password = GetEnvVar(L"QIWO_WEBDAV_PASSWORD");
      if (!password.empty()) {
        args += L" --password-env QIWO_WEBDAV_PASSWORD";
      } else if (!settings.password.empty()) {
        args += L" --password ";
        args += QuoteArg(settings.password);
      }

      auto process_result = RunProcess(sync_tool, args);
      result = process_result.exit_code;
      if (result != 0) {
        LOG(ERROR) << "Qiwo WebDAV sync failed: " << result;
        if (!process_result.output.empty()) {
          LOG(ERROR) << wtou8(process_result.output);
        }
        auto message = BuildSyncFailureMessage(process_result);
        MessageBoxW(NULL, message.c_str(),
                    get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
      } else {
        // 导入合并用户词库
        rime->sync_user_data();
        rime->deploy();
        rime->deploy_config_file("weasel.yaml", "config_version");
      }
    }
  }

  CloseHandle(hMutex);  // should be closed before resuming service.

  if (client.Connect()) {
    LOG(INFO) << "Resuming service.";
    client.EndMaintenance();
  }
  return result;
}

int Configurator::SyncUserDict() {
  auto sync_tool = QiwoSyncToolPath();
  auto settings = LoadQiwoWebDavSettings();
  auto device_id = ResolveQiwoDeviceId(settings.device_id);
  auto user_data_dir = WeaselUserDataPath();
  QiwoInstallationHelper::Ensure(user_data_dir.u8string(), wtou8(device_id));

  // 先导出词库
  RimeApi* rime = rime_get_api();
  rime->sync_user_data();

  // 使用 sync-user-dict 模式（仅同步 sync/ 目录）
  auto remote_url = GetEnvVar(L"QIWO_WEBDAV_URL");
  if (remote_url.empty()) {
    remote_url = BuildQiwoRemoteUrl(settings);
  }

  if (!std::filesystem::exists(sync_tool)) {
    LOG(ERROR) << "qiwo-rime-sync.exe not found.";
    return 1;
  }
  if (remote_url.empty()) {
    LOG(ERROR) << "WebDAV URL not configured.";
    return 1;
  }

  std::wstring args = L"sync-user-dict ";
  args += CommonQiwoArgs(device_id);
  args += L" --remote-url ";
  args += QuoteArg(remote_url);

  auto username = GetEnvVar(L"QIWO_WEBDAV_USERNAME");
  if (username.empty())
    username = settings.username;
  if (!username.empty()) {
    args += L" --username ";
    args += QuoteArg(username);
  }

  auto password = GetEnvVar(L"QIWO_WEBDAV_PASSWORD");
  if (!password.empty()) {
    args += L" --password-env QIWO_WEBDAV_PASSWORD";
  } else if (!settings.password.empty()) {
    args += L" --password ";
    args += QuoteArg(settings.password);
  }

  auto process_result = RunProcess(sync_tool, args);
  int result = process_result.exit_code;
  if (result == 0) {
    // 导入词库
    rime->sync_user_data();
    rime->deploy();
    rime->deploy_config_file("weasel.yaml", "config_version");
  } else {
    LOG(ERROR) << "Qiwo user dict WebDAV sync failed: " << result;
    if (!process_result.output.empty()) {
      LOG(ERROR) << wtou8(process_result.output);
    }
  }
  return result;
}

int Configurator::SyncSettings() {
  WebDavSettingsDialog dlg;
  INT_PTR result = dlg.DoModal();
  if (result == -1) {
    MessageBoxW(NULL, L"Failed to create WebDAV settings dialog.",
                get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
  }
  return 0;
}

int Configurator::InitFrost() {
  EnsureDefaultCustomYaml();

  auto sync_tool = QiwoSyncToolPath();
  if (!std::filesystem::exists(sync_tool)) {
    LOG(WARNING) << "qiwo-rime-sync.exe not found; skipping rime-frost init.";
    return 0;
  }

  auto frost_dir = WeaselSharedDataPath() / L"rime-frost";
  if (!std::filesystem::exists(frost_dir / L"rime_frost.schema.yaml")) {
    LOG(WARNING) << "rime-frost bundle not found; skipping rime-frost init.";
    return 0;
  }

  std::wstring args = L"init-frost ";
  args += CommonQiwoArgs();
  args += L" --frost-dir ";
  args += QuoteArg(frost_dir.wstring());

  auto process_result = RunProcess(sync_tool, args);
  int ret = process_result.exit_code;
  if (ret != 0) {
    LOG(ERROR) << "rime-frost init failed: " << ret;
    if (!process_result.output.empty()) {
      LOG(ERROR) << wtou8(process_result.output);
    }
  }
  return ret;
}
