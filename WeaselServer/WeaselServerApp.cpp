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

WeaselServerApp::~WeaselServerApp() {}

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
  m_handler->OnUpdateUI([this]() { tray_icon.RequestRefresh(); });

  m_keyboard.Create(m_server.GetHWnd());
  m_keyboard.SetKeyHandler([this](weasel::KeyEvent const& key_event) {
    BOOL handled = FALSE;
    bool flush = false;
    m_server.InvokeHandlerAction([&]() {
      handled = m_handler->ProcessKeyEventFromKeyboard(key_event);
      flush = m_handler->HasDeferredCommit();
    });
    if (flush)
      WeaselKeyboard::FlushToClient();
    return handled != FALSE;
  });
  m_keyboard.SetSelectHandler([this](size_t index) {
    m_server.InvokeHandlerAction(
        [&]() { m_handler->SelectCandidateOnCurrentPage(index, 0); });
    WeaselKeyboard::FlushToClient();
  });
  m_keyboard.SetModeToggleHandler([this](bool ascii_mode) {
    m_server.InvokeHandlerAction(
        [&]() { m_handler->SetOption(0, "ascii_mode", !ascii_mode); });
  });
  m_handler->SetKeyboardUpdateCallback([this](WeaselSessionId session_id,
                                              const weasel::Context& ctx,
                                              const weasel::Status& status) {
    m_keyboard.OnEngineUpdate(session_id, ctx, status);
  });

  // The tool panel shares the keyboard's engine entry point (edit keys may
  // need candidate navigation while composing).
  m_tool_panel.Create(m_server.GetHWnd());
  m_tool_panel.SetAnchorWindow(m_keyboard.Hwnd());
  m_tool_panel.SetKeyHandler([this](weasel::KeyEvent const& key_event) {
    BOOL handled = FALSE;
    m_server.InvokeHandlerAction(
        [&]() { handled = m_handler->ProcessKeyEventFromKeyboard(key_event); });
    return handled != FALSE;
  });

  m_keyboard.SetToolHandler([this](int tool) {
    switch (tool) {
      case WeaselKeyboard::TOOL_CLIPBOARD:
        m_tool_panel.Toggle(WeaselToolPanel::PAGE_CLIPBOARD);
        break;
      case WeaselKeyboard::TOOL_EDIT:
        m_tool_panel.Toggle(WeaselToolPanel::PAGE_EDIT);
        break;
      case WeaselKeyboard::TOOL_VOICE:
        m_keyboard.ShowHint(L"语音输入开发中，即将上线");
        break;
      default:
        break;
    }
  });
  m_keyboard.SetDismissHandler([this]() {
    m_keyboard_suppressed = true;
    m_tool_panel.Hide();
    m_keyboard.Hide();
  });

  // Phone-style auto show/hide: the keyboard appears when a text field gains
  // focus and hides when focus leaves (tapping the desktop, switching apps).
  m_handler->SetFocusCallback([this](bool focused) {
    if (focused) {
      if (!m_keyboard_suppressed)
        m_keyboard.Show();
    } else {
      m_keyboard_suppressed = false;
      m_tool_panel.Hide();
      m_keyboard.Hide();
    }
  });

  tray_icon.Create(m_server.GetHWnd());
  tray_icon.SetSoftKeyboardVisibleQuery(
      [this]() { return m_keyboard.IsVisible(); });
  m_server.SetTrayRefreshCallback([this]() { tray_icon.ApplyRefresh(); });
  tray_icon.RequestRefresh();

  int ret = m_server.Run();

  tray_icon.DisableRefresh();
  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();
  win_sparkle_cleanup();

  return ret;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_SOFTKEYBOARD, [this] {
    if (m_keyboard.IsVisible()) {
      m_keyboard_suppressed = true;
      m_tool_panel.Hide();
      m_keyboard.Hide();
    } else {
      m_keyboard_suppressed = false;
      m_keyboard.Show();
    }
    return true;
  });
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
