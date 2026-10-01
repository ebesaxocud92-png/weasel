#pragma once

#include <windows.h>

#include <atltypes.h>

#include <KeyEvent.h>
#include <functional>
#include <gdiplus.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

typedef CWinTraits<WS_POPUP,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                       WS_EX_LAYERED>
    CToolPanelTraits;

// Floating panel above the soft keyboard, opened from the keyboard toolbar.
// Two pages: an edit page (select all / cut / copy / paste + arrow cluster)
// and a clipboard page (recent clipboard history, tap to paste).
class WeaselToolPanel
    : public CWindowImpl<WeaselToolPanel, CWindow, CToolPanelTraits> {
 public:
  enum Page {
    PAGE_EDIT = 0,
    PAGE_CLIPBOARD,
  };

  enum {
    TIMER_CLIPBOARD_WATCH = 1,
  };

  BEGIN_MSG_MAP(WeaselToolPanel)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_PAINT, OnPaint)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
  MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  MESSAGE_HANDLER(WM_TOUCH, OnTouch)
  END_MSG_MAP()

  WeaselToolPanel();
  ~WeaselToolPanel();

  bool Create(HWND parent);
  // Panel anchors above this window (the keyboard) when shown.
  void SetAnchorWindow(HWND hwnd) { m_anchor = hwnd; }
  // Show a page anchored above the keyboard window; tapping the same page
  // again while visible hides the panel.
  void Toggle(Page page);
  void Show(Page page);
  void Hide();
  bool IsVisible() const;

  void SetKeyHandler(std::function<bool(weasel::KeyEvent const&)> handler) {
    m_key_handler = std::move(handler);
  }

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
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
  LRESULT OnTouch(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

 private:
  enum ItemId {
    ITEM_SELECT_ALL = 1,
    ITEM_CUT,
    ITEM_COPY,
    ITEM_PASTE,
    ITEM_ARROW_LEFT,
    ITEM_ARROW_UP,
    ITEM_ARROW_DOWN,
    ITEM_ARROW_RIGHT,
    ITEM_CLEAR,
    ITEM_HISTORY = 1000,  // + history index
  };

  int _Scaled(int value) const;
  void _UpdateDpi();
  void _UpdateFonts();
  void _Layout();
  void _Render();
  void _EnsureBackBuffer(int width, int height);
  void _ReleaseBackBuffer();
  void _Draw(Gdiplus::Graphics& g, const CRect& rc);
  void _PointerDown(CPoint pt);
  void _PointerUp(CPoint pt);
  int _ItemAt(CPoint pt) const;
  void _ActivateItem(int item);
  void _PasteHistory(size_t index);
  void _PollClipboard();
  void _AddClipboardText(const std::wstring& text);

  std::vector<std::pair<CRect, int>> m_items;
  int m_pressed_item = -1;

  Page m_page = PAGE_EDIT;
  HWND m_anchor = nullptr;

  std::function<bool(weasel::KeyEvent const&)> m_key_handler;

  mutable std::mutex m_data_mutex;
  std::vector<std::wstring> m_history;
  DWORD m_clip_seq = 0;

  UINT m_dpi = 96;
  bool m_touch_active = false;
  DWORD m_touch_id = 0;

  HBITMAP m_back_dib = nullptr;
  HDC m_back_dc = nullptr;
  HGDIOBJ m_back_old = nullptr;
  std::unique_ptr<Gdiplus::Bitmap> m_back_bmp;
  int m_back_w = 0;
  int m_back_h = 0;

  ULONG_PTR m_gdiplus_token = 0;
  Gdiplus::GdiplusStartupInput m_gdiplus_input;
  std::unique_ptr<Gdiplus::FontFamily> m_font_family;
  std::unique_ptr<Gdiplus::Font> m_font_text;
  std::unique_ptr<Gdiplus::Font> m_font_small;
};
