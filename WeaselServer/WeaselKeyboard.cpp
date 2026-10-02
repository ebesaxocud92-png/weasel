#include "stdafx.h"
#include "WeaselKeyboard.h"

#include <ShellScalingApi.h>
#include <WeaselUtility.h>

#include <cmath>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

const Gdiplus::Color kColorBg(255, 240, 242, 247);
const Gdiplus::Color kColorStrip(255, 255, 255, 255);
const Gdiplus::Color kColorLine(255, 228, 231, 238);
const Gdiplus::Color kColorCard(255, 255, 255, 255);
const Gdiplus::Color kColorKey(255, 255, 255, 255);
const Gdiplus::Color kColorKeyBorder(40, 0, 0, 0);
const Gdiplus::Color kColorKeyFn(255, 227, 230, 237);
const Gdiplus::Color kColorKeyPressed(255, 205, 210, 220);
const Gdiplus::Color kColorText(255, 26, 26, 26);
const Gdiplus::Color kColorSubText(255, 138, 143, 153);
const Gdiplus::Color kColorGrip(64, 0, 0, 0);
const Gdiplus::Color kColorAccent(255, 52, 120, 246);
const Gdiplus::Color kColorAccentSoft(255, 230, 239, 255);
const Gdiplus::Color kColorWhite(255, 255, 255, 255);

const wchar_t kFontFace[] = L"Microsoft YaHei UI";

// Keyboard height (logical 96 dpi units), persisted across restarts.
constexpr int kDefaultHeight96 = 360;
constexpr int kMinHeight96 = 310;
constexpr int kMaxHeight96 = 620;
const wchar_t kRegPath[] = L"Software\\Rime\\Weasel";
const wchar_t kRegValueHeight[] = L"SoftKeyboardHeight";

constexpr size_t kMaxClipItems = 100;
constexpr size_t kMaxClipLength = 200;

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
    Gdiplus::StringAlignment align = Gdiplus::StringAlignmentCenter,
    Gdiplus::StringAlignment valign = Gdiplus::StringAlignmentCenter) {
  if (text.empty() || !font)
    return;
  Gdiplus::StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(valign);
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

void _DrawChevron(Gdiplus::Graphics& g,
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

void _DrawBackspaceIcon(Gdiplus::Graphics& g,
                        float cx,
                        float cy,
                        float s,
                        const Gdiplus::Color& color) {
  Gdiplus::Pen pen(color, s * 0.22f);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  const float x0 = cx - 0.95f * s;
  const float x1 = cx - 0.15f * s;
  const float x2 = cx + 0.95f * s;
  const float y0 = cy - 0.72f * s;
  const float y1 = cy + 0.72f * s;
  Gdiplus::GraphicsPath path;
  path.AddLine(x1, y0, x2, y0);
  path.AddLine(x2, y0, x2, y1);
  path.AddLine(x2, y1, x1, y1);
  path.AddLine(x1, y1, x0, cy);
  path.CloseFigure();
  g.DrawPath(&pen, &path);
  g.DrawLine(&pen, cx + 0.12f * s, cy - 0.28f * s, cx + 0.6f * s,
             cy + 0.28f * s);
  g.DrawLine(&pen, cx + 0.6f * s, cy - 0.28f * s, cx + 0.12f * s,
             cy + 0.28f * s);
}

void _DrawScissorsIcon(Gdiplus::Graphics& g,
                       float cx,
                       float cy,
                       float s,
                       const Gdiplus::Color& color) {
  Gdiplus::Pen pen(color, s * 0.2f);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  g.DrawEllipse(&pen, cx - 0.95f * s, cy - 1.0f * s, 0.52f * s, 0.52f * s);
  g.DrawEllipse(&pen, cx - 0.95f * s, cy + 0.48f * s, 0.52f * s, 0.52f * s);
  g.DrawLine(&pen, cx - 0.5f * s, cy - 0.55f * s, cx + 0.95f * s,
             cy + 0.8f * s);
  g.DrawLine(&pen, cx - 0.5f * s, cy + 0.55f * s, cx + 0.95f * s,
             cy - 0.8f * s);
}

void _DrawEditIcon(Gdiplus::Graphics& g,
                   float cx,
                   float cy,
                   float s,
                   const Gdiplus::Color& color) {
  Gdiplus::Pen pen(color, s * 0.18f);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  g.DrawLine(&pen, cx, cy - 0.72f * s, cx, cy + 0.72f * s);
  g.DrawLine(&pen, cx - 0.26f * s, cy - 0.72f * s, cx + 0.26f * s,
             cy - 0.72f * s);
  g.DrawLine(&pen, cx - 0.26f * s, cy + 0.72f * s, cx + 0.26f * s,
             cy + 0.72f * s);
  g.DrawLine(&pen, cx - 0.62f * s, cy - 0.44f * s, cx - 0.92f * s, cy);
  g.DrawLine(&pen, cx - 0.92f * s, cy, cx - 0.62f * s, cy + 0.44f * s);
  g.DrawLine(&pen, cx + 0.62f * s, cy - 0.44f * s, cx + 0.92f * s, cy);
  g.DrawLine(&pen, cx + 0.92f * s, cy, cx + 0.62f * s, cy + 0.44f * s);
}

void _DrawChevronDown(Gdiplus::Graphics& g,
                      float cx,
                      float cy,
                      float s,
                      const Gdiplus::Color& color) {
  Gdiplus::Pen pen(color, s * 0.2f);
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  g.DrawLine(&pen, cx - 0.7f * s, cy - 0.35f * s, cx, cy + 0.35f * s);
  g.DrawLine(&pen, cx, cy + 0.35f * s, cx + 0.7f * s, cy - 0.35f * s);
}

// Key definitions -----------------------------------------------------------

KeyDef K(const wchar_t* label,
         UINT code,
         UINT vk,
         int action,
         float weight = 1.0f,
         bool is_fn = false,
         const wchar_t* hint_left = nullptr,
         const wchar_t* hint_right = nullptr,
         bool is_primary = false,
         int payload = 0) {
  KeyDef def;
  def.label = label;
  def.ibus_code = code;
  def.vk = vk;
  def.action = action;
  def.weight = weight;
  def.is_fn = is_fn;
  def.hint_left = hint_left;
  def.hint_right = hint_right;
  def.is_primary = is_primary;
  def.payload = payload;
  return def;
}

// A symbol key: no engine involvement, typed directly as unicode text.
KeyDef KS(const wchar_t* symbol) {
  KeyDef def = K(symbol, 0, 0, KEY_NORMAL);
  def.text = symbol;
  return def;
}

const KeyDef kLettersR1[10] = {
    K(L"q", 'q', 'Q', KEY_NORMAL, 1, false, L"1"),
    K(L"w", 'w', 'W', KEY_NORMAL, 1, false, L"2"),
    K(L"e", 'e', 'E', KEY_NORMAL, 1, false, L"3"),
    K(L"r", 'r', 'R', KEY_NORMAL, 1, false, L"4"),
    K(L"t", 't', 'T', KEY_NORMAL, 1, false, L"5"),
    K(L"y", 'y', 'Y', KEY_NORMAL, 1, false, L"6"),
    K(L"u", 'u', 'U', KEY_NORMAL, 1, false, L"7"),
    K(L"i", 'i', 'I', KEY_NORMAL, 1, false, L"8"),
    K(L"o", 'o', 'O', KEY_NORMAL, 1, false, L"9"),
    K(L"p", 'p', 'P', KEY_NORMAL, 1, false, L"0"),
};

const KeyDef kLettersR2[9] = {
    K(L"a", 'a', 'A', KEY_NORMAL, 1, false, nullptr, L"~"),
    K(L"s", 's', 'S', KEY_NORMAL, 1, false, nullptr, L"!"),
    K(L"d", 'd', 'D', KEY_NORMAL, 1, false, nullptr, L"@"),
    K(L"f", 'f', 'F', KEY_NORMAL, 1, false, nullptr, L"/"),
    K(L"g", 'g', 'G', KEY_NORMAL, 1, false, nullptr, L"%"),
    K(L"h", 'h', 'H', KEY_NORMAL, 1, false, nullptr, L"\""),
    K(L"j", 'j', 'J', KEY_NORMAL, 1, false, nullptr, L"\""),
    K(L"k", 'k', 'K', KEY_NORMAL, 1, false, nullptr, L"*"),
    K(L"l", 'l', 'L', KEY_NORMAL, 1, false, nullptr, L"?"),
};

const KeyDef kLettersR3[9] = {
    K(L"ab", 0, 0, KEY_SHIFT, 1.5f, true),
    K(L"z", 'z', 'Z', KEY_NORMAL, 1, false, nullptr, L"("),
    K(L"x", 'x', 'X', KEY_NORMAL, 1, false, nullptr, L")"),
    K(L"c", 'c', 'C', KEY_NORMAL, 1, false, nullptr, L"-"),
    K(L"v", 'v', 'V', KEY_NORMAL, 1, false, nullptr, L"_"),
    K(L"b", 'b', 'B', KEY_NORMAL, 1, false, nullptr, L":"),
    K(L"n", 'n', 'N', KEY_NORMAL, 1, false, nullptr, L";"),
    K(L"m", 'm', 'M', KEY_NORMAL, 1, false, nullptr, L"/"),
    K(L"", 0, VK_BACK, KEY_BACKSPACE, 1.5f, true),
};

const KeyDef kLettersR4[7] = {
    K(L"符",
      0,
      0,
      KEY_PAGE,
      1.0f,
      true,
      nullptr,
      nullptr,
      false,
      WeaselKeyboard::PAGE_SYMBOLS),
    K(L"123",
      0,
      0,
      KEY_PAGE,
      1.3f,
      true,
      nullptr,
      nullptr,
      false,
      WeaselKeyboard::PAGE_NUMBERS),
    K(L"，", 0x2C, VK_OEM_COMMA, KEY_NORMAL, 0.9f, false),
    K(L"按住说话", 0x20, VK_SPACE, KEY_SPACE, 4.2f, false),
    K(L"。", 0x2E, VK_OEM_PERIOD, KEY_NORMAL, 0.9f, false),
    K(L"中/英", 0, 0, KEY_MODE, 1.3f, true),
    K(L"前往", 0, VK_RETURN, KEY_ENTER, 1.5f, true),
};

const KeyDef kEditR1[5] = {
    K(L"Tab", 0, 0, KEY_TAB, 1, true),
    K(L"复制", 0, 0, KEY_COPY, 1, true),
    K(L"", ibus::Up, VK_UP, KEY_ARROW, 1, true),
    K(L"粘贴", 0, 0, KEY_PASTE, 1, true),
    K(L"", 0, VK_BACK, KEY_BACKSPACE, 1, true),
};

const KeyDef kEditR2[5] = {
    K(L"开头", 0, 0, KEY_HOME, 1, true),
    K(L"", ibus::Left, VK_LEFT, KEY_ARROW, 1, true),
    K(L"选择", 0, 0, KEY_SELECT, 1, true),
    K(L"", ibus::Right, VK_RIGHT, KEY_ARROW, 1, true),
    K(L"Del", 0, 0, KEY_DELETE, 1, true),
};

const KeyDef kEditR3[5] = {
    K(L"末尾", 0, 0, KEY_END, 1, true),
    K(L"全选", 0, 0, KEY_SELECT_ALL, 1, true),
    K(L"", ibus::Down, VK_DOWN, KEY_ARROW, 1, true),
    K(L"剪切", 0, 0, KEY_CUT, 1, true),
    K(L"返回",
      0,
      0,
      KEY_PAGE,
      1,
      false,
      nullptr,
      nullptr,
      true,
      WeaselKeyboard::PAGE_LETTERS),
};

const wchar_t* const kSymbolCatNames[5] = {L"常用", L"中文", L"英文", L"数学",
                                           L"特殊"};

const wchar_t* const kSymbolTable[5][18] = {
    {L"，", L"。", L"？", L"！", L"：", L"；", L"、", L"／", L"（", L"）",
     L"＠", L"％", L"＋", L"《", L"》", L"－", L"＊", L"＃"},
    {L"，", L"。", L"、", L"；", L"：", L"？", L"！", L"“", L"”", L"‘", L"’",
     L"（", L"）", L"《", L"》", L"【", L"】", L"……"},
    {L"~", L"!", L"@", L"#", L"$", L"%", L"^", L"&", L"*", L"(", L")", L"-",
     L"_", L"=", L"+", L"[", L"]", L"\\"},
    {L"+", L"−", L"×", L"÷", L"=", L"≠", L"≈", L"≤", L"≥", L"±", L"∝", L"∞",
     L"√", L"∑", L"∫", L"π", L"°", L"‰"},
    {L"★", L"☆", L"♥", L"♦", L"♣", L"♠", L"→", L"←", L"↑", L"↓", L"￥", L"$",
     L"€", L"£", L"℃", L"℉", L"※", L"§"},
};

}  // namespace

// WeaselKeyboard ------------------------------------------------------------

WeaselKeyboard::WeaselKeyboard() {
  Gdiplus::GdiplusStartup(&m_gdiplus_token, &m_gdiplus_input, NULL);
}

WeaselKeyboard::~WeaselKeyboard() {
  if (IsWindow()) {
    KillTimer(TIMER_LONGPRESS);
    KillTimer(TIMER_HINT);
    KillTimer(TIMER_CLIPBOARD);
  }
  _ReleaseBackBuffer();
  m_font_key.reset();
  m_font_fn.reset();
  m_font_candidate.reset();
  m_font_preedit.reset();
  m_font_hint.reset();
  m_font_tiny.reset();
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
  m_page = PAGE_LETTERS;
  m_shift = false;
  m_select = false;
  Reposition();
  _Layout();
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
  SetTimer(TIMER_CLIPBOARD, 500);
  return 0;
}

LRESULT WeaselKeyboard::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
  KillTimer(TIMER_LONGPRESS);
  KillTimer(TIMER_HINT);
  KillTimer(TIMER_CLIPBOARD);
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
        m_defs[m_keys[m_pressed_key].def].action == KEY_SPACE &&
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
  } else if (wParam == TIMER_CLIPBOARD) {
    _PollClipboard();
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

void WeaselKeyboard::_PointerDown(CPoint pt) {
  if (m_grip_rect.PtInRect(pt)) {
    m_resizing = true;
    SetCapture();
    ::GetCursorPos(&m_drag_start);
    m_drag_start_h = m_height96;
    return;
  }
  SetCapture();
  m_pressed_candidate = _CandidateAt(pt);
  if (m_pressed_candidate < 0) {
    m_pressed_key = _KeyAt(pt);
    if (m_pressed_key >= 0 &&
        m_defs[m_keys[m_pressed_key].def].action == KEY_SPACE) {
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
  if (m_voice_active) {
    m_voice_active = false;
    ShowHint(L"语音输入开发中，即将上线");
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
  m_font_tiny.reset();
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
  m_font_tiny = std::make_unique<Gdiplus::Font>(
      m_font_family.get(), 11.0f * base, Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
}

void WeaselKeyboard::_Layout() {
  CRect rc;
  GetClientRect(&rc);
  m_keys.clear();
  m_defs.clear();
  m_defs.reserve(512);
  m_grip_h = _Scaled(14);
  m_strip_h = _Scaled(48);
  m_toolbar_h = _Scaled(44);
  m_grip_rect = CRect(rc.left, rc.top, rc.right, rc.top + m_grip_h);
  if (rc.Width() <= 0 || rc.Height() <= 0)
    return;
  const int pad = _Scaled(6);
  const int gap = _Scaled(7);
  auto put = [&](int x, int y, int w, int h, const KeyDef& def) {
    if (w < 1 || h < 1)
      return;
    m_defs.push_back(def);
    KeyLayout kl;
    kl.rect = CRect(x, y, x + w, y + h);
    kl.def = (int)(m_defs.size() - 1);
    m_keys.push_back(kl);
  };
  auto row = [&](const KeyDef* defs, int n, int x0, int w_total, int y, int h) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
      sum += defs[i].weight;
    const int avail = w_total - gap * (n - 1);
    if (avail <= 0)
      return;
    int x = x0;
    for (int i = 0; i < n; ++i) {
      int kw = (i == n - 1) ? (x0 + w_total - x)
                            : (int)(avail * (defs[i].weight / sum) + 0.5f);
      put(x, y, kw, h, defs[i]);
      x += kw + gap;
    }
  };

  // Toolbar row: clipboard / edit chips on the left, dismiss on the right.
  const int toolbar_top = m_grip_h + m_strip_h;
  {
    const int bh = m_toolbar_h - _Scaled(10);
    const int by = toolbar_top + _Scaled(5);
    const int bw = _Scaled(84);
    put(pad, by, bw, bh, K(L"", 0, 0, KEY_TOOL_CLIPBOARD));
    put(pad + bw + gap, by, bw, bh, K(L"", 0, 0, KEY_TOOL_EDIT));
    const int dw = _Scaled(56);
    put(rc.right - pad - dw, by, dw, bh, K(L"", 0, 0, KEY_DISMISS, 1, true));
  }

  const int top = toolbar_top + m_toolbar_h;
  const int full_w = rc.Width() - pad * 2;

  switch (m_page) {
    case PAGE_LETTERS: {
      int rh = (rc.Height() - top - pad * 2 - gap * 3) / 4;
      if (rh < _Scaled(32))
        rh = _Scaled(32);
      int y = top + pad;
      row(kLettersR1, 10, pad, full_w, y, rh);
      y += rh + gap;
      {
        const int inset = rc.Width() / 28;
        row(kLettersR2, 9, pad + inset, full_w - inset * 2, y, rh);
      }
      y += rh + gap;
      {
        const int inset = rc.Width() / 14;
        row(kLettersR3, 9, pad + inset, full_w - inset * 2, y, rh);
      }
      y += rh + gap;
      row(kLettersR4, 7, pad, full_w, y, rh);
      break;
    }
    case PAGE_NUMBERS: {
      int rh = (rc.Height() - top - pad * 2 - gap * 3) / 4;
      if (rh < _Scaled(32))
        rh = _Scaled(32);
      const int cw = (full_w - gap * 4) / 5;
      int cx[5];
      {
        int x = pad;
        for (int c = 0; c < 5; ++c) {
          cx[c] = x;
          x += cw + gap;
        }
      }
      const int y1 = top + pad;
      const int y2 = y1 + rh + gap;
      const int y3 = y2 + rh + gap;
      const int y4 = y3 + rh + gap;
      put(cx[0], y1, cw, rh, KS(L"%"));
      put(cx[1], y1, cw, rh, K(L"1", '1', '1', KEY_NORMAL));
      put(cx[2], y1, cw, rh, K(L"2", '2', '2', KEY_NORMAL));
      put(cx[3], y1, cw, rh, K(L"3", '3', '3', KEY_NORMAL));
      put(cx[4], y1, cw, rh, K(L"", 0, VK_BACK, KEY_BACKSPACE, 1, true));
      put(cx[0], y2, cw, rh, KS(L"+"));
      put(cx[1], y2, cw, rh, K(L"4", '4', '4', KEY_NORMAL));
      put(cx[2], y2, cw, rh, K(L"5", '5', '5', KEY_NORMAL));
      put(cx[3], y2, cw, rh, K(L"6", '6', '6', KEY_NORMAL));
      put(cx[4], y2, cw, rh * 2 + gap, K(L"空格", 0x20, VK_SPACE, KEY_SPACE));
      put(cx[0], y3, cw, rh, KS(L"−"));
      put(cx[1], y3, cw, rh, K(L"7", '7', '7', KEY_NORMAL));
      put(cx[2], y3, cw, rh, K(L"8", '8', '8', KEY_NORMAL));
      put(cx[3], y3, cw, rh, K(L"9", '9', '9', KEY_NORMAL));
      put(cx[0], y4, cw, rh, KS(L"/"));
      put(cx[1], y4, cw, rh, KS(L"@"));
      put(cx[2], y4, cw, rh, K(L"0", '0', '0', KEY_NORMAL));
      put(cx[3], y4, cw, rh, KS(L"."));
      put(cx[4], y4, cw, rh,
          K(L"返回", 0, 0, KEY_PAGE, 1, false, nullptr, nullptr, true,
            PAGE_LETTERS));
      break;
    }
    case PAGE_SYMBOLS: {
      const int th = _Scaled(38);
      int y = top + pad;
      const int tw = (full_w - gap * 4) / 5;
      {
        int x = pad;
        for (int i = 0; i < 5; ++i) {
          const int w = (i == 4) ? (pad + full_w - x) : tw;
          put(x, y, w, th,
              K(kSymbolCatNames[i], 0, 0, KEY_SYM_TAB, 1, true, nullptr,
                nullptr, false, i));
          x += tw + gap;
        }
      }
      y += th + gap;
      const int bottom_h = _Scaled(42);
      int grid_h = rc.Height() - y - pad - bottom_h - gap;
      if (grid_h < _Scaled(60))
        grid_h = _Scaled(60);
      const int gh = (grid_h - gap * 2) / 3;
      const int gw = (full_w - gap * 5) / 6;
      int gy = y;
      for (int r = 0; r < 3; ++r) {
        int gx = pad;
        for (int c = 0; c < 6; ++c) {
          put(gx, gy, gw, gh, KS(kSymbolTable[m_symbol_cat][r * 6 + c]));
          gx += gw + gap;
        }
        gy += gh + gap;
      }
      const int by = rc.Height() - pad - bottom_h;
      const int bw = _Scaled(96);
      put(rc.right - pad - bw, by, bw, bottom_h,
          K(L"返回", 0, 0, KEY_PAGE, 1, false, nullptr, nullptr, true,
            PAGE_LETTERS));
      break;
    }
    case PAGE_EDIT: {
      int rh = (rc.Height() - top - pad * 2 - gap * 2) / 3;
      if (rh < _Scaled(40))
        rh = _Scaled(40);
      int y = top + pad;
      row(kEditR1, 5, pad, full_w, y, rh);
      y += rh + gap;
      row(kEditR2, 5, pad, full_w, y, rh);
      y += rh + gap;
      row(kEditR3, 5, pad, full_w, y, rh);
      break;
    }
    case PAGE_CLIPBOARD: {
      const int hh = _Scaled(44);
      int y = top + pad;
      const int back_w = _Scaled(64);
      put(pad, y, back_w, hh,
          K(L"返回", 0, 0, KEY_PAGE, 1, true, nullptr, nullptr, false,
            PAGE_LETTERS));
      const int clear_w = _Scaled(72);
      put(rc.right - pad - clear_w, y, clear_w, hh,
          K(L"清空", 0, 0, KEY_CLIP_CLEAR, 1, true));
      y += hh + gap;
      const int item_h = _Scaled(56);
      int history = 0;
      {
        std::lock_guard<std::mutex> lock(m_data_mutex);
        history = (int)m_history.size();
      }
      int max_items = (rc.Height() - y - pad + gap) / (item_h + gap);
      if (max_items < 0)
        max_items = 0;
      int shown = (history < max_items) ? history : max_items;
      for (int i = 0; i < shown; ++i) {
        KeyDef def;
        def.action = KEY_CLIP_ITEM;
        def.payload = i;
        {
          std::lock_guard<std::mutex> lock(m_data_mutex);
          if (i < (int)m_history.size())
            def.text = m_history[i];
        }
        put(pad, y + i * (item_h + gap), full_w, item_h, def);
      }
      break;
    }
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

bool WeaselKeyboard::_CurrentAsciiMode() const {
  std::lock_guard<std::mutex> lock(m_data_mutex);
  return m_status.ascii_mode;
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

void WeaselKeyboard::SendCtrlKey(WORD vk) {
  INPUT inputs[4] = {};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_CONTROL;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = vk;
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = vk;
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_CONTROL;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  ::SendInput(4, inputs, sizeof(INPUT));
}

void WeaselKeyboard::_InjectVkKey(WORD vk, bool shift) {
  INPUT inputs[4] = {};
  int n = 0;
  if (shift) {
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_SHIFT;
    ++n;
  }
  inputs[n].type = INPUT_KEYBOARD;
  inputs[n].ki.wVk = vk;
  ++n;
  inputs[n].type = INPUT_KEYBOARD;
  inputs[n].ki.wVk = vk;
  inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
  ++n;
  if (shift) {
    inputs[n].type = INPUT_KEYBOARD;
    inputs[n].ki.wVk = VK_SHIFT;
    inputs[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;
  }
  ::SendInput(n, inputs, sizeof(INPUT));
}

void WeaselKeyboard::_InjectUnicode(const std::wstring& text) {
  for (wchar_t ch : text) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    ::SendInput(2, inputs, sizeof(INPUT));
  }
}

void WeaselKeyboard::_ActivateKey(int key_index) {
  if (key_index < 0 || key_index >= (int)m_keys.size())
    return;
  const KeyDef& def = m_defs[m_keys[key_index].def];
  auto switch_page = [&](int page) {
    m_page = (Page)page;
    m_shift = false;
    m_select = false;
    if (m_page == PAGE_CLIPBOARD)
      _PollClipboard();
    _Layout();
    _Render();
  };
  auto engine_key = [&](UINT ibus_code, bool shift) -> bool {
    weasel::KeyEvent event(ibus_code, 0);
    if (shift)
      event.mask |= ibus::SHIFT_MASK;
    return m_key_handler && m_key_handler(event);
  };
  switch (def.action) {
    case KEY_SHIFT:
      m_shift = !m_shift;
      _Render();
      return;
    case KEY_SELECT:
      m_select = !m_select;
      _Render();
      return;
    case KEY_MODE:
      if (m_mode_toggle_handler)
        m_mode_toggle_handler(_CurrentAsciiMode());
      return;
    case KEY_TOOL_CLIPBOARD:
      switch_page(m_page == PAGE_CLIPBOARD ? PAGE_LETTERS : PAGE_CLIPBOARD);
      return;
    case KEY_TOOL_EDIT:
      switch_page(m_page == PAGE_EDIT ? PAGE_LETTERS : PAGE_EDIT);
      return;
    case KEY_DISMISS:
      if (m_dismiss_handler)
        m_dismiss_handler();
      else
        Hide();
      return;
    case KEY_PAGE:
      switch_page(def.payload);
      return;
    case KEY_SYM_TAB:
      m_symbol_cat = def.payload;
      _Layout();
      _Render();
      return;
    case KEY_CLIP_CLEAR: {
      std::lock_guard<std::mutex> lock(m_data_mutex);
      m_history.clear();
    }
      _Layout();
      _Render();
      return;
    case KEY_CLIP_ITEM:
      _PasteClipboardItem((size_t)def.payload);
      return;
    case KEY_SELECT_ALL:
      SendCtrlKey('A');
      return;
    case KEY_COPY:
      SendCtrlKey('C');
      return;
    case KEY_CUT:
      SendCtrlKey('X');
      return;
    case KEY_PASTE:
      SendCtrlKey('V');
      return;
    case KEY_TAB:
      _InjectVkKey(VK_TAB, false);
      return;
    case KEY_HOME:
      _InjectVkKey(VK_HOME, false);
      return;
    case KEY_END:
      _InjectVkKey(VK_END, false);
      return;
    case KEY_DELETE:
      _InjectVkKey(VK_DELETE, false);
      return;
    case KEY_BACKSPACE:
      if (!engine_key(ibus::BackSpace, false))
        _InjectVkKey(VK_BACK, false);
      return;
    case KEY_ENTER:
      if (!engine_key(ibus::Return, false))
        _InjectVkKey(VK_RETURN, false);
      return;
    case KEY_SPACE:
      if (!engine_key(ibus::space, false))
        _InjectVkKey(VK_SPACE, false);
      return;
    case KEY_ARROW: {
      const bool shift = m_select;
      if (!engine_key(def.ibus_code, shift))
        _InjectVkKey((WORD)def.vk, shift);
      return;
    }
    default:
      break;
  }
  // KEY_NORMAL
  const bool is_alpha = def.ibus_code >= 'a' && def.ibus_code <= 'z';
  const bool shift = is_alpha && m_shift;
  bool handled = false;
  if (def.ibus_code) {
    weasel::KeyEvent event(def.ibus_code, 0);
    if (shift) {
      event.keycode = def.ibus_code - 'a' + 'A';
      event.mask |= ibus::SHIFT_MASK;
    }
    handled = m_key_handler && m_key_handler(event);
  }
  if (!handled) {
    if (def.vk)
      _InjectVkKey((WORD)def.vk, shift);
    else if (!def.text.empty())
      _InjectUnicode(def.text);
  }
  if (shift)
    m_shift = false;
}

void WeaselKeyboard::_PollClipboard() {
  DWORD seq = ::GetClipboardSequenceNumber();
  if (seq == m_clip_seq)
    return;
  m_clip_seq = seq;
  if (!::OpenClipboard(m_hWnd))
    return;
  HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
  if (data) {
    const wchar_t* src = static_cast<const wchar_t*>(::GlobalLock(data));
    if (src) {
      std::wstring text(src);
      if (text.size() > kMaxClipLength)
        text.resize(kMaxClipLength);
      ::GlobalUnlock(data);
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

void WeaselKeyboard::_AddClipboardText(const std::wstring& text) {
  if (text.empty())
    return;
  bool refresh = false;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    for (auto it = m_history.begin(); it != m_history.end(); ++it) {
      if (*it == text) {
        m_history.erase(it);
        break;
      }
    }
    m_history.insert(m_history.begin(), text);
    if (m_history.size() > kMaxClipItems)
      m_history.resize(kMaxClipItems);
    refresh = (m_page == PAGE_CLIPBOARD);
  }
  if (refresh) {
    _Layout();
    _Render();
  }
}

void WeaselKeyboard::_PasteClipboardItem(size_t index) {
  std::wstring text;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    if (index >= m_history.size())
      return;
    text = m_history[index];
  }
  if (::OpenClipboard(m_hWnd)) {
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
  }
  SendCtrlKey('V');
  m_page = PAGE_LETTERS;
  m_shift = false;
  m_select = false;
  _Layout();
  _Render();
}

void WeaselKeyboard::_Draw(Gdiplus::Graphics& g, const CRect& rc) {
  weasel::Context ctx;
  weasel::Status status;
  std::wstring hint;
  size_t history_count = 0;
  {
    std::lock_guard<std::mutex> lock(m_data_mutex);
    ctx = m_ctx;
    status = m_status;
    hint = m_hint;
    history_count = m_history.size();
  }

  const int pad = _Scaled(6);
  const int gap = _Scaled(7);
  const int strip_top = m_grip_h;
  const int toolbar_top = m_grip_h + m_strip_h;
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

  // Candidate strip background.
  {
    Gdiplus::SolidBrush strip_brush(kColorStrip);
    g.FillRectangle(&strip_brush, Gdiplus::RectF(0.0f, (float)strip_top, width,
                                                 (float)m_strip_h));
    Gdiplus::SolidBrush line_brush(kColorLine);
    g.FillRectangle(
        &line_brush,
        Gdiplus::RectF(0.0f, (float)(strip_top + m_strip_h - 1), width, 1.0f));
  }

  // Toolbar background.
  {
    Gdiplus::SolidBrush band_brush(kColorStrip);
    g.FillRectangle(&band_brush, Gdiplus::RectF(0.0f, (float)toolbar_top, width,
                                                (float)m_toolbar_h));
    Gdiplus::SolidBrush line_brush(kColorLine);
    g.FillRectangle(&line_brush,
                    Gdiplus::RectF(0.0f, (float)(toolbar_top + m_toolbar_h - 1),
                                   width, 1.0f));
  }

  // Candidate strip content.
  m_candidate_rects.clear();
  {
    float x = (float)pad;
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

  // Keys (toolbar chips and page content).
  for (size_t i = 0; i < m_keys.size(); ++i) {
    const KeyLayout& key = m_keys[i];
    const KeyDef& def = m_defs[key.def];
    const bool pressed = ((int)i == m_pressed_key);
    const Gdiplus::RectF rect((float)key.rect.left, (float)key.rect.top,
                              (float)key.rect.Width(),
                              (float)key.rect.Height());
    const float cx = (rect.X + rect.GetRight()) / 2.0f;
    const float cy = (rect.Y + rect.GetBottom()) / 2.0f;

    // Toolbar chips.
    if (def.action == KEY_TOOL_CLIPBOARD || def.action == KEY_TOOL_EDIT ||
        def.action == KEY_DISMISS) {
      const bool active =
          (def.action == KEY_TOOL_CLIPBOARD && m_page == PAGE_CLIPBOARD) ||
          (def.action == KEY_TOOL_EDIT && m_page == PAGE_EDIT);
      Gdiplus::Color bg = active ? kColorAccentSoft : kColorKeyFn;
      if (pressed)
        bg = kColorKeyPressed;
      _FillRoundedRect(g, rect, chip_radius, bg);
      const Gdiplus::Color ic = active ? kColorAccent : kColorSubText;
      const float s = (float)_Scaled(9);
      if (def.action == KEY_TOOL_CLIPBOARD)
        _DrawScissorsIcon(g, cx, cy, s, ic);
      else if (def.action == KEY_TOOL_EDIT)
        _DrawEditIcon(g, cx, cy, s, ic);
      else
        _DrawChevronDown(g, cx, cy, s, ic);
      continue;
    }

    // Style resolution.
    Gdiplus::Color bg = kColorKey;
    Gdiplus::Color fg = kColorText;
    if (def.action == KEY_CLIP_ITEM) {
      bg = pressed ? kColorAccentSoft : kColorCard;
      fg = pressed ? kColorAccent : kColorText;
    } else if (def.is_primary) {
      bg = kColorAccent;
      fg = kColorWhite;
    } else if (def.action == KEY_SPACE && m_voice_active) {
      bg = kColorAccent;
      fg = kColorWhite;
    } else if ((def.action == KEY_SHIFT && m_shift) ||
               (def.action == KEY_SELECT && m_select) ||
               (def.action == KEY_SYM_TAB && def.payload == m_symbol_cat)) {
      bg = kColorAccentSoft;
      fg = kColorAccent;
    } else if (pressed) {
      bg = kColorKeyPressed;
    } else if (def.is_fn) {
      bg = kColorKeyFn;
    }
    _FillRoundedRect(g, rect, key_radius, bg);
    const bool want_border =
        (def.action == KEY_NORMAL || def.action == KEY_CLIP_ITEM) && !pressed;
    if (want_border) {
      Gdiplus::GraphicsPath border_path;
      _AddRoundedRectPath(border_path, rect, key_radius);
      int border_width = _Scaled(1);
      if (border_width < 1)
        border_width = 1;
      Gdiplus::Pen border_pen(kColorKeyBorder, (Gdiplus::REAL)border_width);
      g.DrawPath(&border_pen, &border_path);
    }

    // Content.
    if (def.action == KEY_ARROW) {
      const Gdiplus::Color color = m_select ? kColorAccent : fg;
      _DrawChevron(g, key.rect, def.ibus_code, color, dpi_scale);
      continue;
    }
    if (def.action == KEY_BACKSPACE) {
      _DrawBackspaceIcon(
          g, cx, cy, (float)_Scaled(9),
          (def.is_primary || (def.action == KEY_SPACE && m_voice_active))
              ? fg
              : kColorText);
      continue;
    }
    if (def.action == KEY_CLIP_ITEM) {
      _DrawTextInRect(
          g, def.text, m_font_candidate.get(),
          Gdiplus::RectF(rect.X + (float)_Scaled(16), rect.Y,
                         rect.Width() - (float)_Scaled(24), rect.Height()),
          fg, Gdiplus::StringAlignmentNear);
      continue;
    }
    std::wstring label = def.label;
    if (def.action == KEY_SPACE)
      label = m_voice_active ? L"松开结束" : L"按住说话";
    Gdiplus::Font* font = m_font_fn.get();
    if (def.action == KEY_NORMAL && label.size() == 1) {
      font = (label[0] < 128) ? m_font_key.get() : m_font_candidate.get();
    }
    _DrawTextInRect(g, label, font, rect, fg);
    if (def.hint_left) {
      _DrawTextInRect(
          g, def.hint_left, m_font_tiny.get(),
          Gdiplus::RectF(rect.X + (float)_Scaled(6), rect.Y + (float)_Scaled(2),
                         rect.Width() - (float)_Scaled(12), (float)_Scaled(16)),
          kColorSubText, Gdiplus::StringAlignmentNear,
          Gdiplus::StringAlignmentNear);
    }
    if (def.hint_right) {
      _DrawTextInRect(
          g, def.hint_right, m_font_tiny.get(),
          Gdiplus::RectF(rect.X + (float)_Scaled(6), rect.Y + (float)_Scaled(2),
                         rect.Width() - (float)_Scaled(12), (float)_Scaled(16)),
          kColorSubText, Gdiplus::StringAlignmentFar,
          Gdiplus::StringAlignmentNear);
    }
  }

  // Clipboard page chrome (title, count, empty hint).
  if (m_page == PAGE_CLIPBOARD) {
    const int top = m_grip_h + m_strip_h + m_toolbar_h;
    const int hh = _Scaled(44);
    const int back_w = _Scaled(64);
    const float title_x = (float)(pad + back_w + _Scaled(10));
    _DrawTextInRect(g, L"剪贴板", m_font_candidate.get(),
                    Gdiplus::RectF(title_x, (float)(top + pad),
                                   (float)_Scaled(90), (float)hh),
                    kColorText, Gdiplus::StringAlignmentNear);
    if (history_count) {
      _DrawTextInRect(
          g,
          std::to_wstring(history_count) + L"/" +
              std::to_wstring(kMaxClipItems),
          m_font_tiny.get(),
          Gdiplus::RectF(title_x + (float)_Scaled(78), (float)(top + pad),
                         (float)_Scaled(80), (float)hh),
          kColorSubText, Gdiplus::StringAlignmentNear);
    }
    if (!history_count) {
      _DrawTextInRect(
          g, L"复制过的内容会出现在这里", m_font_hint.get(),
          Gdiplus::RectF((float)(pad + _Scaled(10)),
                         (float)(top + pad + hh + gap),
                         width - (float)(pad * 2), (float)_Scaled(56)),
          kColorSubText, Gdiplus::StringAlignmentNear);
    }
  }
}
