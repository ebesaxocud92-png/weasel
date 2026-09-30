#pragma once

#include <windows.h>

#include <atltypes.h>

#include <KeyEvent.h>
#include <WeaselIPCData.h>
#include <algorithm>
#include <functional>
#include <gdiplus.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct KeyDef;

typedef CWinTraits<WS_POPUP,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE>
    CKeyboardTraits;

// A phone-style soft keyboard owned by WeaselServer. It processes keys into
// the active Rime session and shows the current composition above the keys.
class WeaselKeyboard
    : public CWindowImpl<WeaselKeyboard, CWindow, CKeyboardTraits>,
      public CDoubleBufferImpl<WeaselKeyboard> {
 public:
  enum {
    WM_APP_CONTEXT_UPDATE = WM_APP + 42,
    TIMER_LONGPRESS = 1,
    TIMER_HINT = 2,
  };

  BEGIN_MSG_MAP(WeaselKeyboard)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
  MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  MESSAGE_HANDLER(WM_SIZE, OnSize)
  MESSAGE_HANDLER(WM_DISPLAYCHANGE, OnDisplayChange)
  MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  MESSAGE_HANDLER(WM_APP_CONTEXT_UPDATE, OnContextUpdateMessage)
  CHAIN_MSG_MAP(CDoubleBufferImpl<WeaselKeyboard>)
  END_MSG_MAP()

  WeaselKeyboard();
  ~WeaselKeyboard();

  bool Create(HWND parent);
  void Show();
  void Hide();
  void ToggleShow();
  bool IsVisible() const;
  void Reposition();

  void SetKeyHandler(std::function<bool(weasel::KeyEvent const&)> handler) {
    m_key_handler = std::move(handler);
  }
  void SetSelectHandler(std::function<void(size_t)> handler) {
    m_select_handler = std::move(handler);
  }
  void SetModeToggleHandler(std::function<void(bool)> handler) {
    m_mode_toggle_handler = std::move(handler);
  }

  // Thread-safe: may be called from IPC worker threads.
  void OnEngineUpdate(DWORD session_id,
                      const weasel::Context& ctx,
                      const weasel::Status& status);

  // Ask the focused client to pull the pending engine response by injecting
  // a flush key event (the same trick the candidate window uses).
  static void FlushToClient();

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnLButtonDown(UINT uMsg,
                        WPARAM wParam,
                        LPARAM lParam,
                        BOOL& bHandled);
  LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseActivate(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDisplayChange(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnSettingChange(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnContextUpdateMessage(UINT uMsg,
                                 WPARAM wParam,
                                 LPARAM lParam,
                                 BOOL& bHandled);

  void DoPaint(CDCHandle dc);

 private:
  struct KeyLayout {
    CRect rect;
    int id;
  };

  int _Scaled(int value) const;
  void _UpdateDpi();
  void _UpdateFonts();
  void _Layout();
  void _Repaint();
  void _ActivateKey(int key_index);
  void _InjectKey(const KeyDef& def, bool shift);
  int _KeyAt(CPoint pt) const;
  int _CandidateAt(CPoint pt) const;
  bool _CurrentAsciiMode() const;

  void _Draw(Gdiplus::Graphics& g, const CRect& rc);

  std::vector<KeyLayout> m_keys;
  std::vector<std::pair<CRect, size_t>> m_candidate_rects;
  CRect m_mode_rect;

  std::function<bool(weasel::KeyEvent const&)> m_key_handler;
  std::function<void(size_t)> m_select_handler;
  std::function<void(bool)> m_mode_toggle_handler;

  mutable std::mutex m_data_mutex;
  weasel::Context m_ctx;
  weasel::Status m_status;
  std::wstring m_hint;

  UINT m_dpi = 96;
  bool m_shift = false;
  bool m_voice_active = false;
  int m_pressed_key = -1;
  int m_pressed_candidate = -1;
  bool m_pressed_mode = false;
  UINT_PTR m_longpress_timer = 0;
  UINT_PTR m_hint_timer = 0;

  ULONG_PTR m_gdiplus_token = 0;
  Gdiplus::GdiplusStartupInput m_gdiplus_input;
  std::unique_ptr<Gdiplus::FontFamily> m_font_family;
  std::unique_ptr<Gdiplus::Font> m_font_key;
  std::unique_ptr<Gdiplus::Font> m_font_fn;
  std::unique_ptr<Gdiplus::Font> m_font_candidate;
  std::unique_ptr<Gdiplus::Font> m_font_preedit;
  std::unique_ptr<Gdiplus::Font> m_font_hint;
};
