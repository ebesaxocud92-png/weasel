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

const Gdiplus::Color kColorBg(255, 238, 240, 246);
const Gdiplus::Color kColorStrip(255, 255, 255, 255);
const Gdiplus::Color kColorLine(255, 228, 231, 238);
const Gdiplus::Color kColorKey(255, 255, 255, 255);
const Gdiplus::Color kColorKeyFn(255, 216, 219, 227);
const Gdiplus::Color kColorKeyPressed(255, 201, 206, 216);
const Gdiplus::Color kColorText(255, 26, 26, 26);
const Gdiplus::Color kColorSubText(255, 138, 143, 153);
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
             WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, 0U) != NULL;
}

void WeaselKeyboard::Show() {
  if (!IsWindow())
    return;
  Reposition();
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
  HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi;
  mi.cbSize = sizeof(MONITORINFO);
  if (!monitor || !GetMonitorInfo(monitor, &mi))
    return;
  const int width = mi.rcWork.right - mi.rcWork.left;
  const int height = _Scaled(284);
  SetWindowPos(NULL, mi.rcWork.left, mi.rcWork.bottom - height, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
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
  _UpdateFonts();
  _Layout();
  Reposition();
  return 0;
}

LRESULT WeaselKeyboard::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
  KillTimer(TIMER_LONGPRESS);
  KillTimer(TIMER_HINT);
  m_longpress_timer = 0;
  m_hint_timer = 0;
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
  _Repaint();
  return 0;
}

LRESULT WeaselKeyboard::OnDisplayChange(UINT, WPARAM, LPARAM, BOOL&) {
  _UpdateDpi();
  _UpdateFonts();
  _Layout();
  if (IsVisible())
    Reposition();
  _Repaint();
  return 0;
}

LRESULT WeaselKeyboard::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
  _UpdateDpi();
  _UpdateFonts();
  _Layout();
  _Repaint();
  return 0;
}

LRESULT WeaselKeyboard::OnSettingChange(UINT, WPARAM, LPARAM, BOOL&) {
  if (IsVisible())
    Reposition();
  return 0;
}

LRESULT WeaselKeyboard::OnContextUpdateMessage(UINT, WPARAM, LPARAM, BOOL&) {
  _Repaint();
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
      _Repaint();
    }
  } else if (wParam == TIMER_HINT) {
    KillTimer(TIMER_HINT);
    m_hint_timer = 0;
    {
      std::lock_guard<std::mutex> lock(m_data_mutex);
      m_hint.clear();
    }
    _Repaint();
  }
  return 0;
}

LRESULT WeaselKeyboard::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
  SetCapture();
  CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  m_pressed_candidate = _CandidateAt(pt);
  if (m_pressed_candidate < 0) {
    m_pressed_key = _KeyAt(pt);
    if (m_pressed_key >= 0 &&
        _DefById(m_keys[m_pressed_key].id).action == KEY_SPACE) {
      m_longpress_timer = SetTimer(TIMER_LONGPRESS, 350);
    } else if (m_pressed_key < 0) {
      m_pressed_mode = (m_mode_rect.PtInRect(pt) != FALSE);
    }
  }
  _Repaint();
  return 0;
}

LRESULT WeaselKeyboard::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
  if (GetCapture() == m_hWnd)
    ReleaseCapture();
  if (m_longpress_timer) {
    KillTimer(TIMER_LONGPRESS);
    m_longpress_timer = 0;
  }
  CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  if (m_voice_active) {
    m_voice_active = false;
    {
      std::lock_guard<std::mutex> lock(m_data_mutex);
      m_hint = L"语音输入开发中，敬请期待";
    }
    if (m_hint_timer)
      KillTimer(TIMER_HINT);
    m_hint_timer = SetTimer(TIMER_HINT, 1600);
  } else if (m_pressed_candidate >= 0) {
    const int candidate = _CandidateAt(pt);
    if (candidate == m_pressed_candidate && m_select_handler) {
      m_select_handler((size_t)candidate);
    }
  } else if (m_pressed_mode) {
    if (m_mode_rect.PtInRect(pt) && m_mode_toggle_handler) {
      m_mode_toggle_handler(_CurrentAsciiMode());
    }
  } else if (m_pressed_key >= 0) {
    if (_KeyAt(pt) == m_pressed_key) {
      _ActivateKey(m_pressed_key);
    }
  }
  m_pressed_key = -1;
  m_pressed_candidate = -1;
  m_pressed_mode = false;
  _Repaint();
  return 0;
}

void WeaselKeyboard::DoPaint(CDCHandle dc) {
  CRect rc;
  GetClientRect(&rc);
  if (rc.IsRectEmpty())
    return;
  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  _Draw(g, rc);
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
      m_font_family.get(), 19.0f * base, Gdiplus::FontStyleRegular,
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
  if (rc.Width() <= 0 || rc.Height() <= 0)
    return;
  const int pad = _Scaled(6);
  const int gap = _Scaled(6);
  const int row_h = _Scaled(52);
  const int strip_h = _Scaled(46);
  int y = strip_h + pad;
  for (int r = 0; r < ARRAYSIZE(kRows); ++r) {
    const RowDef& row = kRows[r];
    float weight_sum = 0.0f;
    for (int i = 0; i < row.count; ++i)
      weight_sum += row.defs[i].weight;
    const int avail = rc.Width() - pad * 2 - gap * (row.count - 1);
    if (avail <= 0)
      return;
    int x = pad;
    for (int i = 0; i < row.count; ++i) {
      int w = (i == row.count - 1)
                  ? (rc.Width() - pad - x)
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

void WeaselKeyboard::_Repaint() {
  if (IsWindow())
    Invalidate(FALSE);
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
  const bool shifted = is_alpha && m_shift;
  if (shifted) {
    event.keycode = def.ibus_code - 'a' + 'A';
    event.mask |= ibus::SHIFT_MASK;
  }
  const bool handled = m_key_handler && m_key_handler(event);
  if (!handled && def.vk) {
    _InjectKey(def, shifted);
  }
  if (shifted)
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
    inputs.push_back(input);
  }
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

  const int pad = _Scaled(6);
  const int gap = _Scaled(6);
  const int strip_h = _Scaled(46);
  const float radius = (float)_Scaled(8);
  const float window_radius = (float)_Scaled(14);
  const float width = (float)rc.Width();
  const float height = (float)rc.Height();

  // window background, rounded top corners
  {
    Gdiplus::GraphicsPath path;
    const float d = window_radius * 2.0f;
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

  // candidate strip
  {
    Gdiplus::SolidBrush strip_brush(kColorStrip);
    g.FillRectangle(&strip_brush,
                    Gdiplus::RectF(0.0f, 0.0f, width, (float)strip_h));
    Gdiplus::SolidBrush line_brush(kColorLine);
    g.FillRectangle(&line_brush,
                    Gdiplus::RectF(0.0f, (float)strip_h - 1.0f, width, 1.0f));
  }

  // mode chip
  {
    const int chip_w = _Scaled(40);
    const int chip_h = _Scaled(30);
    m_mode_rect = CRect(pad, (strip_h - chip_h) / 2, pad + chip_w,
                        (strip_h + chip_h) / 2);
    const Gdiplus::RectF rect((float)m_mode_rect.left, (float)m_mode_rect.top,
                              (float)chip_w, (float)chip_h);
    _FillRoundedRect(g, rect, radius,
                     m_pressed_mode ? kColorAccent : kColorAccentSoft);
    _DrawTextInRect(g, status.ascii_mode ? L"英" : L"中", m_font_fn.get(), rect,
                    m_pressed_mode ? kColorWhite : kColorAccent);
  }

  // composition and candidates
  m_candidate_rects.clear();
  {
    float x = (float)(m_mode_rect.right + gap * 2);
    const float area_right = width - (float)pad;
    const float chip_h = (float)_Scaled(32);
    const float chip_y = ((float)strip_h - chip_h) / 2.0f;

    if (!ctx.preedit.str.empty()) {
      const float w = _MeasureText(g, ctx.preedit.str, m_font_preedit.get());
      _DrawTextInRect(g, ctx.preedit.str, m_font_preedit.get(),
                      Gdiplus::RectF(x, 0.0f, w + 4.0f, (float)strip_h),
                      kColorAccent, Gdiplus::StringAlignmentNear);
      x += w + (float)gap * 2;
    }

    if (m_voice_active) {
      _DrawTextInRect(g, L"聆听中… 松开空格结束", m_font_hint.get(),
                      Gdiplus::RectF(x, 0.0f, area_right - x, (float)strip_h),
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
          _FillRoundedRect(g, Gdiplus::RectF(x, chip_y, w, chip_h), radius,
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
                        Gdiplus::RectF(x, 0.0f, area_right - x, (float)strip_h),
                        color, Gdiplus::StringAlignmentNear);
      }
    }
  }

  // keys
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
    _FillRoundedRect(g, rect, radius, bg);

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
