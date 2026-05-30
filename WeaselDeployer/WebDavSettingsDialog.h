#pragma once

#include "QiwoWebDavSettings.h"
#include "resource.h"

class WebDavSettingsDialog : public CDialogImpl<WebDavSettingsDialog> {
 public:
  enum { IDD = IDD_WEBDAV_SETTINGS };

 protected:
  BEGIN_MSG_MAP(WebDavSettingsDialog)
  MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  COMMAND_ID_HANDLER(IDOK, OnSave)
  COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnSave(WORD, WORD, HWND, BOOL&);
  LRESULT OnCancel(WORD, WORD, HWND, BOOL&);

 private:
  std::wstring GetText(int control_id) const;
  void SetText(int control_id, const std::wstring& value);

  QiwoWebDavSettings settings_;
};
