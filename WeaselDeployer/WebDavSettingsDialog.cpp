#include "stdafx.h"
#include "WebDavSettingsDialog.h"

#include <WeaselUtility.h>
#include <filesystem>
#include <vector>

std::wstring WebDavSettingsDialog::GetText(int control_id) const {
  CWindow control = GetDlgItem(control_id);
  int length = control.GetWindowTextLength();
  std::vector<wchar_t> buffer(length + 1, L'\0');
  control.GetWindowText(buffer.data(), static_cast<int>(buffer.size()));
  return buffer.data();
}

void WebDavSettingsDialog::SetText(int control_id,
                                   const std::wstring& value) {
  SetDlgItemText(control_id, value.c_str());
}

LRESULT WebDavSettingsDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
  settings_ = LoadQiwoWebDavSettings();

  SetText(IDC_WEBDAV_SERVER_URL, settings_.server_url);
  SetText(IDC_WEBDAV_REMOTE_PATH, settings_.remote_path);
  SetText(IDC_WEBDAV_USERNAME, settings_.username);
  SetText(IDC_WEBDAV_PASSWORD, settings_.password);
  SetText(IDC_WEBDAV_DEVICE_ID, settings_.device_id);

  CenterWindow();
  BringWindowToTop();
  return TRUE;
}

LRESULT WebDavSettingsDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT WebDavSettingsDialog::OnSave(WORD, WORD, HWND, BOOL&) {
  settings_.server_url = GetText(IDC_WEBDAV_SERVER_URL);
  settings_.remote_path = GetText(IDC_WEBDAV_REMOTE_PATH);
  settings_.username = GetText(IDC_WEBDAV_USERNAME);
  settings_.password = GetText(IDC_WEBDAV_PASSWORD);
  settings_.device_id = GetText(IDC_WEBDAV_DEVICE_ID);

  if (BuildQiwoRemoteUrl(settings_).empty()) {
    ::MessageBoxW(m_hWnd, L"Please enter a WebDAV server URL.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
    return 0;
  }

  if (!SaveQiwoWebDavSettings(settings_)) {
    ::MessageBoxW(m_hWnd, L"Failed to save WebDAV settings.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
    return 0;
  }

  EndDialog(IDOK);
  return 0;
}

LRESULT WebDavSettingsDialog::OnCancel(WORD, WORD, HWND, BOOL&) {
  EndDialog(IDCANCEL);
  return 0;
}

LRESULT WebDavSettingsDialog::OnTestConnection(WORD, WORD, HWND, BOOL&) {
  QiwoWebDavSettings temp;
  temp.server_url = GetText(IDC_WEBDAV_SERVER_URL);
  temp.remote_path = GetText(IDC_WEBDAV_REMOTE_PATH);
  temp.username = GetText(IDC_WEBDAV_USERNAME);
  temp.password = GetText(IDC_WEBDAV_PASSWORD);

  std::wstring url = BuildQiwoRemoteUrl(temp);
  if (url.empty()) {
    ::MessageBoxW(m_hWnd, L"Please enter a WebDAV server URL.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
    return 0;
  }

  WCHAR exe_path[MAX_PATH] = {0};
  GetModuleFileNameW(NULL, exe_path, _countof(exe_path));
  std::filesystem::path curl_path =
      std::filesystem::path(exe_path).remove_filename() / L"curl.exe";

  if (!std::filesystem::exists(curl_path)) {
    ::MessageBoxW(m_hWnd, L"curl.exe not found beside WeaselDeployer.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
    return 0;
  }

  std::wstring cmd = L"--silent --fail --max-time 10 --basic";
  if (!temp.username.empty() || !temp.password.empty()) {
    cmd += L" --user \"";
    cmd += temp.username;
    cmd += L":";
    cmd += temp.password;
    cmd += L"\"";
  }
  cmd += L" -X PROPFIND -H \"Depth: 0\" -o NUL \"";
  cmd += url;
  cmd += L"\"";

  std::wstring cmd_line = L"\"" + curl_path.wstring() + L"\" " + cmd;

  STARTUPINFOW si = {0};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION pi = {0};
  std::vector<wchar_t> buffer(cmd_line.begin(), cmd_line.end());
  buffer.push_back(L'\0');

  BOOL ok = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  if (!ok) {
    ::MessageBoxW(m_hWnd, L"Failed to start curl.exe.",
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
    return 0;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (exit_code == 0) {
    ::MessageBoxW(m_hWnd, L"Connection successful.",
                  get_weasel_ime_name().c_str(), MB_ICONINFORMATION | MB_OK);
  } else {
    std::wstring msg = L"Connection failed.\n\ncurl exit code: ";
    msg += std::to_wstring(exit_code);
    ::MessageBoxW(m_hWnd, msg.c_str(),
                  get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
  }
  return 0;
}
