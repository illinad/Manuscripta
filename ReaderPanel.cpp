#include "ReaderPanel.h"
#include <algorithm>
#include "config.h"
#include <unordered_set>
#include "SceneFetcher.h"
#include "ImageCache.h"

const int ReaderPanel::SCROLL_W = GetSystemMetrics(SM_CXVSCROLL);

using std::max;
using std::min;

bool ReaderPanel::TryMarkFrameRequested(const std::wstring& frame)
{
    return _requestedFrames.insert(frame).second;
}

std::wstring ReaderPanel::GetFrameText(size_t start, int count) const
{
    size_t end = findNextParagraph(start, count);
    if (end > _text.size()) end = _text.size();
    return _text.substr(start, end - start);
}

size_t ReaderPanel::findNextParagraph(size_t start, int count) const
{
    size_t pos = start;
    int found = 0;
    while (pos < _text.size() && found < count)
    {
        if (pos + 1 < _text.size() &&
            _text[pos] == L'\n' && _text[pos + 1] == L'\n') {
            ++found;
            pos += 2;
        }
        else if (pos + 3 < _text.size() &&
            _text[pos] == L'\r' && _text[pos + 1] == L'\n' &&
            _text[pos + 2] == L'\r' && _text[pos + 3] == L'\n') {
            ++found;
            pos += 4;
        }
        else {
            ++pos;
        }
    }
    return pos;
}

ReaderPanel::ReaderPanel(HINSTANCE hInst, HWND hParent)
    : _hInst(hInst), _hParent(hParent)
{
    LOGFONTW lf{}; wcscpy_s(lf.lfFaceName, L"Georgia");
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;

    HDC hdc = GetDC(nullptr);
    lf.lfHeight = -MulDiv(12, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);

    _font = CreateFontIndirectW(&lf);
}

ReaderPanel::~ReaderPanel()
{
    destroyScrollbar();
    DeleteObject(_font);
}

// ──────────────────────────────────────────────
//  Возвращает высоту (px) прямоугольника, который
//  займёт подстрока [_text[start … start+len) ] 
//  после раскладки DrawTextW с DT_WORDBREAK.
// ──────────────────────────────────────────────
int ReaderPanel::measureHeightForRange(size_t start, size_t len) const
{
    if (len == 0) return 0;

    HDC hdc = GetDC(_hParent);
    HFONT oldF = (HFONT)SelectObject(hdc, _font);

    RECT rc = _rcBox;
    rc.right -= SCROLL_W;
    InflateRect(&rc, -TEXT_MARGIN, -TEXT_MARGIN);

    const wchar_t* slice = _text.c_str() + start;
    DrawTextW(hdc, slice,
        static_cast<int>(len), &rc,
        DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_TOP);

    SelectObject(hdc, oldF);
    ReleaseDC(_hParent, hdc);

    return rc.bottom - rc.top; // высота контента
}

void ReaderPanel::SetOnFrameChange(std::function<void(const std::wstring&)> cb)
{
    _onFrameChange = std::move(cb);
}

std::wstring ReaderPanel::GetFirstFrame() const
{
    size_t pos = 0;

    // пропускаем пробелы и пустые строки
    while (pos < _text.size())
    {
        wchar_t ch = _text[pos];
        if (ch == L'\n' || ch == L'\r' || ch == L' ' || ch == L'\t') ++pos;
        else break;
    }

    size_t end = _text.find(L'\n', pos);
    if (end == std::wstring::npos)
        end = _text.size();
    else
        ++end;

    return _text.substr(pos, end - pos);
}

//------------------------------------------------------------------
// Пересчитать структуру скролла (range/page/pos)
//------------------------------------------------------------------
void ReaderPanel::updateScrollInfo()
{
    if (!_hScroll) return;

    const int visible = BOX_H - 2 * TEXT_MARGIN;        // высота окна текста
    const int total = _textHeight;                    // посчитано в recalcTextMetrics()

    _maxScroll = max(0, total - visible);
    if (_scrollPos > _maxScroll) _scrollPos = _maxScroll;

    SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0;
    si.nMax = _maxScroll + visible - 1;   //  ✔️  «+visible-1» чтобы докручивалось
    si.nPage = visible;
    si.nPos = _scrollPos;
    SetScrollInfo(_hScroll, SB_CTL, &si, TRUE);
}


// ─────────────────────────────────────────────────────────────
// Ищет границу абзаца — два подряд \n\n или \r\n\r\n.
// Возвращает индекс первого символа ПЕРЕД этой парой.
// ─────────────────────────────────────────────────────────────
size_t ReaderPanel::findParagraphEnd(size_t start) const
{
    size_t i = start;
    while (i + 1 < _text.size()) {
        // Unix: \n\n
        if (_text[i] == L'\n' && _text[i + 1] == L'\n')
            return i;

        // Windows: \r\n\r\n
        if (i + 3 < _text.size() &&
            _text[i] == L'\r' &&
            _text[i + 1] == L'\n' &&
            _text[i + 2] == L'\r' &&
            _text[i + 3] == L'\n')
            return i;

        ++i;
    }
    return _text.size(); // не нашли — до конца текста
}
void ReaderPanel::SetText(const std::wstring& txt)
{
    _text = txt;
    _visible = 1;          // оставляем 1 → первый символ сразу виден
    _scrollPos = 0;
    _active = true;
    _frameStart = 0;
    _cursorPos = 0;
    _frameIdle = false;      // ← лишнее обнуление _visible убрано


    RECT rc; GetClientRect(_hParent, &rc);
    Resize(rc);

    recalcTextMetrics();
    ensureScrollbar();
    SetTimer(_hParent, TIMER_ID, TICK_MS, nullptr);
    InvalidateRect(_hParent, nullptr, FALSE);
}

void ReaderPanel::Resize(const RECT& rcClient)
{
    const int cx = (rcClient.right - BOX_W) / 2;
    const int cy = rcClient.bottom - BOX_H - 40;
    _rcBox = { cx, cy, cx + BOX_W, cy + BOX_H };

    recalcTextMetrics();
    positionScrollbar();
    updateScrollInfo();
}

void ReaderPanel::drawCloseButton(HDC hdc)
{
    RECT cli; GetClientRect(_hParent, &cli);
    _rcClose = { cli.right - 48 - 16, 16,
                 cli.right - 16,      16 + 48 };

    HBRUSH brC = CreateSolidBrush(CLR_CLOSE);
    HGDIOBJ oldBr = SelectObject(hdc, brC);
    Ellipse(hdc, _rcClose.left, _rcClose.top, _rcClose.right, _rcClose.bottom);
    SelectObject(hdc, oldBr); DeleteObject(brC);

    HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, _rcClose.left + 12, _rcClose.top + 12, nullptr);
    LineTo(hdc, _rcClose.right - 12, _rcClose.bottom - 12);
    MoveToEx(hdc, _rcClose.right - 12, _rcClose.top + 12, nullptr);
    LineTo(hdc, _rcClose.left + 12, _rcClose.bottom - 12);
    SelectObject(hdc, oldPen); DeleteObject(pen);
}

void ReaderPanel::drawPauseButton(HDC hdc)
{
    int px = _rcBox.right - 16 - 40;
    int py = _rcBox.top + 16;
    _rcPauseBtn = { px, py, px + 40, py + 40 };

    HBRUSH brP = CreateSolidBrush(CLR_CLOSE);
    HGDIOBJ oldBr = SelectObject(hdc, brP);
    Ellipse(hdc, _rcPauseBtn.left, _rcPauseBtn.top, _rcPauseBtn.right, _rcPauseBtn.bottom);
    SelectObject(hdc, oldBr); DeleteObject(brP);

    HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    if (_paused)
    {
        POINT pts[3] = {
            { px + 13, py + 10 },
            { px + 13, py + 30 },
            { px + 27, py + 20 }
        };
        Polygon(hdc, pts, 3);
    }
    else
    {
        Rectangle(hdc, px + 11, py + 10, px + 16, py + 30);
        Rectangle(hdc, px + 24, py + 10, px + 29, py + 30);
    }

    SelectObject(hdc, oldPen); DeleteObject(pen);
}

void ReaderPanel::OnPaint(HDC hdc)
{
    if (!_active) return;

    // ─── double-buffer ─────────────────────────────────────
    RECT cli; GetClientRect(_hParent, &cli);
    HDC     mem = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateCompatibleBitmap(hdc, cli.right, cli.bottom);
    HGDIOBJ oldB = SelectObject(mem, dib);

    // ─── фон окна (чёрный) ─────────────────────────────────
    FillRect(mem, &cli, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // ─── верхняя иллюстрация (если есть) ──────────────────
    // ─── верхняя иллюстрация ───────────────────────────────────────────
    if (_bgBitmap)
    {
        const int topMargin = 40;   // отступ от верхнего края окна
        const int gap = 8;    // микро-зазор до _rcBox

        HDC hTmp = CreateCompatibleDC(mem);
        HGDIOBJ o = SelectObject(hTmp, _bgBitmap);

        BITMAP bm; GetObject(_bgBitmap, sizeof(bm), &bm);

        // 1. свободная «рамка» для картинки
        int maxH = _rcBox.top - gap - topMargin;     // до края текстового окна
        int maxW = cli.right - topMargin * 2;        // почти во всю ширину

        // 2. коэффициент масштабирования
        float sW = float(maxW) / bm.bmWidth;
        float sH = float(maxH) / bm.bmHeight;
        float scale = min(sW, sH);                   // → не выйдем за пределы

        // 3. финальные размеры и позиция
        int w = int(bm.bmWidth * scale);
        int h = int(bm.bmHeight * scale);
        int x = (cli.right - w) / 2;
        int y = topMargin;

        SetStretchBltMode(mem, HALFTONE);            // сглаживаем
        StretchBlt(mem, x, y, w, h,
            hTmp, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

        SelectObject(hTmp, o);
        DeleteDC(hTmp);
    }


    // ─── серый текстовый бокс ──────────────────────────────
    HRGN rgn = CreateRoundRectRgn(_rcBox.left, _rcBox.top,
        _rcBox.right, _rcBox.bottom,
        BOX_R, BOX_R);
    HBRUSH br = CreateSolidBrush(CLR_BOX);
    FillRgn(mem, rgn, br);
    DeleteObject(br);
    DeleteObject(rgn);

    // ─── текст внутри бокса ────────────────────────────────
    RECT rcT = _rcBox;
    rcT.right -= SCROLL_W;
    InflateRect(&rcT, -TEXT_MARGIN, -TEXT_MARGIN);
    rcT.top -= _scrollPos;

    SelectObject(mem, _font);
    SetTextColor(mem, CLR_TEXT);
    SetBkMode(mem, TRANSPARENT);

    SaveDC(mem);
    IntersectClipRect(mem,
        _rcBox.left + TEXT_MARGIN,
        _rcBox.top + TEXT_MARGIN,
        _rcBox.right - TEXT_MARGIN,
        _rcBox.bottom - TEXT_MARGIN);

    const wchar_t* slice = _text.c_str() + _frameStart;
    int visible = static_cast<int>(_visible);

    DrawTextW(mem, slice, visible, &rcT,
        DT_WORDBREAK | DT_LEFT | DT_TOP);

    RestoreDC(mem, -1);

    // ─── кнопки (⨉ и ▶▮▮) ─────────────────────────────────
    drawCloseButton(mem);
    drawPauseButton(mem);

    // ─── вывод на экран ────────────────────────────────────
    BitBlt(hdc, 0, 0, cli.right, cli.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldB);
    DeleteObject(dib);
    DeleteDC(mem);
}

void ReaderPanel::positionScrollbar()
{
    if (!_hScroll) return;
    int sx = _rcBox.right - SCROLL_W - 1;
    int sy = _rcBox.top + TEXT_MARGIN;
    int sh = BOX_H - 2 * TEXT_MARGIN;
    MoveWindow(_hScroll, sx, sy, SCROLL_W, sh, TRUE);
}

void ReaderPanel::ensureScrollbar()
{
    if (!_maxScroll) { destroyScrollbar(); return; }

    if (!_hScroll)
    {
        _hScroll = CreateWindowExW(0, L"SCROLLBAR", nullptr,
            WS_CHILD | WS_VISIBLE | SBS_VERT,
            0, 0, SCROLL_W, BOX_H, _hParent,
            nullptr, _hInst, nullptr);
    }

    // 🔹 NEW: ставим сразу на место
    positionScrollbar();

    SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0;
    si.nMax = _maxScroll;
    si.nPage = BOX_H - 2 * TEXT_MARGIN;
    si.nPos = _scrollPos;
    SetScrollInfo(_hScroll, SB_CTL, &si, TRUE);
}


void ReaderPanel::OnTimer()
{
    if (!_active || _paused) return;

    // ---------- конец книги? ----------
    if (_frameStart + _visible >= _text.size()) {
        KillTimer(_hParent, TIMER_ID);
        return;
    }

    // ---------- начало НОВОГО кадра ----------
    if (_visible == 0) {
        //_bgBitmap = nullptr;           // ✨ убираем прошлую иллюстрацию
        InvalidateRect(_hParent, nullptr, FALSE);
        // ───── старт и конец кадра ─────
        size_t start = _frameStart;
        size_t end = findNextParagraph(start, SKIP_ENDS);
        if (end > _text.size()) end = _text.size();

        // ───── обновляем текущий кадр ─────
        _frameStart = start;
        _endOfFrame = end;

        if (_onFrameChange) {
            std::wstring frameText = _text.substr(_frameStart, _endOfFrame - _frameStart);
            _onFrameChange(frameText);
            // ─── [NEW] Кэшируем и запрашиваем 2 сцены ───────────────
            auto sendSceneRequest = [this](const std::wstring& chunk) {
                if (_requestedFrames.insert(chunk).second) {
                    fetchSceneAsync(chunk, [this](SceneApiResponse scene) {
                        if (scene.imageUrl.empty())
                            return;

                        if (HBITMAP bmp = _imageCache.Get(scene.imageUrl))
                        {
                            PostMessage(_hParent, WM_USER + 1, reinterpret_cast<WPARAM>(bmp), 0);
                        }
                    });
                }
            };

            // отправка текущей сцены
            sendSceneRequest(frameText);

            // попытка запроса следующей сцены (на +SKIP_ENDS)
            size_t nextStart = findNextParagraph(_endOfFrame, 0);
            size_t nextEnd = findNextParagraph(nextStart, SKIP_ENDS);
            if (nextEnd > _text.size()) nextEnd = _text.size();

            if (nextStart < nextEnd) {
                std::wstring nextFrame = _text.substr(nextStart, nextEnd - nextStart);
                sendSceneRequest(nextFrame);
            }
        }

        // ───── ничего не печатаем, если пусто ─────
        if (_frameStart == _endOfFrame) {
            _cursorPos = _frameStart;
            _visible = 0;
            _paused = false;
            _frameIdle = false;
            return;
        }
    }

    // ─── Обработка мгновенного пропуска по клику ───
    if (_pendingSkip) {
        _pendingSkip = false;
        _visible = static_cast<int>(_endOfFrame - _frameStart - 1);

        recalcTextMetrics();
        ensureScrollbar();
        InvalidateRect(_hParent, &_rcBox, FALSE);
        return;
    }
    // ---------- печатаем символ ----------
    ++_visible;


    // ---------- кадр напечатан? ----------
    if (_frameStart + _visible >= _endOfFrame) {
        _paused = true;                // ставим авто-паузу
        _frameIdle = true;
        _cursorPos = _endOfFrame;         // откуда продолжать

        // пропускаем пробелы / табы, но не \n
        while (_cursorPos < _text.size() &&
            (_text[_cursorPos] == L' ' || _text[_cursorPos] == L'\t'))
            ++_cursorPos;
    }
    // ---------- перерисовка ----------
    recalcTextMetrics();
    ensureScrollbar();
    InvalidateRect(_hParent, &_rcBox, FALSE);
}

bool ReaderPanel::OnClick(int x, int y)
{
    if (!_active) return false;

    if (PtInRect(&_rcPauseBtn, { x, y }))
    {
        _paused = !_paused;
        InvalidateRect(_hParent, &_rcBox, FALSE);
        return true;
    }
    if (PtInRect(&_rcBox, { x, y })) {
        if (_paused && _frameIdle) {
            _frameStart = _cursorPos;
            _visible = 0;
            _scrollPos = 0;
            _paused = false;
            _frameIdle = false;
            recalcTextMetrics();
            ensureScrollbar();
            InvalidateRect(_hParent, &_rcBox, FALSE);
        }
        else {
            _pendingSkip = true;
        }
        return true;
    }
    if (PtInRect(&_rcClose, { x,y }))
    {
        // закрываем
        _active = false;
        destroyScrollbar();
        KillTimer(_hParent, TIMER_ID);
        InvalidateRect(_hParent, nullptr, TRUE);
        return true;
    }
    return false;
}

bool ReaderPanel::OnMouseWheel(short delta)
{
    const int step = (delta > 0 ? -40 : 40);          // шаг в пикселях
    _scrollPos = std::clamp(_scrollPos + step, 0, _maxScroll);
    _autoScroll = false;                             // пользователь крутил вручную

    updateScrollInfo();
    InvalidateRect(_hParent, nullptr, FALSE);
    return true;                                      // событие обработано
}

// ░░░ вертикальный скроллбар ░░░───────────────────────────────────────
bool ReaderPanel::OnVScroll(HWND hScrollWnd, WPARAM wParam)
{
    // обрабатываем только собственный скроллбар
    if (hScrollWnd != _hScroll) return false;

    SCROLLINFO si{ sizeof(si), SIF_ALL };
    GetScrollInfo(_hScroll, SB_CTL, &si);
    int pos = si.nPos;

    switch (LOWORD(wParam))
    {
    case SB_LINEUP:      pos -= 40;         break;
    case SB_LINEDOWN:    pos += 40;         break;
    case SB_PAGEUP:      pos -= si.nPage;   break;
    case SB_PAGEDOWN:    pos += si.nPage;   break;
    case SB_THUMBTRACK:  pos = si.nTrackPos; break;
    case SB_TOP:         pos = 0;          break;
    case SB_BOTTOM:      pos = _maxScroll; break;
    default: return false;                  // ничего не делаем
    }

    _scrollPos = std::clamp(pos, 0, _maxScroll);
    _autoScroll = false;

    updateScrollInfo();
    InvalidateRect(_hParent, nullptr, FALSE);
    return true;                                  // событие обработано
}

// ──────────────────────────────────────────────
//             helpers
// ──────────────────────────────────────────────
void ReaderPanel::recalcTextMetrics() {
    if (_text.empty() || _visible == 0)
        return;

    HDC hdc = GetDC(_hParent);
    HFONT oldFont = (HFONT)SelectObject(hdc, _font);  // ← правильно

    RECT rc = _rcBox;
    rc.right -= SCROLL_W;
    InflateRect(&rc, -TEXT_MARGIN, -TEXT_MARGIN);

    const wchar_t* slice = _text.c_str() + _frameStart;
    int len = static_cast<int>(_visible);

    DrawTextW(hdc, slice, len, &rc,
        DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_TOP);

    int contentHeight = rc.bottom - rc.top;
    _maxScroll = max(0, contentHeight - (_rcBox.bottom - _rcBox.top));
    _textHeight = contentHeight;

    if (_scrollPos > _maxScroll)
        _scrollPos = _maxScroll;

    SelectObject(hdc, oldFont);     // ← возвращаем обратно
    ReleaseDC(_hParent, hdc);
}



void ReaderPanel::destroyScrollbar()
{
    if (_hScroll) { DestroyWindow(_hScroll); _hScroll = nullptr; }
}

// 1) реализация нового метода
void ReaderPanel::SetBackground(HBITMAP bmp)
{
    //if (!bmp) return;                        // 🔹 ничего — выходим
    _bgBitmap = bmp;
    InvalidateRect(_hParent, nullptr, FALSE);
}