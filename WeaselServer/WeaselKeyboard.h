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
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                       WS_EX_LAYERED>
    CKeyboardTraits;

// A phone-style soft keyboard owned by WeaselServer. It renders with GDI+ onto
// a layered window (per-pixel alpha, rounded top corners), processes keys into
// the active Rime session, and shows the current composition above the keys.
// The grip strip along the top edge can be dragged to resize the height.
class WeaselKeyboard
    : public CWindowImpl<WeaselKeyboard, CWindow, CKeyboardTraits> {
 public:
  enum {
    WM_APP_CONTEXT_UPDATE = WM_APP + 42,
    TIMER_LONGPRESS = 1,
    TIMER_HINT = 2,
  };

  enum ToolButton {
    TOOL_CLIPBOARD = 0,
    TOOL_EDIT,
    TOOL_VOICE,
  };

  BEGIN_MSG_MAP(WeaselKeyboard)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_PAINT, OnPaint)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
  MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
  MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
  MESSAGE_HANDLER(WM_TOUCH, OnTouch)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  MESSAGE_HANDLER(WM_SIZE, OnSize)
  MESSAGE_HANDLER(WM_DISPLAYCHANGE, OnDisplayChange)
  MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  MESSAGE_HANDLER(WM_APP_CONTEXT_UPDATE, OnContextUpdateMessage)
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
  // Toolbar buttons above the letter rows (clipboard / edit / voice).
  void SetToolHandler(std::function<void(int)> handler) {
    m_tool_handler = std::move(handler);
  }
  // Called when the dismiss key is tapped (default: hide the keyboard).
  void SetDismissHandler(std::function<void()> handler) {
    m_dismiss_handler = std::move(handler);
  }
  HWND Hwnd() const { return m_hWnd; }
  // Show a transient hint in the candidate strip; auto-clears after a while.
  void ShowHint(const std::wstring& hint);

  // Inject a Ctrl+key combination into the focused application. Also used by
  // the tool panel (cut/copy/paste/select all).
  static void SendCtrlKey(WORD vk);

  // Thread-safe: may be called from IPC worker threads.
  void OnEngineUpdate(DWORD session_id,
                      const weasel::Context& ctx,
                      const weasel::Status& status);

  // Ask the focused client to pull the pending engine response by injecting
  // a flush key event (the same trick the candidate window uses).
  static void FlushToClient();

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnLButtonDown(UINT uMsg,
                        WPARAM wParam,
                        LPARAM lParam,
                        BOOL& bHandled);
  LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseActivate(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnTouch(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
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

 private:
  struct KeyLayout {
    CRect rect;
    int id;
  };

  int _Scaled(int value) const;
  void _UpdateDpi();
  void _UpdateFonts();
  void _Layout();
  void _Render();
  void _EnsureBackBuffer(int width, int height);
  void _ReleaseBackBuffer();
  void _ActivateKey(int key_index);
  void _InjectKey(const KeyDef& def, bool shift);
  // Shared press handling for mouse and touch input.
  void _PointerDown(CPoint pt);
  void _PointerUp(CPoint pt);
  void _PointerMove();
  int _KeyAt(CPoint pt) const;
  int _CandidateAt(CPoint pt) const;
  int _ToolAt(CPoint pt) const;
  bool _CurrentAsciiMode() const;
  void _Draw(Gdiplus::Graphics& g, const CRect& rc);

  std::vector<KeyLayout> m_keys;
  std::vector<std::pair<CRect, size_t>> m_candidate_rects;
  std::vector<std::pair<CRect, int>> m_toolbar_rects;
  CRect m_grip_rect;
  CRect m_dismiss_rect;
  int m_grip_h = 0;
  int m_strip_h = 0;
  int m_toolbar_h = 0;

  std::function<bool(weasel::KeyEvent const&)> m_key_handler;
  std::function<void(size_t)> m_select_handler;
  std::function<void(bool)> m_mode_toggle_handler;
  std::function<void(int)> m_tool_handler;
  std::function<void()> m_dismiss_handler;

  mutable std::mutex m_data_mutex;
  weasel::Context m_ctx;
  weasel::Status m_status;
  std::wstring m_hint;

  // Keyboard height in logical (96 dpi) units; persisted on resize.
  int m_height96 = 0;
  bool m_resizing = false;
  CPoint m_drag_start;
  int m_drag_start_h = 0;

  UINT m_dpi = 96;
  bool m_shift = false;
  bool m_voice_active = false;
  int m_pressed_key = -1;
  int m_pressed_candidate = -1;
  int m_pressed_tool = -1;
  bool m_pressed_dismiss = false;
  UINT_PTR m_longpress_timer = 0;
  UINT_PTR m_hint_timer = 0;

  // Touch handling: the window is registered as a touch window so Windows
  // does not turn press-and-hold into a right-click gesture on the keys.
  bool m_touch_active = false;
  DWORD m_touch_id = 0;

  // Back buffer for layered rendering.
  HBITMAP m_back_dib = nullptr;
  HDC m_back_dc = nullptr;
  HGDIOBJ m_back_old = nullptr;
  std::unique_ptr<Gdiplus::Bitmap> m_back_bmp;
  int m_back_w = 0;
  int m_back_h = 0;

  ULONG_PTR m_gdiplus_token = 0;
  Gdiplus::GdiplusStartupInput m_gdiplus_input;
  std::unique_ptr<Gdiplus::FontFamily> m_font_family;
  std::unique_ptr<Gdiplus::Font> m_font_key;
  std::unique_ptr<Gdiplus::Font> m_font_fn;
  std::unique_ptr<Gdiplus::Font> m_font_candidate;
  std::unique_ptr<Gdiplus::Font> m_font_preedit;
  std::unique_ptr<Gdiplus::Font> m_font_hint;
};
