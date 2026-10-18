#include "ReaderPanel.h"
#include <algorithm>
#include <cwchar>
#include "config.h"
#include "Settings.h"

using std::max;
using std::min;

namespace {
    constexpr UINT TEXT_FLAGS = DT_WORDBREAK | DT_LEFT | DT_TOP | DT_NOPREFIX | DT_EDITCONTROL;

    bool isBlank(wchar_t c)
    {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    }

    // Позиция сразу после count-й пустой строки (\n\n или \r\n\r\n), начиная со start
    size_t findNextParagraph(const std::wstring& t, size_t start, int count)
    {
        size_t pos = start;
        int found = 0;
        while (pos < t.size() && found < count)
        {
            if (pos + 1 < t.size() && t[pos] == L'\n' && t[pos + 1] == L'\n') {
                ++found;
                pos += 2;
            }
            else if (pos + 3 < t.size() &&
                t[pos] == L'\r' && t[pos + 1] == L'\n' &&
                t[pos + 2] == L'\r' && t[pos + 3] == L'\n') {
                ++found;
                pos += 4;
            }
            else {
                ++pos;
            }
        }
        return pos;
    }

    // Где разрезать слишком длинный кадр: перевод строки, конец предложения или пробел
    size_t splitPoint(const std::wstring& t, size_t start, size_t maxLen)
    {
        const size_t limit = min(t.size(), start + maxLen);
        const size_t floor = start + maxLen / 2;

        for (size_t i = limit; i > floor; --i)
            if (t[i - 1] == L'\n') return i;
        for (size_t i = limit; i > floor; --i)
            if (wcschr(L".!?…", t[i - 1]) && i < t.size() && isBlank(t[i])) return i;
        for (size_t i = limit; i > floor; --i)
            if (t[i - 1] == L' ') return i;
        return limit;
    }
}

ReaderPanel::ReaderPanel(HINSTANCE hInst, HWND hParent, Layout layout)
    : _hInst(hInst), _hParent(hParent), _layout(layout)
{
    _dpi = GetDpiForWindow(hParent);
    if (!_dpi) _dpi = 96;
    _fontPt = std::clamp(static_cast<int>(settings::getDword(L"FontSize", 14)), 10, 32);
    _speed = std::clamp(static_cast<int>(settings::getDword(L"Speed", 1)), 1, 10);
    updateFonts();
}

ReaderPanel::~ReaderPanel()
{
    KillTimer(_hParent, TIMER_ID);
    destroyScrollbar();
    DeleteObject(_font);
    DeleteObject(_fontSmall);
}

void ReaderPanel::updateFonts()
{
    if (_font) DeleteObject(_font);
    if (_fontSmall) DeleteObject(_fontSmall);

    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;

    wcscpy_s(lf.lfFaceName, L"Georgia");
    lf.lfHeight = -MulDiv(_fontPt, _dpi, 72);
    _font = CreateFontIndirectW(&lf);

    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    lf.lfHeight = -MulDiv(10, _dpi, 72);
    _fontSmall = CreateFontIndirectW(&lf);
}

// ──────────────────────────────────────────────
//  Текст и кадры
// ──────────────────────────────────────────────
void ReaderPanel::buildFrames()
{
    _frames.clear();
    size_t pos = 0;
    for (;;)
    {
        while (pos < _text.size() && isBlank(_text[pos])) ++pos;
        if (pos >= _text.size()) break;

        size_t end = findNextParagraph(_text, pos, SKIP_ENDS);
        if (end - pos > MAX_FRAME_CHARS)
            end = splitPoint(_text, pos, MAX_FRAME_CHARS);

        size_t trimmed = end;
        while (trimmed > pos && isBlank(_text[trimmed - 1])) --trimmed;
        _frames.push_back({ pos, trimmed });
        pos = end;
    }
}

std::wstring ReaderPanel::GetFrameText(size_t frame) const
{
    if (frame >= _frames.size()) return {};
    return _text.substr(_frames[frame].start, _frames[frame].end - _frames[frame].start);
}

size_t ReaderPanel::frameLength() const
{
    return _frames.empty() ? 0 : _frames[_frame].end - _frames[_frame].start;
}

void ReaderPanel::SetText(const std::wstring& txt, size_t startFrame)
{
    _text = txt;
    buildFrames();
    _active = true;
    _paused = false;

    RECT rc; GetClientRect(_hParent, &rc);
    Resize(rc);

    SetTimer(_hParent, TIMER_ID, TICK_MS, nullptr);
    goToFrame(startFrame < _frames.size() ? startFrame : 0, true);
}

void ReaderPanel::goToFrame(size_t frame, bool animate)
{
    if (_frames.empty()) return;

    _frame = min(frame, _frames.size() - 1);
    _visible = animate ? 0 : frameLength();
    _scrollPos = 0;
    _userScrolled = !animate;            // показанный целиком кадр — с начала, без автопрокрутки

    recalcTextMetrics();
    ensureScrollbar();
    if (_onFrameChange) _onFrameChange(_frame);
    InvalidateRect(_hParent, nullptr, FALSE);
}

void ReaderPanel::SetOnFrameChange(std::function<void(size_t)> cb)
{
    _onFrameChange = std::move(cb);
}

void ReaderPanel::SetBackground(HBITMAP bmp)
{
    _bgBitmap = bmp;
    InvalidateRect(_hParent, nullptr, FALSE);
}

// ──────────────────────────────────────────────
//  Навигация
// ──────────────────────────────────────────────
void ReaderPanel::Next()
{
    if (!_active || _frames.empty()) return;
    _paused = false;

    if (!frameDone()) {                  // сначала допечатываем текущий кадр
        _visible = frameLength();
        recalcTextMetrics();
        ensureScrollbar();
        InvalidateRect(_hParent, nullptr, FALSE);
        return;
    }
    if (_frame + 1 < _frames.size())
        goToFrame(_frame + 1, true);
}

void ReaderPanel::Prev()
{
    if (!_active || _frame == 0) return;
    _paused = false;
    goToFrame(_frame - 1, false);
}

void ReaderPanel::Close()
{
    _active = false;
    KillTimer(_hParent, TIMER_ID);
    destroyScrollbar();
    InvalidateRect(_hParent, nullptr, TRUE);
}

void ReaderPanel::OnTimer()
{
    if (!_active || _paused || _frames.empty() || frameDone()) return;

    _visible = min(frameLength(), _visible + static_cast<size_t>(_speed));

    recalcTextMetrics();
    ensureScrollbar();
    InvalidateRect(_hParent, &_rcBox, FALSE);
    if (frameDone()) InvalidateRect(_hParent, &_rcPauseBtn, FALSE);
}

bool ReaderPanel::OnClick(int x, int y)
{
    if (!_active) return false;
    const POINT pt{ x, y };

    if (PtInRect(&_rcClose, pt)) {
        Close();
        return true;
    }
    if (PtInRect(&_rcPauseBtn, pt) && !frameDone()) {
        _paused = !_paused;
        InvalidateRect(_hParent, &_rcBox, FALSE);
        return true;
    }
    Next();                              // клик в любом другом месте листает вперёд
    return true;
}

bool ReaderPanel::OnKey(UINT vk)
{
    if (!_active) return false;

    switch (vk)
    {
    case VK_RIGHT: case VK_RETURN: case VK_NEXT:
        Next(); return true;
    case VK_LEFT: case VK_BACK: case VK_PRIOR:
        Prev(); return true;
    case VK_SPACE:
        if (frameDone()) Next();
        else { _paused = !_paused; InvalidateRect(_hParent, &_rcBox, FALSE); }
        return true;
    case VK_ESCAPE:
        Close(); return true;
    case VK_UP:
        setScroll(_scrollPos - px(40), true); return true;
    case VK_DOWN:
        setScroll(_scrollPos + px(40), true); return true;
    case VK_OEM_PLUS: case VK_ADD:
        changeFontSize(+1); return true;
    case VK_OEM_MINUS: case VK_SUBTRACT:
        changeFontSize(-1); return true;
    case VK_OEM_4:                       // [
        changeSpeed(-1); return true;
    case VK_OEM_6:                       // ]
        changeSpeed(+1); return true;
    }
    return false;
}

void ReaderPanel::changeFontSize(int deltaPt)
{
    _fontPt = std::clamp(_fontPt + deltaPt, 10, 32);
    settings::setDword(L"FontSize", _fontPt);
    updateFonts();
    recalcTextMetrics();
    ensureScrollbar();
    InvalidateRect(_hParent, nullptr, FALSE);
}

void ReaderPanel::changeSpeed(int delta)
{
    _speed = std::clamp(_speed + delta, 1, 10);
    settings::setDword(L"Speed", _speed);
    InvalidateRect(_hParent, &_rcFooter, FALSE);
}

// ──────────────────────────────────────────────
//  Раскладка
// ──────────────────────────────────────────────
void ReaderPanel::Resize(const RECT& rcClient)
{
    UINT dpi = GetDpiForWindow(_hParent);
    if (dpi && dpi != _dpi) { _dpi = dpi; updateFonts(); }

    const int W = rcClient.right, H = rcClient.bottom;
    const int footerH = px(44);
    const int side = px(40);

    int boxW, boxH, top;
    if (_layout == Layout::TextOnly) {
        boxW = min(W - 2 * side, px(1100));
        top = px(80);
        boxH = H - top - footerH;
    }
    else {
        boxW = min(W - 2 * side, px(1000));
        boxH = min(px(260), H / 3);
        top = H - footerH - boxH;
    }
    boxW = max(boxW, px(240));
    boxH = max(boxH, px(140));

    const int left = (W - boxW) / 2;
    _rcBox = { left, top, left + boxW, top + boxH };
    _rcFooter = { left, _rcBox.bottom, _rcBox.right, _rcBox.bottom + footerH };
    _rcImage = { px(80), px(40), W - px(80), _rcBox.top - px(8) };
    _rcClose = { W - px(64), px(16), W - px(16), px(64) };

    const int p = px(40);
    _rcPauseBtn = { _rcBox.right - px(16) - p, _rcBox.top + px(16),
                    _rcBox.right - px(16),     _rcBox.top + px(16) + p };

    recalcTextMetrics();
    ensureScrollbar();
}

RECT ReaderPanel::textRect() const
{
    RECT rc = _rcBox;
    rc.left += px(TEXT_MARGIN);
    rc.top += px(TEXT_MARGIN);
    rc.bottom -= px(TEXT_MARGIN);
    rc.right = _rcPauseBtn.left - px(12);   // справа колонка под паузу и скроллбар
    return rc;
}

void ReaderPanel::recalcTextMetrics()
{
    const RECT tr = textRect();
    const int visibleH = tr.bottom - tr.top;

    int contentH = 0;
    if (!_frames.empty() && _visible > 0)
    {
        HDC hdc = GetDC(_hParent);
        HFONT oldFont = (HFONT)SelectObject(hdc, _font);
        RECT rc = tr;
        DrawTextW(hdc, _text.c_str() + _frames[_frame].start, static_cast<int>(_visible),
            &rc, TEXT_FLAGS | DT_CALCRECT);
        contentH = rc.bottom - rc.top;
        SelectObject(hdc, oldFont);
        ReleaseDC(_hParent, hdc);
    }

    _textHeight = contentH;
    _maxScroll = max(0, contentH - visibleH);
    if (!_userScrolled) _scrollPos = _maxScroll;     // автопрокрутка к печатаемой строке
    _scrollPos = std::clamp(_scrollPos, 0, _maxScroll);
}

void ReaderPanel::ensureScrollbar()
{
    if (_maxScroll <= 0) { destroyScrollbar(); return; }

    const int sw = GetSystemMetricsForDpi(SM_CXVSCROLL, _dpi);
    const int x = _rcPauseBtn.left + (_rcPauseBtn.right - _rcPauseBtn.left - sw) / 2;
    const int y = _rcPauseBtn.bottom + px(8);
    const int h = max(0, static_cast<int>(_rcBox.bottom) - px(TEXT_MARGIN) - y);

    if (!_hScroll) {
        _hScroll = CreateWindowExW(0, L"SCROLLBAR", nullptr,
            WS_CHILD | WS_VISIBLE | SBS_VERT,
            x, y, sw, h, _hParent, nullptr, _hInst, nullptr);
    }
    else {
        RECT cur; GetWindowRect(_hScroll, &cur);
        MapWindowPoints(nullptr, _hParent, reinterpret_cast<POINT*>(&cur), 2);
        if (cur.left != x || cur.top != y || cur.bottom - cur.top != h)
            MoveWindow(_hScroll, x, y, sw, h, TRUE);
    }

    const RECT tr = textRect();
    SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0;
    si.nMax = _textHeight - 1;
    si.nPage = tr.bottom - tr.top;
    si.nPos = _scrollPos;
    SetScrollInfo(_hScroll, SB_CTL, &si, TRUE);
}

void ReaderPanel::destroyScrollbar()
{
    if (_hScroll) { DestroyWindow(_hScroll); _hScroll = nullptr; }
}

void ReaderPanel::setScroll(int pos, bool byUser)
{
    _scrollPos = std::clamp(pos, 0, _maxScroll);
    if (byUser) _userScrolled = _scrollPos < _maxScroll;   // докрутили до конца → снова авто

    if (_hScroll) {
        SCROLLINFO si{ sizeof(si), SIF_POS };
        si.nPos = _scrollPos;
        SetScrollInfo(_hScroll, SB_CTL, &si, TRUE);
    }
    InvalidateRect(_hParent, &_rcBox, FALSE);
}

bool ReaderPanel::OnMouseWheel(short delta)
{
    setScroll(_scrollPos + (delta > 0 ? -px(40) : px(40)), true);
    return true;
}

bool ReaderPanel::OnVScroll(HWND hScrollWnd, WPARAM wParam)
{
    // обрабатываем только собственный скроллбар
    if (hScrollWnd != _hScroll) return false;

    SCROLLINFO si{ sizeof(si), SIF_ALL };
    GetScrollInfo(_hScroll, SB_CTL, &si);
    int pos = si.nPos;

    switch (LOWORD(wParam))
    {
    case SB_LINEUP:      pos -= px(40);              break;
    case SB_LINEDOWN:    pos += px(40);              break;
    case SB_PAGEUP:      pos -= static_cast<int>(si.nPage); break;
    case SB_PAGEDOWN:    pos += static_cast<int>(si.nPage); break;
    case SB_THUMBTRACK:  pos = si.nTrackPos;         break;
    case SB_TOP:         pos = 0;                    break;
    case SB_BOTTOM:      pos = _maxScroll;           break;
    default: return false;
    }

    setScroll(pos, true);
    return true;
}

// ──────────────────────────────────────────────
//  Отрисовка
// ──────────────────────────────────────────────
void ReaderPanel::drawCloseButton(HDC hdc)
{
    HBRUSH brC = CreateSolidBrush(CLR_CLOSE);
    HGDIOBJ oldBr = SelectObject(hdc, brC);
    Ellipse(hdc, _rcClose.left, _rcClose.top, _rcClose.right, _rcClose.bottom);
    SelectObject(hdc, oldBr); DeleteObject(brC);

    const int in = (_rcClose.right - _rcClose.left) / 4;
    HPEN pen = CreatePen(PS_SOLID, px(3), RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, _rcClose.left + in, _rcClose.top + in, nullptr);
    LineTo(hdc, _rcClose.right - in, _rcClose.bottom - in);
    MoveToEx(hdc, _rcClose.right - in, _rcClose.top + in, nullptr);
    LineTo(hdc, _rcClose.left + in, _rcClose.bottom - in);
    SelectObject(hdc, oldPen); DeleteObject(pen);
}

void ReaderPanel::drawPauseButton(HDC hdc)
{
    const RECT& r = _rcPauseBtn;
    const int w = r.right - r.left, h = r.bottom - r.top;

    HBRUSH brP = CreateSolidBrush(CLR_CLOSE);
    HGDIOBJ oldBr = SelectObject(hdc, brP);
    Ellipse(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, oldBr); DeleteObject(brP);

    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    oldBr = SelectObject(hdc, GetStockObject(WHITE_BRUSH));

    if (_paused || frameDone())          // ▶ — продолжить / дальше
    {
        POINT pts[3] = {
            { r.left + w * 36 / 100, r.top + h * 26 / 100 },
            { r.left + w * 36 / 100, r.top + h * 74 / 100 },
            { r.left + w * 72 / 100, r.top + h / 2 }
        };
        Polygon(hdc, pts, 3);
    }
    else                                 // ▮▮ — идёт печать
    {
        Rectangle(hdc, r.left + w * 30 / 100, r.top + h * 26 / 100, r.left + w * 43 / 100, r.top + h * 74 / 100);
        Rectangle(hdc, r.left + w * 57 / 100, r.top + h * 26 / 100, r.left + w * 70 / 100, r.top + h * 74 / 100);
    }

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

void ReaderPanel::drawFooter(HDC hdc)
{
    SelectObject(hdc, _fontSmall);
    SetTextColor(hdc, CLR_HINT);
    SetBkMode(hdc, TRANSPARENT);

    wchar_t progress[64];
    swprintf_s(progress, L"%zu / %zu    speed %d", _frame + 1, _frames.size(), _speed);

    RECT rc = _rcFooter;
    DrawTextW(hdc, progress, -1, &rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT measure{};
    DrawTextW(hdc, progress, -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    rc.right -= (measure.right - measure.left) + px(24);
    DrawTextW(hdc,
        L"← →  navigate     Space  pause     + −  text size     [ ]  speed     Esc  close",
        -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void ReaderPanel::OnPaint(HDC hdc)
{
    if (!_active) return;

    // ─── double-buffer ─────────────────────────────────────
    RECT cli; GetClientRect(_hParent, &cli);
    HDC     mem = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateCompatibleBitmap(hdc, cli.right, cli.bottom);
    HGDIOBJ oldB = SelectObject(mem, dib);

    FillRect(mem, &cli, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // ─── иллюстрация над текстом ───────────────────────────
    if (_layout == Layout::Illustrated && _bgBitmap &&
        _rcImage.bottom > _rcImage.top && _rcImage.right > _rcImage.left)
    {
        HDC hTmp = CreateCompatibleDC(mem);
        HGDIOBJ o = SelectObject(hTmp, _bgBitmap);
        BITMAP bm; GetObject(_bgBitmap, sizeof(bm), &bm);

        const int maxW = _rcImage.right - _rcImage.left;
        const int maxH = _rcImage.bottom - _rcImage.top;
        const float scale = min(float(maxW) / bm.bmWidth, float(maxH) / bm.bmHeight);
        const int w = int(bm.bmWidth * scale);
        const int h = int(bm.bmHeight * scale);
        const int x = _rcImage.left + (maxW - w) / 2;

        SetStretchBltMode(mem, HALFTONE);
        StretchBlt(mem, x, _rcImage.top, w, h, hTmp, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

        SelectObject(hTmp, o);
        DeleteDC(hTmp);
    }

    // ─── серый текстовый бокс ──────────────────────────────
    HRGN rgn = CreateRoundRectRgn(_rcBox.left, _rcBox.top, _rcBox.right, _rcBox.bottom,
        px(BOX_R), px(BOX_R));
    HBRUSH br = CreateSolidBrush(CLR_BOX);
    FillRgn(mem, rgn, br);
    DeleteObject(br);
    DeleteObject(rgn);

    // ─── текст кадра ───────────────────────────────────────
    if (!_frames.empty() && _visible > 0)
    {
        const RECT clip = textRect();
        RECT rcT = clip;
        rcT.top -= _scrollPos;

        SelectObject(mem, _font);
        SetTextColor(mem, CLR_TEXT);
        SetBkMode(mem, TRANSPARENT);

        SaveDC(mem);
        IntersectClipRect(mem, clip.left, clip.top, clip.right, clip.bottom);
        DrawTextW(mem, _text.c_str() + _frames[_frame].start, static_cast<int>(_visible),
            &rcT, TEXT_FLAGS);
        RestoreDC(mem, -1);
    }

    drawCloseButton(mem);
    drawPauseButton(mem);
    drawFooter(mem);

    // ─── вывод на экран ────────────────────────────────────
    BitBlt(hdc, 0, 0, cli.right, cli.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldB);
    DeleteObject(dib);
    DeleteDC(mem);
}
