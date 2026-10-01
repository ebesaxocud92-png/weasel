#include "stdafx.h"
#include "WeaselToolPanel.h"

#include "WeaselKeyboard.h"

#include <ShellScalingApi.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

const Gdiplus::Color kColorCard(255, 255, 255, 255);
const Gdiplus::Color kColorLine(255, 228, 231, 238);
const Gdiplus::Color kColorKeyFn(255, 227, 230, 237);
const Gdiplus::Color kColorKeyPressed(255, 205, 210, 220);
const Gdiplus::Color kColorText(255, 26, 26, 26);
const Gdiplus::Color kColorSubText(255, 138, 143, 153);
const Gdiplus::Color kColorAccent(255, 52, 120, 246);
const Gdiplus::Color kColorAccentSoft(255, 230, 239, 255);

const wchar_t kFontFace[] = L"Microsoft YaHei UI";
constexpr size_t kMaxHistory = 20;
constexpr size_t kMaxItemLength = 64;

void _AddRoundedRectPath(Gdiplus::GraphicsPath& path,
                         const Gdiplus::RectF& rect,
                         float radius) {
  float d = radius * 2.0f;
  if (d > rect.Width)
    d = rect.Width;
  if (d > rect.Height)
    d = rect.Height;
  path.StartFigure();
  path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
  path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
  path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
  path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
  path.CloseFigure();
}

void _FillRoundedRect(Gdiplus::Graphics& g,
                      const Gdiplus::RectF& rect,
                      float radius,
                      const Gdiplus::Color& color) {
  Gdiplus::GraphicsPath path;
  _AddRoundedRectPath(path, rect, radius);
  Gdiplus::SolidBrush brush(color);
  g.FillPath(&brush, &path);
}

void _DrawTextInRect(
    Gdiplus::Graphics& g,
    const std::wstring& text,
    Gdiplus::Font* font,
    const Gdiplus::RectF& rect,
    const Gdiplus::Color& color,
    Gdiplus::StringAlignment align = Gdiplus::StringAlignmentCenter) {
  if (text.empty() || !font)
    return;
  Gdiplus::StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  Gdiplus::SolidBrush brush(color);
  g.DrawString(text.c_str(), (INT)text.size(), font, rect, &format, &brush);
}

void _InjectVk(WORD vk, bool shift) {
  std::vector<INPUT> inputs;
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  if (shift) {
    input.ki.wVk = VK_SHIFT;
    inputs.push_back(input);
  }
  input.ki.wVk = vk;
  inputs.push_back(input);
  input.ki.dwFlags = KEYEVENTF_KEYUP;
  inputs.push_back(input);
  if (shift) {
    input.ki.wVk = VK_SHIFT;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(input);
  }
  ::SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
}

void _DrawArrow(Gdiplus::Graphics& g,
                const CRect& rc,
                UINT ibus_code,
                const Gdiplus::Color& color,
                float scale) {
  const float cx = ((float)rc.left + (float)rc.right) / 2.0f;
  const float cy = ((float)rc.top + (float)rc.bottom) / 2.0f;
  const float s = 7.0f * scale;
  Gdiplus::Pen pen(color, 2.2f * scale);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  Gdiplus::GraphicsPath path;
  switch (ibus_code) {
    case ibus::Left:
      path.AddLine(cx + s * 0.5f, cy - s, cx - s * 0.5f, cy);
      path.AddLine(cx - s * 0.5f, cy, cx + s * 0.5f, cy + s);
      break;
    case ibus::Right:
      path.AddLine(cx - s * 0.5f, cy - s, cx + s * 0.5f, cy);
      path.AddLine(cx + s * 0.5f, cy, cx - s * 0.5f, cy + s);
      break;
    case ibus::Up:
      path.AddLine(cx - s, cy + s * 0.5f, cx, cy - s * 0.5f);
      path.AddLine(cx, cy - s * 0.5f, cx + s, cy + s * 0.5f);
      break;
    case ibus::Down:
      path.AddLine(cx - s, cy - s * 0.5f, cx, cy + s * 0.5f);
      path.AddLine(cx, cy + s * 0.5f, cx + s, cy - s * 0.5f);
      break;
    default:
      return;
  }
  g.DrawPath(&pen, &path);
}

}  // namespace

WeaselToolPanel::WeaselToolPanel() {
  Gdiplus::GdiplusStartup(&m_gdiplus_token, &m_gdiplus_input, NULL);
}

WeaselToolPanel::~WeaselToolPanel() {
  if (IsWindow()) {
    KillTimer(TIMER_CLIPBOARD_WATCH);
    UnregisterTouchWindow(m_hWnd);
  }
  _ReleaseBackBuffer();
  m_font_text.reset();
  m_font_small.reset();
  m_font_family.reset();
  if (m_gdiplus_token) {
    Gdiplus::GdiplusShutdown(m_gdiplus_token);
    m_gdiplus_token = 0;
  }
}

bool WeaselToolPanel::Create(HWND parent) {
  if (IsWindow())
    return true;
  RECT rc = {0, 0, 0, 0};
  return CWindowImpl<WeaselToolPanel, CWindow, CToolPanelTraits>::Create(
             parent, rc, NULL, WS_POPUP,
             WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                 WS_EX_LAYERED,
             0U) != NULL;
}

void WeaselToolPanel::Toggle(Page page) {
  if (IsVisible() && m_page == page) {
    Hide();
    return;
  }
  Show(page);
}

void WeaselToolPanel::Show(Page page) {
  if (!IsWindow())
    return;
  m_page = page;
  _UpdateDpi();
  _Layout();
  if (m_anchor && ::IsWindow(m_anchor)) {
    CRect anchor_rc;
    ::GetWindowRect(m_anchor, &anchor_rc);
    CRect rc;
    GetWindowRect(&rc);
    const int width = rc.Width();
    const int height = rc.Height();
    int x = anchor_rc.left + (anchor_rc.Width() - width) / 2;
    int y = anchor_rc.top - height - _Scaled(8);
    // Keep the panel on screen.
    HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    if (monitor && GetMonitorInfo(monitor, &mi)) {
      if (x < mi.rcWork.left + _Scaled(8))
        x = mi.rcWork.left + _Scaled(8);
      if (x + width > mi.rcWork.right - _Scaled(8))
        x = mi.rcWork.right - _Scaled(8) - width;
      if (y < mi.rcWork.top + _Scaled(8))
        y = mi.rcWork.top + _Scaled(8);
    }
    ::SetWindowPos(m_hWnd, NULL, x, y, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
  }
  _PollClipboard();
  _Render();
  ShowWindow(SW_SHOWNA);
}

void WeaselToolPanel::Hide() {
  if (IsWindow())
    ShowWindow(SW_HIDE);
}

bool WeaselToolPanel::IsVisible() const {
  return IsWindow() && (IsWindowVisible() != FALSE);
}

LRESULT WeaselToolPanel::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
  _UpdateDpi();
  _UpdateFonts();
  ::RegisterTouchWindow(m_hWnd, 0);
  SetTimer(TIMER_CLIPBOARD_WATCH, 500);
  return 0;
}

LRESULT WeaselToolPanel::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
  KillTimer(TIMER_CLIPBOARD_WATCH);
  ::UnregisterTouchWindow(m_hWnd);
  _ReleaseBackBuffer();
  return 0;
}

LRESULT WeaselToolPanel::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
  _Render();
  ::ValidateRect(m_hWnd, NULL);
  return 0;
}

LRESULT WeaselToolPanel::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
  return 1;
}

LRESULT WeaselToolPanel::OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
  return MA_NOACTIVATE;
}

LRESULT WeaselToolPanel::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
  SetCapture();
  _PointerDown(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
  return 0;
}

LRESULT WeaselToolPanel::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
  if (GetCapture() == m_hWnd)
    ReleaseCapture();
  _PointerUp(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
  return 0;
}

LRESULT WeaselToolPanel::OnTouch(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
  const UINT count = LOWORD(wParam);
  if (!count)
    return 0;
  HTOUCHINPUT handle = reinterpret_cast<HTOUCHINPUT>(lParam);
  std::vector<TOUCHINPUT> inputs(count);
  if (!::GetTouchInputInfo(handle, count, inputs.data(), sizeof(TOUCHINPUT))) {
    ::CloseTouchInputHandle(handle);
    return 0;
  }
  auto client_point = [this](const TOUCHINPUT& ti) {
    CPoint pt((int)(ti.x / 100), (int)(ti.y / 100));
    ::ScreenToClient(m_hWnd, &pt);
    return pt;
  };
  for (const TOUCHINPUT& ti : inputs) {
    if (!m_touch_active && (ti.dwFlags & TOUCHEVENTF_DOWN)) {
      m_touch_active = true;
      m_touch_id = ti.dwID;
      SetCapture();
      _PointerDown(client_point(ti));
    } else if (m_touch_active && ti.dwID == m_touch_id &&
               (ti.dwFlags & TOUCHEVENTF_UP)) {
      m_touch_active = false;
      if (GetCapture() == m_hWnd)
        ReleaseCapture();
      _PointerUp(client_point(ti));
    }
  }
  ::CloseTouchInputHandle(handle);
  return 0;
}

LRESULT WeaselToolPanel::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
  if (wParam == TIMER_CLIPBOARD_WATCH) {
    _PollClipboard();
  }
  return 0;
}

void WeaselToolPanel::_PointerDown(CPoint pt) {
  m_pressed_item = _ItemAt(pt);
  _Render();
}

void WeaselToolPanel::_PointerUp(CPoint pt) {
  if (m_pressed_item >= 0 && _ItemAt(pt) == m_pressed_item) {
    _ActivateItem(m_pressed_item);
  }
  m_pressed_item = -1;
  _Render();
}

int WeaselToolPanel::_ItemAt(CPoint pt) const {
  for (const auto& entry : m_items) {
    if (entry.first.PtInRect(pt))
      return entry.second;
  }
  return -1;
}

void WeaselToolPanel::_ActivateItem(int item) {
  switch (item) {
    case ITEM_SELECT_ALL:
      WeaselKeyboard::SendCtrlKey('A');
      break;
    case ITEM_CUT:
      WeaselKeyboard::SendCtrlKey('X');
      break;
    case ITEM_COPY:
      WeaselKeyboard::SendCtrlKey('C');
      break;
    case ITEM_PASTE:
      WeaselKeyboard::SendCtrlKey('V');
      break;
    case ITEM_ARROW_LEFT:
    case ITEM_ARROW_UP:
    case ITEM_ARROW_DOWN:
    case ITEM_ARROW_RIGHT: {
      UINT code = ibus::Left;
      WORD vk = VK_LEFT;
      switch (item) {
        case ITEM_ARROW_UP:
          code = ibus::Up;
          vk = VK_UP;
          break;
        case ITEM_ARROW_DOWN:
          code = ibus::Down;
          vk = VK_DOWN;
          break;
        case ITEM_ARROW_RIGHT:
          code = ibus::Right;
          vk = VK_RIGHT;
          break;
        default:
          break;
      }
      weasel::KeyEvent event(code, 0);
      const bool handled = m_key_handler && m_key_handler(event);
      if (!handled)
        _InjectVk(vk, false);
      break;
    }
    case ITEM_CLEAR: {
      {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        m_history.clear();
      }
      _Layout();
      _Render();
      break;
    }
    default:
      if (item >= ITEM_HISTORY) {
        _PasteHistory((size_t)(item - ITEM_HISTORY));
      }
      break;
  }
}

void WeaselToolPanel::_PasteHistory(size_t index) {
  std::wstring text;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    if (index >= m_history.size())
      return;
    text = m_history[index];
  }
  if (!::OpenClipboard(m_hWnd))
    return;
  ::EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (mem) {
    void* dst = ::GlobalLock(mem);
    if (dst) {
      memcpy(dst, text.c_str(), bytes);
      ::GlobalUnlock(mem);
    }
    ::SetClipboardData(CF_UNICODETEXT, mem);
  }
  ::CloseClipboard();
  WeaselKeyboard::SendCtrlKey('V');
}

void WeaselToolPanel::_PollClipboard() {
  DWORD seq = ::GetClipboardSequenceNumber();
  if (seq == m_clip_seq)
    return;
  m_clip_seq = seq;
  if (!::OpenClipboard(NULL))
    return;
  HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
  if (data) {
    const wchar_t* src = static_cast<const wchar_t*>(::GlobalLock(data));
    if (src) {
      std::wstring text(src);
      if (text.size() > kMaxItemLength)
        text.resize(kMaxItemLength);
      ::GlobalUnlock(data);
      // Normalize line breaks for display.
      for (auto& ch : text) {
        if (ch == L'\r' || ch == L'\n')
          ch = L' ';
      }
      while (!text.empty() && text.back() == L' ')
        text.pop_back();
      _AddClipboardText(text);
    }
  }
  ::CloseClipboard();
}

void WeaselToolPanel::_AddClipboardText(const std::wstring& text) {
  if (text.empty())
    return;
  bool relayout = false;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    for (auto it = m_history.begin(); it != m_history.end(); ++it) {
      if (*it == text) {
        m_history.erase(it);
        break;
      }
    }
    m_history.insert(m_history.begin(), text);
    if (m_history.size() > kMaxHistory)
      m_history.resize(kMaxHistory);
    relayout = IsVisible() && m_page == PAGE_CLIPBOARD;
  }
  if (relayout) {
    _Layout();
    _Render();
  }
}

void WeaselToolPanel::_UpdateDpi() {
  HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
  UINT dpi_x = 96;
  UINT dpi_y = 96;
  if (monitor) {
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
  }
  m_dpi = dpi_x ? dpi_x : 96;
}

void WeaselToolPanel::_UpdateFonts() {
  m_font_text.reset();
  m_font_small.reset();
  m_font_family.reset();
  m_font_family = std::make_unique<Gdiplus::FontFamily>(kFontFace);
  if (!m_font_family->IsAvailable()) {
    m_font_family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
  }
  if (!m_font_family->IsAvailable()) {
    m_font_family = std::make_unique<Gdiplus::FontFamily>(L"Arial");
  }
  const Gdiplus::REAL base = (Gdiplus::REAL)m_dpi / 96.0f;
  m_font_text = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 15.0f * base, Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
  m_font_small = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 13.0f * base, Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
}

int WeaselToolPanel::_Scaled(int value) const {
  return (int)std::lround(value * m_dpi / 96.0);
}

void WeaselToolPanel::_Layout() {
  CRect rc;
  GetClientRect(&rc);
  m_items.clear();
  const int pad = _Scaled(10);
  const int gap = _Scaled(8);
  if (m_page == PAGE_EDIT) {
    // Two rows: actions, then a large arrow cluster.
    const int w = _Scaled(380);
    const int h = _Scaled(158);
    if (rc.Width() != w || rc.Height() != h) {
      ::SetWindowPos(m_hWnd, NULL, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      GetClientRect(&rc);
    }
    const int btn_h = _Scaled(40);
    const int y0 = pad;
    const int btn_w = (rc.Width() - pad * 2 - gap * 3) / 4;
    int x = pad;
    const int ids[] = {ITEM_SELECT_ALL, ITEM_CUT, ITEM_COPY, ITEM_PASTE};
    const wchar_t* labels[] = {L"全选", L"剪切", L"复制", L"粘贴"};
    for (int i = 0; i < 4; ++i) {
      m_items.emplace_back(CRect(x, y0, x + btn_w, y0 + btn_h), ids[i]);
      x += btn_w + gap;
    }
    // Arrow cluster: four buttons in a row, centered, bigger.
    const int arrow_w = _Scaled(62);
    const int arrow_h = _Scaled(52);
    const int y1 = y0 + btn_h + gap * 2;
    const int total = arrow_w * 4 + gap * 3;
    int ax = (rc.Width() - total) / 2;
    const int arrows[] = {ITEM_ARROW_LEFT, ITEM_ARROW_UP, ITEM_ARROW_DOWN,
                          ITEM_ARROW_RIGHT};
    for (int i = 0; i < 4; ++i) {
      m_items.emplace_back(CRect(ax, y1, ax + arrow_w, y1 + arrow_h),
                           arrows[i]);
      ax += arrow_w + gap;
    }
  } else {
    const int w = _Scaled(400);
    int history = 0;
    {
      std::lock_guard<std::mutex> lock(m_data_mutex);
      history = (int)m_history.size();
    }
    const int row_h = _Scaled(40);
    const int header_h = _Scaled(44);
    int h = pad + header_h + pad + row_h * std::max(1, history) + pad;
    if (h > _Scaled(420))
      h = _Scaled(420);
    if (rc.Width() != w || rc.Height() != h) {
      ::SetWindowPos(m_hWnd, NULL, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      GetClientRect(&rc);
    }
    // Header: clear button on the right.
    const int clear_w = _Scaled(64);
    const int clear_h = _Scaled(32);
    m_items.emplace_back(
        CRect(rc.right - pad - clear_w, pad, rc.right - pad, pad + clear_h),
        ITEM_CLEAR);
    // History rows.
    const int list_top = pad + header_h;
    const int list_h = rc.Height() - list_top - pad;
    const int visible = std::max(1, std::min(history, list_h / row_h));
    for (int i = 0; i < visible; ++i) {
      m_items.emplace_back(CRect(pad, list_top + i * row_h, rc.right - pad,
                                 list_top + i * row_h + row_h),
                           ITEM_HISTORY + i);
    }
  }
}

void WeaselToolPanel::_Render() {
  if (!IsWindow())
    return;
  CRect rc;
  GetClientRect(&rc);
  const int width = rc.Width();
  const int height = rc.Height();
  if (width <= 0 || height <= 0)
    return;
  _EnsureBackBuffer(width, height);
  if (!m_back_bmp)
    return;
  {
    Gdiplus::Graphics g(m_back_bmp.get());
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    _Draw(g, rc);
    g.Flush();
  }
  HDC screen_dc = ::GetDC(NULL);
  CRect window_rect;
  GetWindowRect(&window_rect);
  POINT dst = {window_rect.left, window_rect.top};
  SIZE size = {width, height};
  POINT src = {0, 0};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  ::UpdateLayeredWindow(m_hWnd, screen_dc, &dst, &size, m_back_dc, &src, 0,
                        &blend, ULW_ALPHA);
  ::ReleaseDC(NULL, screen_dc);
}

void WeaselToolPanel::_EnsureBackBuffer(int width, int height) {
  if (m_back_bmp && m_back_w == width && m_back_h == height)
    return;
  _ReleaseBackBuffer();
  HDC screen_dc = ::GetDC(NULL);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  m_back_dib =
      ::CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  m_back_dc = ::CreateCompatibleDC(screen_dc);
  ::ReleaseDC(NULL, screen_dc);
  if (!m_back_dib || !m_back_dc || !bits) {
    _ReleaseBackBuffer();
    return;
  }
  m_back_old = ::SelectObject(m_back_dc, m_back_dib);
  m_back_bmp = std::make_unique<Gdiplus::Bitmap>(width, height, width * 4,
                                                 PixelFormat32bppPARGB,
                                                 static_cast<BYTE*>(bits));
  if (m_back_bmp->GetLastStatus() != Gdiplus::Ok) {
    _ReleaseBackBuffer();
    return;
  }
  m_back_w = width;
  m_back_h = height;
}

void WeaselToolPanel::_ReleaseBackBuffer() {
  m_back_bmp.reset();
  if (m_back_dc && m_back_old) {
    ::SelectObject(m_back_dc, m_back_old);
    m_back_old = nullptr;
  }
  if (m_back_dc) {
    ::DeleteDC(m_back_dc);
    m_back_dc = nullptr;
  }
  if (m_back_dib) {
    ::DeleteObject(m_back_dib);
    m_back_dib = nullptr;
  }
  m_back_w = 0;
  m_back_h = 0;
}

void WeaselToolPanel::_Draw(Gdiplus::Graphics& g, const CRect& rc) {
  std::vector<std::wstring> history;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    history = m_history;
  }
  const int pad = _Scaled(10);
  const float width = (float)rc.Width();
  const float height = (float)rc.Height();
  const float radius = (float)_Scaled(12);
  const float chip_radius = (float)_Scaled(9);
  const float dpi_scale = (float)m_dpi / 96.0f;

  // Window background with rounded corners.
  {
    Gdiplus::GraphicsPath path;
    _AddRoundedRectPath(path, Gdiplus::RectF(0.0f, 0.0f, width, height),
                        radius);
    Gdiplus::SolidBrush brush(kColorCard);
    g.FillPath(&brush, &path);
  }

  if (m_page == PAGE_EDIT) {
    for (const auto& entry : m_items) {
      const int item = entry.second;
      if (item > ITEM_ARROW_RIGHT)
        continue;
      const bool pressed = (item == m_pressed_item);
      const Gdiplus::RectF rect((float)entry.first.left, (float)entry.first.top,
                                (float)entry.first.Width(),
                                (float)entry.first.Height());
      _FillRoundedRect(g, rect, chip_radius,
                       pressed ? kColorKeyPressed : kColorKeyFn);
      if (item >= ITEM_ARROW_LEFT && item <= ITEM_ARROW_RIGHT) {
        UINT code = ibus::Left;
        switch (item) {
          case ITEM_ARROW_UP:
            code = ibus::Up;
            break;
          case ITEM_ARROW_DOWN:
            code = ibus::Down;
            break;
          case ITEM_ARROW_RIGHT:
            code = ibus::Right;
            break;
          default:
            break;
        }
        _DrawArrow(g, entry.first, code, pressed ? kColorAccent : kColorText,
                   dpi_scale);
      } else {
        std::wstring label;
        switch (item) {
          case ITEM_SELECT_ALL:
            label = L"全选";
            break;
          case ITEM_CUT:
            label = L"剪切";
            break;
          case ITEM_COPY:
            label = L"复制";
            break;
          case ITEM_PASTE:
            label = L"粘贴";
            break;
        }
        _DrawTextInRect(g, label, m_font_text.get(), rect,
                        pressed ? kColorAccent : kColorText);
      }
    }
  } else {
    // Header.
    const int header_h = _Scaled(44);
    _DrawTextInRect(g, L"剪贴板", m_font_text.get(),
                    Gdiplus::RectF((float)pad, (float)pad, (float)_Scaled(80),
                                   (float)(header_h - pad)),
                    kColorText, Gdiplus::StringAlignmentNear);
    for (const auto& entry : m_items) {
      if (entry.second != ITEM_CLEAR)
        continue;
      const bool pressed = (entry.second == m_pressed_item);
      const Gdiplus::RectF rect((float)entry.first.left, (float)entry.first.top,
                                (float)entry.first.Width(),
                                (float)entry.first.Height());
      _FillRoundedRect(g, rect, chip_radius,
                       pressed ? kColorKeyPressed : kColorKeyFn);
      _DrawTextInRect(g, L"清空", m_font_small.get(), rect,
                      pressed ? kColorAccent : kColorSubText);
    }
    // History rows.
    const int list_top = pad + header_h;
    for (const auto& entry : m_items) {
      if (entry.second < ITEM_HISTORY)
        continue;
      const size_t index = (size_t)(entry.second - ITEM_HISTORY);
      if (index >= history.size())
        continue;
      const bool pressed = (entry.second == m_pressed_item);
      const Gdiplus::RectF rect((float)entry.first.left, (float)entry.first.top,
                                (float)entry.first.Width(),
                                (float)entry.first.Height());
      if (pressed) {
        _FillRoundedRect(g, rect, chip_radius, kColorAccentSoft);
      }
      _DrawTextInRect(g, history[index], m_font_text.get(), rect,
                      pressed ? kColorAccent : kColorText,
                      Gdiplus::StringAlignmentNear);
      if ((int)index < (int)history.size() - 1) {
        Gdiplus::SolidBrush line_brush(kColorLine);
        g.FillRectangle(&line_brush,
                        Gdiplus::RectF((float)pad, rect.GetBottom() - 1.0f,
                                       width - (float)pad * 2, 1.0f));
      }
    }
    if (history.empty()) {
      _DrawTextInRect(
          g, L"复制过的内容会出现在这里", m_font_small.get(),
          Gdiplus::RectF((float)pad, (float)list_top, width - (float)pad * 2,
                         (float)_Scaled(60)),
          kColorSubText);
    }
  }
}
