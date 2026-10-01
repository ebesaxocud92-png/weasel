#include "stdafx.h"
#include "WeaselKeyboard.h"

#include <ShellScalingApi.h>
#include <WeaselUtility.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "gdiplus.lib")

// Key table ----------------------------------------------------------------

enum KeyAction {
  KEY_NORMAL = 0,
  KEY_SHIFT,
  KEY_BACKSPACE,
  KEY_ENTER,
  KEY_TOGGLE_MODE,
  KEY_SPACE,
};

struct KeyDef {
  const wchar_t* label;
  UINT ibus_code;
  UINT vk;
  int action;
  float weight;
  bool is_fn;
};

namespace {

const KeyDef kRow1[] = {
    {L"q", 'q', 'Q', KEY_NORMAL, 1.0f, false},
    {L"w", 'w', 'W', KEY_NORMAL, 1.0f, false},
    {L"e", 'e', 'E', KEY_NORMAL, 1.0f, false},
    {L"r", 'r', 'R', KEY_NORMAL, 1.0f, false},
    {L"t", 't', 'T', KEY_NORMAL, 1.0f, false},
    {L"y", 'y', 'Y', KEY_NORMAL, 1.0f, false},
    {L"u", 'u', 'U', KEY_NORMAL, 1.0f, false},
    {L"i", 'i', 'I', KEY_NORMAL, 1.0f, false},
    {L"o", 'o', 'O', KEY_NORMAL, 1.0f, false},
    {L"p", 'p', 'P', KEY_NORMAL, 1.0f, false},
};

const KeyDef kRow2[] = {
    {L"a", 'a', 'A', KEY_NORMAL, 1.0f, false},
    {L"s", 's', 'S', KEY_NORMAL, 1.0f, false},
    {L"d", 'd', 'D', KEY_NORMAL, 1.0f, false},
    {L"f", 'f', 'F', KEY_NORMAL, 1.0f, false},
    {L"g", 'g', 'G', KEY_NORMAL, 1.0f, false},
    {L"h", 'h', 'H', KEY_NORMAL, 1.0f, false},
    {L"j", 'j', 'J', KEY_NORMAL, 1.0f, false},
    {L"k", 'k', 'K', KEY_NORMAL, 1.0f, false},
    {L"l", 'l', 'L', KEY_NORMAL, 1.0f, false},
};

const KeyDef kRow3[] = {
    {L"Shift", 0, 0, KEY_SHIFT, 1.5f, true},
    {L"z", 'z', 'Z', KEY_NORMAL, 1.0f, false},
    {L"x", 'x', 'X', KEY_NORMAL, 1.0f, false},
    {L"c", 'c', 'C', KEY_NORMAL, 1.0f, false},
    {L"v", 'v', 'V', KEY_NORMAL, 1.0f, false},
    {L"b", 'b', 'B', KEY_NORMAL, 1.0f, false},
    {L"n", 'n', 'N', KEY_NORMAL, 1.0f, false},
    {L"m", 'm', 'M', KEY_NORMAL, 1.0f, false},
    {L"Del", 0, VK_BACK, KEY_BACKSPACE, 1.5f, true},
};

// The main key face follows the phone keyboard convention: three letter rows
// and a bottom function row. Edit/clipboard tools live in the toolbar above.
const KeyDef kRow4[] = {
    {L"中", 0, 0, KEY_TOGGLE_MODE, 1.1f, true},
    {L"，", 0x2C, VK_OEM_COMMA, KEY_NORMAL, 1.0f, true},
    {L"按住说话", 0x20, VK_SPACE, KEY_SPACE, 5.0f, false},
    {L"。", 0x2E, VK_OEM_PERIOD, KEY_NORMAL, 1.0f, true},
    {L"换行", 0, VK_RETURN, KEY_ENTER, 1.1f, true},
};

struct RowDef {
  const KeyDef* defs;
  int count;
};

const RowDef kRows[] = {
    {kRow1, ARRAYSIZE(kRow1)},
    {kRow2, ARRAYSIZE(kRow2)},
    {kRow3, ARRAYSIZE(kRow3)},
    {kRow4, ARRAYSIZE(kRow4)},
};

const KeyDef& _DefById(int id) {
  const int row = (id >> 8) & 0xff;
  const int index = id & 0xff;
  return kRows[row].defs[index];
}

// Palette -------------------------------------------------------------------

const Gdiplus::Color kColorBg(255, 240, 242, 247);
const Gdiplus::Color kColorStrip(255, 255, 255, 255);
const Gdiplus::Color kColorLine(255, 228, 231, 238);
const Gdiplus::Color kColorKey(255, 255, 255, 255);
const Gdiplus::Color kColorKeyBorder(30, 0, 0, 0);
const Gdiplus::Color kColorKeyFn(255, 227, 230, 237);
const Gdiplus::Color kColorKeyPressed(255, 205, 210, 220);
const Gdiplus::Color kColorText(255, 26, 26, 26);
const Gdiplus::Color kColorSubText(255, 138, 143, 153);
const Gdiplus::Color kColorGrip(64, 0, 0, 0);
const Gdiplus::Color kColorAccent(255, 52, 120, 246);
const Gdiplus::Color kColorAccentSoft(255, 230, 239, 255);
const Gdiplus::Color kColorWhite(255, 255, 255, 255);

// Drawing helpers -----------------------------------------------------------

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

float _MeasureText(Gdiplus::Graphics& g,
                   const std::wstring& text,
                   Gdiplus::Font* font) {
  if (text.empty() || !font)
    return 0.0f;
  Gdiplus::RectF bounds;
  Gdiplus::StringFormat format;
  format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
  g.MeasureString(text.c_str(), (INT)text.size(), font, Gdiplus::PointF(0, 0),
                  &format, &bounds);
  return bounds.Width;
}

const wchar_t kFontFace[] = L"Microsoft YaHei UI";

// Keyboard height (logical 96 dpi units), persisted across restarts.
constexpr int kDefaultHeight96 = 340;
constexpr int kMinHeight96 = 250;
constexpr int kMaxHeight96 = 620;
const wchar_t kRegPath[] = L"Software\\Rime\\Weasel";
const wchar_t kRegValueHeight[] = L"SoftKeyboardHeight";

int _LoadHeight() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, kRegPath, kRegValueHeight,
                   RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
    if (value >= (DWORD)kMinHeight96 && value <= (DWORD)kMaxHeight96)
      return (int)value;
  }
  return kDefaultHeight96;
}

void _SaveHeight(int height) {
  DWORD value = (DWORD)height;
  RegSetKeyValueW(HKEY_CURRENT_USER, kRegPath, kRegValueHeight, REG_DWORD,
                  &value, sizeof(value));
}

}  // namespace

// WeaselKeyboard ------------------------------------------------------------

WeaselKeyboard::WeaselKeyboard() {
  Gdiplus::GdiplusStartup(&m_gdiplus_token, &m_gdiplus_input, NULL);
}

WeaselKeyboard::~WeaselKeyboard() {
  if (IsWindow()) {
    KillTimer(TIMER_LONGPRESS);
    KillTimer(TIMER_HINT);
  }
  _ReleaseBackBuffer();
  m_font_key.reset();
  m_font_fn.reset();
  m_font_candidate.reset();
  m_font_preedit.reset();
  m_font_hint.reset();
  m_font_family.reset();
  if (m_gdiplus_token) {
    Gdiplus::GdiplusShutdown(m_gdiplus_token);
    m_gdiplus_token = 0;
  }
}

bool WeaselKeyboard::Create(HWND parent) {
  if (IsWindow())
    return true;
  RECT rc = {0, 0, 0, 0};
  return CWindowImpl<WeaselKeyboard, CWindow, CKeyboardTraits>::Create(
             parent, rc, NULL, WS_POPUP,
             WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                 WS_EX_LAYERED,
             0U) != NULL;
}

void WeaselKeyboard::Show() {
  if (!IsWindow())
    return;
  Reposition();
  _Render();
  ShowWindow(SW_SHOWNA);
}

void WeaselKeyboard::Hide() {
  if (IsWindow())
    ShowWindow(SW_HIDE);
}

void WeaselKeyboard::ToggleShow() {
  if (IsVisible())
    Hide();
  else
    Show();
}

bool WeaselKeyboard::IsVisible() const {
  return IsWindow() && (IsWindowVisible() != FALSE);
}

void WeaselKeyboard::Reposition() {
  if (!IsWindow())
    return;
  if (m_height96 <= 0)
    m_height96 = kDefaultHeight96;
  HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi;
  mi.cbSize = sizeof(MONITORINFO);
  if (!monitor || !GetMonitorInfo(monitor, &mi))
    return;
  // Keep clear of the screen edges so Windows edge gestures (task view,
  // action center) keep working.
  const int margin = _Scaled(18);
  const int work_w = mi.rcWork.right - mi.rcWork.left;
  const int work_h = mi.rcWork.bottom - mi.rcWork.top;
  const int width = work_w - margin * 2;
  int height = _Scaled(m_height96);
  // Never let the keyboard cover the whole screen.
  if (height > work_h - _Scaled(80))
    height = work_h - _Scaled(80);
  SetWindowPos(NULL, mi.rcWork.left + margin, mi.rcWork.bottom - height, width,
               height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void WeaselKeyboard::FlushToClient() {
  // A key must translate through TranslateKeycode to reach the client; the
  // candidate window uses VK_SELECT for the same purpose.
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = VK_SELECT;
  ::SendInput(1, &input, sizeof(INPUT));
}

void WeaselKeyboard::OnEngineUpdate(DWORD session_id,
                                    const weasel::Context& ctx,
                                    const weasel::Status& status) {
  if (!session_id)
    return;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    m_ctx = ctx;
    m_status = status;
  }
  if (m_hWnd) {
    ::PostMessage(m_hWnd, WM_APP_CONTEXT_UPDATE, 0, 0);
  }
}

LRESULT WeaselKeyboard::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
  _UpdateDpi();
  m_height96 = _LoadHeight();
  _UpdateFonts();
  _Layout();
  Reposition();
  // Touch-aware: Windows skips press-and-hold-to-right-click gesture
  // recognition on this window; touch arrives as WM_TOUCH.
  ::RegisterTouchWindow(m_hWnd, 0);
  return 0;
}

LRESULT WeaselKeyboard::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
  KillTimer(TIMER_LONGPRESS);
  KillTimer(TIMER_HINT);
  m_longpress_timer = 0;
  m_hint_timer = 0;
  ::UnregisterTouchWindow(m_hWnd);
  _ReleaseBackBuffer();
  return 0;
}

LRESULT WeaselKeyboard::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
  _Render();
  ::ValidateRect(m_hWnd, NULL);
  return 0;
}

LRESULT WeaselKeyboard::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
  return 1;
}

LRESULT WeaselKeyboard::OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
  return MA_NOACTIVATE;
}

LRESULT WeaselKeyboard::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
  _Layout();
  _Render();
  return 0;
}

LRESULT WeaselKeyboard::OnDisplayChange(UINT, WPARAM, LPARAM, BOOL&) {
  _UpdateDpi();
  _UpdateFonts();
  _Layout();
  if (IsVisible())
    Reposition();
  _Render();
  return 0;
}

LRESULT WeaselKeyboard::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
  _UpdateDpi();
  _UpdateFonts();
  _Layout();
  _Render();
  return 0;
}

LRESULT WeaselKeyboard::OnSettingChange(UINT, WPARAM, LPARAM, BOOL&) {
  if (IsVisible())
    Reposition();
  return 0;
}

LRESULT WeaselKeyboard::OnContextUpdateMessage(UINT, WPARAM, LPARAM, BOOL&) {
  _Render();
  return 0;
}

LRESULT WeaselKeyboard::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL&) {
  if (wParam == TIMER_LONGPRESS) {
    KillTimer(TIMER_LONGPRESS);
    m_longpress_timer = 0;
    if (m_pressed_key >= 0 && m_pressed_key < (int)m_keys.size() &&
        _DefById(m_keys[m_pressed_key].id).action == KEY_SPACE &&
        !m_voice_active) {
      m_voice_active = true;
      _Render();
    }
  } else if (wParam == TIMER_HINT) {
    KillTimer(TIMER_HINT);
    m_hint_timer = 0;
    {
      std::lock_guard<std::mutex> lock(m_data_mutex);
      m_hint.clear();
    }
    _Render();
  }
  return 0;
}

LRESULT WeaselKeyboard::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
  _PointerDown(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
  return 0;
}

LRESULT WeaselKeyboard::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
  _PointerUp(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
  return 0;
}

LRESULT WeaselKeyboard::OnMouseMove(UINT, WPARAM, LPARAM, BOOL&) {
  _PointerMove();
  return 0;
}

LRESULT WeaselKeyboard::OnTouch(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
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
      _PointerDown(client_point(ti));
    } else if (m_touch_active && ti.dwID == m_touch_id) {
      if (ti.dwFlags & TOUCHEVENTF_UP) {
        m_touch_active = false;
        _PointerUp(client_point(ti));
      } else if (ti.dwFlags & TOUCHEVENTF_MOVE) {
        _PointerMove();
      }
    }
  }
  ::CloseTouchInputHandle(handle);
  return 0;
}

void WeaselKeyboard::_PointerDown(CPoint pt) {
  if (m_grip_rect.PtInRect(pt)) {
    m_resizing = true;
    SetCapture();
    ::GetCursorPos(&m_drag_start);
    m_drag_start_h = m_height96;
    return;
  }
  SetCapture();
  m_pressed_dismiss = (m_dismiss_rect.PtInRect(pt) != FALSE);
  if (m_pressed_dismiss)
    return;
  m_pressed_tool = _ToolAt(pt);
  if (m_pressed_tool >= 0) {
    _Render();
    return;
  }
  m_pressed_candidate = _CandidateAt(pt);
  if (m_pressed_candidate < 0) {
    m_pressed_key = _KeyAt(pt);
    if (m_pressed_key >= 0 &&
        _DefById(m_keys[m_pressed_key].id).action == KEY_SPACE) {
      m_longpress_timer = SetTimer(TIMER_LONGPRESS, 350);
    }
  }
  _Render();
}

void WeaselKeyboard::_PointerUp(CPoint pt) {
  if (GetCapture() == m_hWnd)
    ReleaseCapture();
  if (m_resizing) {
    m_resizing = false;
    _SaveHeight(m_height96);
    _Render();
    return;
  }
  if (m_longpress_timer) {
    KillTimer(TIMER_LONGPRESS);
    m_longpress_timer = 0;
  }
  if (m_pressed_dismiss) {
    if (m_dismiss_rect.PtInRect(pt)) {
      if (m_dismiss_handler)
        m_dismiss_handler();
      else
        Hide();
    }
    m_pressed_dismiss = false;
    _Render();
    return;
  }
  if (m_pressed_tool >= 0) {
    if (_ToolAt(pt) == m_pressed_tool && m_tool_handler) {
      m_tool_handler(m_pressed_tool);
    }
    m_pressed_tool = -1;
    _Render();
    return;
  }
  if (m_voice_active) {
    m_voice_active = false;
    {
      std::lock_guard<std::mutex> lock(m_data_mutex);
      m_hint = L"语音输入开发中，即将上线";
    }
    if (m_hint_timer)
      KillTimer(TIMER_HINT);
    m_hint_timer = SetTimer(TIMER_HINT, 1600);
  } else if (m_pressed_candidate >= 0) {
    const int candidate = _CandidateAt(pt);
    if (candidate == m_pressed_candidate && m_select_handler) {
      m_select_handler((size_t)candidate);
    }
  } else if (m_pressed_key >= 0) {
    if (_KeyAt(pt) == m_pressed_key) {
      _ActivateKey(m_pressed_key);
    }
  }
  m_pressed_key = -1;
  m_pressed_candidate = -1;
  _Render();
}

void WeaselKeyboard::_PointerMove() {
  if (!m_resizing)
    return;
  CPoint cur;
  ::GetCursorPos(&cur);
  const int delta_phys = m_drag_start.y - cur.y;
  const int delta96 = (int)std::lround(delta_phys * 96.0 / (double)m_dpi);
  int height = m_drag_start_h + delta96;
  if (height < kMinHeight96)
    height = kMinHeight96;
  else if (height > kMaxHeight96)
    height = kMaxHeight96;
  if (height != m_height96) {
    m_height96 = height;
    Reposition();
  }
}

LRESULT WeaselKeyboard::OnSetCursor(UINT, WPARAM, LPARAM, BOOL&) {
  CPoint pt;
  ::GetCursorPos(&pt);
  ::ScreenToClient(m_hWnd, &pt);
  if (m_resizing || m_grip_rect.PtInRect(pt)) {
    ::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
  } else {
    ::SetCursor(::LoadCursor(NULL, IDC_ARROW));
  }
  return TRUE;
}

int WeaselKeyboard::_Scaled(int value) const {
  return (int)std::lround(value * m_dpi / 96.0);
}

void WeaselKeyboard::_UpdateDpi() {
  HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
  UINT dpi_x = 96;
  UINT dpi_y = 96;
  if (monitor) {
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
  }
  m_dpi = dpi_x ? dpi_x : 96;
}

void WeaselKeyboard::_UpdateFonts() {
  m_font_key.reset();
  m_font_fn.reset();
  m_font_candidate.reset();
  m_font_preedit.reset();
  m_font_hint.reset();
  m_font_family.reset();
  m_font_family = std::make_unique<Gdiplus::FontFamily>(kFontFace);
  if (!m_font_family->IsAvailable()) {
    m_font_family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
  }
  if (!m_font_family->IsAvailable()) {
    m_font_family = std::make_unique<Gdiplus::FontFamily>(L"Arial");
  }
  const Gdiplus::REAL base = (Gdiplus::REAL)m_dpi / 96.0f;
  m_font_key = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 20.0f * base, Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
  m_font_fn = std::make_unique<Gdiplus::Font>(m_font_family.get(), 15.0f * base,
                                              Gdiplus::FontStyleRegular,
                                              Gdiplus::UnitPixel);
  m_font_candidate = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 16.0f * base, Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
  m_font_preedit = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 15.0f * base, Gdiplus::FontStyleBold,
      Gdiplus::UnitPixel);
  m_font_hint = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 13.0f * base, Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
}

void WeaselKeyboard::_Layout() {
  CRect rc;
  GetClientRect(&rc);
  m_keys.clear();
  m_toolbar_rects.clear();
  m_grip_h = _Scaled(14);
  m_strip_h = _Scaled(48);
  m_toolbar_h = _Scaled(40);
  m_grip_rect = CRect(rc.left, rc.top, rc.right, rc.top + m_grip_h);
  const int pad = _Scaled(8);
  const int gap = _Scaled(8);
  // Dismiss key sits at the left edge of the candidate strip, like the
  // phone keyboards put it.
  const int dismiss_w = _Scaled(40);
  m_dismiss_rect = CRect(pad, m_grip_h + _Scaled(6), pad + dismiss_w,
                         m_grip_h + m_strip_h - _Scaled(6));
  if (rc.Width() <= 0 || rc.Height() <= 0)
    return;
  // Toolbar: three small icon buttons, left-aligned.
  const int tool_w = _Scaled(64);
  const int tool_h = _Scaled(30);
  const int tool_y = m_grip_h + m_strip_h + (m_toolbar_h - tool_h) / 2;
  int tool_x = pad;
  for (int t = 0; t < 3; ++t) {
    m_toolbar_rects.emplace_back(
        CRect(tool_x, tool_y, tool_x + tool_w, tool_y + tool_h), t);
    tool_x += tool_w + gap;
  }
  const int top = m_grip_h + m_strip_h + m_toolbar_h;
  const int row_count = (int)ARRAYSIZE(kRows);
  const int total_gap = gap * (row_count - 1);
  int row_h = (rc.Height() - top - pad * 2 - total_gap) / row_count;
  if (row_h < _Scaled(30))
    row_h = _Scaled(30);
  int y = top + pad;
  for (int r = 0; r < row_count; ++r) {
    const RowDef& row = kRows[r];
    float weight_sum = 0.0f;
    for (int i = 0; i < row.count; ++i)
      weight_sum += row.defs[i].weight;
    // Stagger the letter rows like a phone keyboard.
    int inset = 0;
    if (r == 1)
      inset = rc.Width() / 28;
    else if (r == 2)
      inset = rc.Width() / 14;
    const int avail = rc.Width() - (pad + inset) * 2 - gap * (row.count - 1);
    if (avail <= 0)
      return;
    int x = pad + inset;
    for (int i = 0; i < row.count; ++i) {
      int w = (i == row.count - 1)
                  ? (rc.Width() - pad - inset - x)
                  : (int)(avail * (row.defs[i].weight / weight_sum) + 0.5f);
      KeyLayout key;
      key.rect = CRect(x, y, x + w, y + row_h);
      key.id = (r << 8) | i;
      m_keys.push_back(key);
      x += w + gap;
    }
    y += row_h + gap;
  }
}

void WeaselKeyboard::_Render() {
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

void WeaselKeyboard::_EnsureBackBuffer(int width, int height) {
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

void WeaselKeyboard::_ReleaseBackBuffer() {
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

int WeaselKeyboard::_KeyAt(CPoint pt) const {
  for (size_t i = 0; i < m_keys.size(); ++i) {
    if (m_keys[i].rect.PtInRect(pt))
      return (int)i;
  }
  return -1;
}

int WeaselKeyboard::_CandidateAt(CPoint pt) const {
  for (const auto& entry : m_candidate_rects) {
    if (entry.first.PtInRect(pt))
      return (int)entry.second;
  }
  return -1;
}

int WeaselKeyboard::_ToolAt(CPoint pt) const {
  for (const auto& entry : m_toolbar_rects) {
    if (entry.first.PtInRect(pt))
      return entry.second;
  }
  return -1;
}

void WeaselKeyboard::ShowHint(const std::wstring& hint) {
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    m_hint = hint;
  }
  if (m_hWnd) {
    if (m_hint_timer)
      ::KillTimer(m_hWnd, TIMER_HINT);
    m_hint_timer = ::SetTimer(m_hWnd, TIMER_HINT, 1800, NULL);
    _Render();
  }
}

bool WeaselKeyboard::_CurrentAsciiMode() const {
  std::lock_guard<std::mutex> lock(m_data_mutex);
  return m_status.ascii_mode;
}

void WeaselKeyboard::_ActivateKey(int key_index) {
  if (key_index < 0 || key_index >= (int)m_keys.size())
    return;
  const KeyDef& def = _DefById(m_keys[key_index].id);
  switch (def.action) {
    case KEY_SHIFT:
      m_shift = !m_shift;
      return;
    case KEY_TOGGLE_MODE:
      if (m_mode_toggle_handler)
        m_mode_toggle_handler(_CurrentAsciiMode());
      return;
    default:
      break;
  }
  weasel::KeyEvent event(def.ibus_code, 0);
  const bool is_alpha = def.ibus_code >= 'a' && def.ibus_code <= 'z';
  const bool need_shift = (is_alpha && m_shift);
  if (need_shift) {
    event.mask |= ibus::SHIFT_MASK;
    if (is_alpha)
      event.keycode = def.ibus_code - 'a' + 'A';
  }
  const bool handled = m_key_handler && m_key_handler(event);
  if (!handled && def.vk) {
    _InjectKey(def, need_shift);
  }
  if (is_alpha && m_shift)
    m_shift = false;
}

void WeaselKeyboard::_InjectKey(const KeyDef& def, bool shift) {
  if (!def.vk)
    return;
  std::vector<INPUT> inputs;
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  if (shift) {
    input.ki.wVk = VK_SHIFT;
    inputs.push_back(input);
  }
  input.ki.wVk = (WORD)def.vk;
  input.ki.dwFlags = 0;
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

void WeaselKeyboard::SendCtrlKey(WORD vk) {
  std::vector<INPUT> inputs(4);
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = VK_CONTROL;
  inputs[0] = input;
  input.ki.wVk = vk;
  inputs[1] = input;
  input.ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[2] = input;
  input.ki.wVk = VK_CONTROL;
  inputs[3] = input;
  ::SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
}

void WeaselKeyboard::_Draw(Gdiplus::Graphics& g, const CRect& rc) {
  weasel::Context ctx;
  weasel::Status status;
  std::wstring hint;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    ctx = m_ctx;
    status = m_status;
    hint = m_hint;
  }

  const int pad = _Scaled(8);
  const int gap = _Scaled(8);
  const int strip_top = m_grip_h;
  const float width = (float)rc.Width();
  const float height = (float)rc.Height();
  const float key_radius = (float)_Scaled(10);
  const float chip_radius = (float)_Scaled(9);
  const float dpi_scale = (float)m_dpi / 96.0f;

  // Window background with rounded top corners.
  {
    Gdiplus::GraphicsPath path;
    const float d = (float)_Scaled(16) * 2.0f;
    path.StartFigure();
    path.AddArc(0.0f, 0.0f, d, d, 180.0f, 90.0f);
    path.AddArc(width - d, 0.0f, d, d, 270.0f, 90.0f);
    path.AddLine(width, d / 2.0f, width, height);
    path.AddLine(width, height, 0.0f, height);
    path.AddLine(0.0f, height, 0.0f, d / 2.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(kColorBg);
    g.FillPath(&brush, &path);
  }

  // Resize grip pill.
  {
    const float pill_w = (float)_Scaled(44);
    const float pill_h = (float)_Scaled(4);
    _FillRoundedRect(
        g,
        Gdiplus::RectF((width - pill_w) / 2.0f,
                       ((float)m_grip_h - pill_h) / 2.0f, pill_w, pill_h),
        pill_h / 2.0f, m_resizing ? kColorAccent : kColorGrip);
  }

  // Candidate strip.
  {
    Gdiplus::SolidBrush strip_brush(kColorStrip);
    g.FillRectangle(&strip_brush, Gdiplus::RectF(0.0f, (float)strip_top, width,
                                                 (float)m_strip_h));
    Gdiplus::SolidBrush line_brush(kColorLine);
    g.FillRectangle(
        &line_brush,
        Gdiplus::RectF(0.0f, (float)(strip_top + m_strip_h - 1), width, 1.0f));
  }

  // Dismiss key (hide the keyboard), left edge of the candidate strip.
  {
    const Gdiplus::RectF rect(
        (float)m_dismiss_rect.left, (float)m_dismiss_rect.top,
        (float)m_dismiss_rect.Width(), (float)m_dismiss_rect.Height());
    _FillRoundedRect(g, rect, chip_radius,
                     m_pressed_dismiss ? kColorKeyPressed : kColorKeyFn);
    const float cx = rect.X + rect.Width / 2.0f;
    const float cy = rect.Y + rect.Height / 2.0f;
    const float s = 6.0f * dpi_scale;
    Gdiplus::Pen pen(kColorSubText, 2.0f * dpi_scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::GraphicsPath path;
    path.AddLine(cx - s, cy - s * 0.35f, cx, cy + s * 0.45f);
    path.AddLine(cx, cy + s * 0.45f, cx + s, cy - s * 0.35f);
    g.DrawPath(&pen, &path);
  }

  // Toolbar strip: small icon buttons between the candidate strip and the
  // letter rows (clipboard / edit / voice), like the phone keyboards.
  {
    const int band_top = m_grip_h + m_strip_h;
    Gdiplus::SolidBrush band_brush(kColorStrip);
    g.FillRectangle(&band_brush, Gdiplus::RectF(0.0f, (float)band_top, width,
                                                (float)m_toolbar_h));
    Gdiplus::SolidBrush line_brush(kColorLine);
    g.FillRectangle(
        &line_brush,
        Gdiplus::RectF(0.0f, (float)(band_top + m_toolbar_h - 1), width, 1.0f));
    for (const auto& entry : m_toolbar_rects) {
      const int tool = entry.second;
      const bool pressed = (tool == m_pressed_tool);
      const Gdiplus::RectF rect((float)entry.first.left, (float)entry.first.top,
                                (float)entry.first.Width(),
                                (float)entry.first.Height());
      _FillRoundedRect(g, rect, chip_radius,
                       pressed ? kColorKeyPressed : kColorKeyFn);
      const float cx = rect.X + rect.Width / 2.0f;
      const float cy = rect.Y + rect.Height / 2.0f;
      const float s = 7.0f * dpi_scale;
      Gdiplus::Pen pen(pressed ? kColorAccent : kColorSubText,
                       1.8f * dpi_scale);
      pen.SetStartCap(Gdiplus::LineCapRound);
      pen.SetEndCap(Gdiplus::LineCapRound);
      pen.SetLineJoin(Gdiplus::LineJoinRound);
      Gdiplus::GraphicsPath path;
      switch (tool) {
        case TOOL_CLIPBOARD: {  // clipboard outline with a clip on top
          path.AddRectangle(Gdiplus::RectF(cx - s * 0.7f, cy - s * 0.4f,
                                           s * 1.4f, s * 1.35f));
          path.StartFigure();
          path.AddLine(cx - s * 0.3f, cy - s * 0.85f, cx + s * 0.3f,
                       cy - s * 0.85f);
          path.AddLine(cx + s * 0.3f, cy - s * 0.85f, cx + s * 0.3f,
                       cy - s * 0.45f);
          path.AddLine(cx - s * 0.3f, cy - s * 0.45f, cx - s * 0.3f,
                       cy - s * 0.85f);
          break;
        }
        case TOOL_EDIT: {  // four-way arrow cluster
          path.AddLine(cx, cy - s, cx + s * 0.9f, cy);
          path.AddLine(cx + s * 0.9f, cy, cx, cy + s);
          path.AddLine(cx, cy + s, cx - s * 0.9f, cy);
          path.AddLine(cx - s * 0.9f, cy, cx, cy - s);
          break;
        }
        case TOOL_VOICE: {  // microphone
          path.AddArc(
              Gdiplus::RectF(cx - s * 0.45f, cy - s, s * 0.9f, s * 0.9f),
              180.0f, 180.0f);
          path.AddLine(cx - s * 0.45f, cy - s * 0.55f, cx - s * 0.45f,
                       cy - s * 0.35f);
          path.AddLine(cx - s * 0.45f, cy - s * 0.35f, cx - s * 0.85f,
                       cy - s * 0.1f);
          path.AddLine(cx - s * 0.85f, cy + s * 0.75f, cx + s * 0.85f,
                       cy + s * 0.75f);
          path.AddLine(cx + s * 0.85f, cy - s * 0.1f, cx + s * 0.45f,
                       cy - s * 0.35f);
          path.AddLine(cx + s * 0.45f, cy - s * 0.35f, cx + s * 0.45f,
                       cy - s * 0.55f);
          break;
        }
        default:
          break;
      }
      g.DrawPath(&pen, &path);
    }
  }

  // Composition and candidates.
  m_candidate_rects.clear();
  {
    float x = (float)(m_dismiss_rect.right + gap);
    const float area_right = width - (float)pad;
    const float chip_h = (float)_Scaled(34);
    const float chip_y = (float)strip_top + ((float)m_strip_h - chip_h) / 2.0f;

    if (!ctx.preedit.str.empty()) {
      const float w = _MeasureText(g, ctx.preedit.str, m_font_preedit.get());
      _DrawTextInRect(
          g, ctx.preedit.str, m_font_preedit.get(),
          Gdiplus::RectF(x, (float)strip_top, w + 4.0f, (float)m_strip_h),
          kColorAccent, Gdiplus::StringAlignmentNear);
      x += w + (float)gap * 2;
    }

    if (m_voice_active) {
      _DrawTextInRect(
          g, L"聆听中… 松开空格结束", m_font_hint.get(),
          Gdiplus::RectF(x, (float)strip_top, area_right - x, (float)m_strip_h),
          kColorAccent, Gdiplus::StringAlignmentNear);
    } else {
      for (size_t i = 0; i < ctx.cinfo.candies.size(); ++i) {
        const std::wstring text = unescape_string(ctx.cinfo.candies[i].str);
        if (text.empty())
          continue;
        const float w =
            _MeasureText(g, text, m_font_candidate.get()) + (float)_Scaled(24);
        if (x + w > area_right)
          break;
        const bool active = ((int)i == ctx.cinfo.highlighted) || (i == 0);
        const bool pressed = ((int)i == m_pressed_candidate);
        if (active || pressed) {
          _FillRoundedRect(g, Gdiplus::RectF(x, chip_y, w, chip_h), chip_radius,
                           pressed ? kColorAccent : kColorAccentSoft);
        }
        _DrawTextInRect(g, text, m_font_candidate.get(),
                        Gdiplus::RectF(x, chip_y, w, chip_h),
                        (active || pressed) ? kColorAccent : kColorText);
        m_candidate_rects.emplace_back(
            CRect((int)x, (int)chip_y, (int)(x + w), (int)(chip_y + chip_h)),
            i);
        x += w + (float)_Scaled(8);
      }

      if (ctx.preedit.str.empty() && ctx.cinfo.candies.empty()) {
        std::wstring text;
        Gdiplus::Color color = kColorSubText;
        if (!hint.empty()) {
          text = hint;
          color = kColorAccent;
        } else if (status.disabled) {
          text = L"部署中…";
        } else {
          text = L"按住空格说话";
        }
        _DrawTextInRect(g, text, m_font_hint.get(),
                        Gdiplus::RectF(x, (float)strip_top, area_right - x,
                                       (float)m_strip_h),
                        color, Gdiplus::StringAlignmentNear);
      }
    }
  }

  // Keys.
  for (size_t i = 0; i < m_keys.size(); ++i) {
    const KeyLayout& key = m_keys[i];
    const KeyDef& def = _DefById(key.id);
    const bool pressed = ((int)i == m_pressed_key);
    const bool voice_space = (def.action == KEY_SPACE && m_voice_active);
    const bool shift_on = (def.action == KEY_SHIFT && m_shift);
    const Gdiplus::RectF rect((float)key.rect.left, (float)key.rect.top,
                              (float)key.rect.Width(),
                              (float)key.rect.Height());
    Gdiplus::Color bg = def.is_fn ? kColorKeyFn : kColorKey;
    Gdiplus::Color fg = kColorText;
    if (voice_space) {
      bg = kColorAccent;
      fg = kColorWhite;
    } else if (shift_on) {
      bg = kColorAccentSoft;
      fg = kColorAccent;
    } else if (pressed) {
      bg = kColorKeyPressed;
    }
    _FillRoundedRect(g, rect, key_radius, bg);
    if (!def.is_fn && !pressed && !voice_space) {
      Gdiplus::GraphicsPath border_path;
      _AddRoundedRectPath(border_path, rect, key_radius);
      int border_width = _Scaled(1);
      if (border_width < 1)
        border_width = 1;
      Gdiplus::Pen border_pen(kColorKeyBorder, (Gdiplus::REAL)border_width);
      g.DrawPath(&border_pen, &border_path);
    }

    std::wstring label = def.label;
    if (def.action == KEY_TOGGLE_MODE)
      label = status.ascii_mode ? L"英" : L"中";
    else if (def.action == KEY_SPACE)
      label = m_voice_active ? L"松开结束" : L"按住说话";
    const bool single_letter =
        (def.action == KEY_NORMAL && !def.is_fn && def.label[1] == 0);
    _DrawTextInRect(
        g, label, single_letter ? m_font_key.get() : m_font_fn.get(), rect, fg);
  }
}
