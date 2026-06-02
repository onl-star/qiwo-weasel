#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>

WeaselServerApp::WeaselServerApp()
    : m_handler(std::make_unique<RimeWithWeaselHandler>(&m_ui)),
      tray_icon(m_ui) {
  // m_handler.reset(new RimeWithWeaselHandler(&m_ui));
  m_server.SetRequestHandler(m_handler.get());
  SetupMenuHandlers();
}

WeaselServerApp::~WeaselServerApp() {
  StopAutoSync();
}

VOID CALLBACK WeaselServerApp::AutoSyncTimerProc(HWND hwnd,
                                                 UINT,
                                                 UINT_PTR id,
                                                 DWORD) {
  auto* self =
      reinterpret_cast<WeaselServerApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (self) {
    self->OnAutoSync();
  }
}

void WeaselServerApp::OnAutoSync() {
  std::filesystem::path dir = install_dir();
  execute(dir / L"WeaselDeployer.exe", std::wstring(L"/sync-user-dict"));
}

void WeaselServerApp::StartAutoSync() {
  StopAutoSync();

  // 直接读取 INI 设置（不依赖 WeaselDeployer 项目）
  auto ini_path = WeaselUserDataPath() / L".qiwo-sync" / L"webdav.ini";
  bool auto_sync =
      GetPrivateProfileIntW(L"webdav", L"auto_sync", 0, ini_path.c_str()) != 0;
  if (!auto_sync)
    return;

  int interval_minutes = GetPrivateProfileIntW(
      L"webdav", L"sync_interval_minutes", 60, ini_path.c_str());
  if (interval_minutes <= 0)
    return;

  UINT elapse = static_cast<UINT>(interval_minutes) * 60 * 1000;
  m_nAutoSyncTimerId =
      SetTimer(m_server.GetHWnd(), 1, elapse, AutoSyncTimerProc);
  SetWindowLongPtr(m_server.GetHWnd(), GWLP_USERDATA,
                   reinterpret_cast<LONG_PTR>(this));
}

void WeaselServerApp::StopAutoSync() {
  if (m_nAutoSyncTimerId != 0) {
    KillTimer(m_server.GetHWnd(), m_nAutoSyncTimerId);
    m_nAutoSyncTimerId = 0;
  }
}

int WeaselServerApp::Run() {
  if (!m_server.Start())
    return -1;

  // win_sparkle_set_appcast_url("http://localhost:8000/weasel/update/appcast.xml");
  win_sparkle_set_registry_path("Software\\Rime\\Weasel\\Updates");
  if (GetThreadUILanguage() ==
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
    win_sparkle_set_lang("zh-TW");
  else if (GetThreadUILanguage() ==
           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
    win_sparkle_set_lang("zh-CN");
  else
    win_sparkle_set_lang("en");
  win_sparkle_init();
  m_ui.Create(m_server.GetHWnd());

  m_handler->Initialize();
  m_handler->OnUpdateUI([this]() { tray_icon.Refresh(); });

  tray_icon.Create(m_server.GetHWnd());
  tray_icon.Refresh();

  StartAutoSync();

  int ret = m_server.Run();

  StopAutoSync();

  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();
  win_sparkle_cleanup();

  return ret;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_QUIT,
                          [this] { return m_server.Stop() == 0; });
  m_server.AddMenuHandler(ID_WEASELTRAY_DEPLOY,
                          std::bind(execute, dir / L"WeaselDeployer.exe",
                                    std::wstring(L"/deploy")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SETTINGS,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring()));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_DICT_MANAGEMENT,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/dict")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SYNC,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/sync")));
  m_server.AddMenuHandler(ID_WEASELTRAY_SYNC_SETTINGS,
                          std::bind(execute, dir / L"WeaselDeployer.exe",
                                    std::wstring(L"/sync-settings")));
  m_server.AddMenuHandler(ID_WEASELTRAY_WIKI,
                          std::bind(open, L"https://rime.im/docs/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_HOMEPAGE,
                          std::bind(open, L"https://rime.im/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_FORUM,
                          std::bind(open, L"https://rime.im/discuss/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_CHECKUPDATE, check_update);
  m_server.AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, std::bind(explore, dir));
  m_server.AddMenuHandler(ID_WEASELTRAY_USERCONFIG,
                          std::bind(explore, WeaselUserDataPath()));
  m_server.AddMenuHandler(ID_WEASELTRAY_LOGDIR,
                          std::bind(explore, WeaselLogPath()));
}
