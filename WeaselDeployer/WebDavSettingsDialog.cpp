#include "stdafx.h"
#include "WebDavSettingsDialog.h"

#include <WeaselUtility.h>
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
