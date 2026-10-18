// MenuWindow.cpp
// ───────────────────────────────────────────────────────────
// Главное окно: вывод заставки-меню и делегирование событий
// компоненту ReaderPanel, когда активен режим чтения.
// ───────────────────────────────────────────────────────────
#include <windows.h>
#include <windowsx.h>      // ← GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>
#include <exception>
#include "MenuWindow.h"
#include "FileLoader.h"     // selectTxtFile / loadTextFileW
#include "Resource.h"       // ID кнопок
#include "ImageLoader.h"
#include "SceneFetcher.h"
#include "Settings.h"

// ───── локальные константы оформления меню ─────
namespace {
    constexpr int  WIN_W = 1600;        // желаемый размер окна при 96 DPI
    constexpr int  WIN_H = 1000;
    constexpr int  BTN_W = 400;
    constexpr int  BTN_H = 80;
    constexpr int  BTN_Y1 = 400;        // Open existing
    constexpr int  BTN_Y2 = 500;        // Mode
    constexpr int  BTN_Y3 = 600;        // Exit

    constexpr COLORREF CLR_BG = RGB(13, 13, 15);
    constexpr COLORREF CLR_ACC = RGB(0, 195, 198);
    constexpr COLORREF CLR_TXT = RGB(239, 232, 210);

    constexpr UINT   WM_SCENE_IMAGE = WM_USER + 1;   // фоновый поток → окно: SceneImage*
    constexpr size_t KEEP_SCENES = 2;                // держим картинки кадров в пределах ±2

    // Готовая картинка из фонового потока. Владение переходит окну.
    struct SceneImage
    {
        unsigned generation;
        size_t   frame;
        HBITMAP  bmp;
    };
}

// ───────────────────────────────────────────────────────────
//                      helpers
// ───────────────────────────────────────────────────────────
static HFONT makeFont(LPCWSTR face, int pt, bool bold = false)
{
    LOGFONTW lf{};
    wcscpy_s(lf.lfFaceName, face);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;

    HDC hdc = GetDC(nullptr);
    lf.lfHeight = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);

    return CreateFontIndirectW(&lf);
}

int MenuWindow::px(int logical) const
{
    UINT dpi = _hWnd ? GetDpiForWindow(_hWnd) : GetDpiForSystem();
    return MulDiv(logical, dpi ? dpi : 96, 96);
}

HWND MenuWindow::createButton(UINT id, LPCWSTR text)
{
    return CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, _hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), _hInst, nullptr);
}

void MenuWindow::layoutButtons()
{
    RECT rc; GetClientRect(_hWnd, &rc);
    const int x = (rc.right - px(BTN_W)) / 2;
    const HWND buttons[] = { _btnOpen, _btnMode, _btnExit };
    const int  ys[] = { BTN_Y1, BTN_Y2, BTN_Y3 };
    for (int i = 0; i < 3; ++i)
        if (buttons[i])
            MoveWindow(buttons[i], x, px(ys[i]), px(BTN_W), px(BTN_H), TRUE);
}

LPCWSTR MenuWindow::modeLabel() const
{
    return _textOnly ? L"Mode: Text only" : L"Mode: Illustrated";
}

void MenuWindow::showMenuButtons(bool show)
{
    const int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(_btnOpen, cmd);
    ShowWindow(_btnMode, cmd);
    ShowWindow(_btnExit, cmd);
}

void MenuWindow::paintMenu(HDC hdc)
{
    RECT rc; GetClientRect(_hWnd, &rc);
    HBRUSH brBG = CreateSolidBrush(CLR_BG);
    FillRect(hdc, &rc, brBG);
    DeleteObject(brBG);

    SetBkMode(hdc, TRANSPARENT);

    SelectObject(hdc, _fontTitle);
    SetTextColor(hdc, CLR_TXT);
    RECT rTitle{ 0, px(160), rc.right, px(280) };
    DrawTextW(hdc, L"MANUSCRIPTA", -1, &rTitle, DT_CENTER | DT_TOP);

    SelectObject(hdc, _fontSub);
    SetTextColor(hdc, CLR_ACC);
    RECT rSub{ 0, px(300), rc.right, px(360) };
    DrawTextW(hdc, L"Read. Reshape. Resonate.", -1, &rSub, DT_CENTER | DT_TOP);
}


// ───────────────────────────────────────────────────────────
//                 MenuWindow implementation
// ───────────────────────────────────────────────────────────
MenuWindow::MenuWindow(HINSTANCE hInst) : _hInst(hInst)
{
    _textOnly = settings::getDword(L"TextOnly", 1) != 0;

    // 1) регистрируем класс окна
    WNDCLASSEX wc{ sizeof(wc) };
    wc.hInstance = _hInst;
    wc.lpfnWndProc = MenuWindow::WndProc;
    wc.lpszClassName = L"ManuscriptaMain";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassEx(&wc);

    // 2) окно: желаемый размер с учётом DPI, но не больше рабочей области
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int workW = work.right - work.left, workH = work.bottom - work.top;
    const int w = (std::min)(px(WIN_W), workW * 9 / 10);
    const int h = (std::min)(px(WIN_H), workH * 9 / 10);

    _hWnd = CreateWindowW(L"ManuscriptaMain", L"MANUSCRIPTA",
        WS_OVERLAPPEDWINDOW,
        work.left + (workW - w) / 2, work.top + (workH - h) / 2, w, h,
        nullptr, nullptr, _hInst, this);

    // 3) шрифты
    _fontTitle = makeFont(L"Georgia", 96, true);
    _fontSub = makeFont(L"Segoe UI", 28);
    _fontBtn = makeFont(L"Georgia", 30);

    // 4) кнопки
    _btnOpen = createButton(ID_BTN_OPEN, L"Open existing");
    _btnMode = createButton(ID_BTN_MODE, modeLabel());
    _btnExit = createButton(ID_BTN_EXIT, L"Exit");
    layoutButtons();
}

MenuWindow::~MenuWindow()
{
    releaseBook();
    DeleteObject(_fontTitle);
    DeleteObject(_fontSub);
    DeleteObject(_fontBtn);
}

//--------------------------------------------------------------------------
// Открыть книгу
//--------------------------------------------------------------------------
void MenuWindow::openBook()
{
    // 1. Pick a text file ----------------------------------------------------
    std::wstring path = selectTxtFile(_hWnd);
    if (path.empty()) return;

    // 2. Read it first: a file that can't be read must not crash the app ------
    std::wstring text;
    try
    {
        text = manuscripta::loadTextFileW(path);
    }
    catch (const std::exception&)
    {
        MessageBoxW(_hWnd, L"Could not read the selected file.",
            L"MANUSCRIPTA", MB_OK | MB_ICONERROR);
        return;
    }
    if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos)
    {
        MessageBoxW(_hWnd, L"The selected file is empty.",
            L"MANUSCRIPTA", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 3. Fresh reader for every book, resumed where we stopped last time ----
    releaseBook();
    _bookPath = path;
    _reader = std::make_unique<ReaderPanel>(_hInst, _hWnd,
        _textOnly ? ReaderPanel::Layout::TextOnly : ReaderPanel::Layout::Illustrated);
    _reader->SetOnFrameChange([this](size_t frame) { onFrameChange(frame); });

    showMenuButtons(false);
    SetFocus(_hWnd);                     // клавиатура — читалке, а не скрытой кнопке
    _reader->SetText(text, settings::getPosition(path));
}

//--------------------------------------------------------------------------
// Переход на кадр: запомнить позицию и подтянуть иллюстрации
//--------------------------------------------------------------------------
void MenuWindow::onFrameChange(size_t frame)
{
    settings::setPosition(_bookPath, frame);
    if (_textOnly || !_reader) return;

    // картинки далёких кадров освобождаем — при возврате запросим снова
    for (auto it = _sceneImages.begin(); it != _sceneImages.end(); )
    {
        const size_t f = it->first;
        if (f + KEEP_SCENES < frame || f > frame + KEEP_SCENES)
        {
            DeleteObject(it->second);
            _requestedScenes.erase(f);
            it = _sceneImages.erase(it);
        }
        else ++it;
    }

    // своя картинка, если уже есть; иначе остаётся картинка соседнего кадра
    auto own = _sceneImages.find(frame);
    if (own != _sceneImages.end())
        _reader->SetBackground(own->second);
    else
    {
        HBITMAP fallback = nullptr;
        for (size_t d = 1; d <= KEEP_SCENES && !fallback; ++d)
        {
            if (frame >= d)
                if (auto p = _sceneImages.find(frame - d); p != _sceneImages.end()) fallback = p->second;
            if (!fallback)
                if (auto n = _sceneImages.find(frame + d); n != _sceneImages.end()) fallback = n->second;
        }
        _reader->SetBackground(fallback);
    }

    requestScene(frame);
    requestScene(frame + 1);             // предзагрузка следующей сцены
}

//--------------------------------------------------------------------------
// Запросить иллюстрацию для кадра (если ещё не запрашивали).
// Фоновый поток не трогает ни MenuWindow, ни ReaderPanel: только сеть,
// декодирование и PostMessage. Устаревшие картинки отсекаются по _generation.
//--------------------------------------------------------------------------
void MenuWindow::requestScene(size_t frame)
{
    if (!_reader || frame >= _reader->FrameCount() || !_requestedScenes.insert(frame).second)
        return;

    HWND hwnd = _hWnd;
    unsigned gen = _generation;
    fetchSceneAsync(_reader->GetFrameText(frame), [hwnd, gen, frame](SceneApiResponse scene)
        {
            if (scene.imageUrl.empty())
                return;
            HBITMAP bmp = downloadBitmap(scene.imageUrl);
            if (!bmp)
                return;
            auto* msg = new SceneImage{ gen, frame, bmp };
            if (!PostMessage(hwnd, WM_SCENE_IMAGE, 0, reinterpret_cast<LPARAM>(msg)))
            {
                DeleteObject(bmp);
                delete msg;
            }
        });
}

void MenuWindow::onSceneImage(unsigned generation, size_t frame, HBITMAP bmp)
{
    if (generation != _generation || !_reader || !_reader->IsActive())
    {
        DeleteObject(bmp);               // от уже закрытой книги
        return;
    }

    const size_t current = _reader->CurrentFrame();
    if (frame + KEEP_SCENES < current || frame > current + KEEP_SCENES)
    {
        DeleteObject(bmp);               // читатель уже ушёл далеко
        _requestedScenes.erase(frame);
        return;
    }

    HBITMAP& slot = _sceneImages[frame];
    HBITMAP old = slot;
    slot = bmp;
    if (frame == current)
        _reader->SetBackground(bmp);
    if (old) DeleteObject(old);
}

//--------------------------------------------------------------------------
// Закрыть текущую книгу: читалка, иллюстрации и все запросы в полёте
//--------------------------------------------------------------------------
void MenuWindow::releaseBook()
{
    ++_generation;
    _reader.reset();
    for (auto& kv : _sceneImages) DeleteObject(kv.second);
    _sceneImages.clear();
    _requestedScenes.clear();
    _bookPath.clear();
}

void MenuWindow::closeReaderIfDone()
{
    if (_reader && !_reader->IsActive())
    {
        releaseBook();
        showMenuButtons(true);
        InvalidateRect(_hWnd, nullptr, TRUE);
    }
}

//--------------------------------------------------------------------------
// MenuWindow::OnCommand
//--------------------------------------------------------------------------
void MenuWindow::OnCommand(UINT id)
{
    switch (id)
    {
    case ID_BTN_OPEN:
        openBook();
        break;

    case ID_BTN_MODE:
        _textOnly = !_textOnly;
        settings::setDword(L"TextOnly", _textOnly ? 1 : 0);
        SetWindowTextW(_btnMode, modeLabel());
        InvalidateRect(_btnMode, nullptr, TRUE);
        break;

    case ID_BTN_EXIT:
        PostQuitMessage(0);
        break;
    }
}


// ───────────────────────────────────────────────────────────
//                    оконная процедура
// ───────────────────────────────────────────────────────────
LRESULT CALLBACK MenuWindow::WndProc(HWND hWnd, UINT msg,
    WPARAM wParam, LPARAM lParam)
{
    auto self = reinterpret_cast<MenuWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE)
    {
        self = reinterpret_cast<MenuWindow*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        self->_hWnd = hWnd;
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    if (!self) return DefWindowProc(hWnd, msg, wParam, lParam);

    const bool reading = self->_reader && self->_reader->IsActive();

    switch (msg)
    {
        // ─── рисуем owner‑draw кнопки ───
    case WM_DRAWITEM:
    {
        auto dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlID == ID_BTN_OPEN || dis->CtlID == ID_BTN_MODE || dis->CtlID == ID_BTN_EXIT)
        {
            FillRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(LTGRAY_BRUSH));
            SetBkMode(dis->hDC, TRANSPARENT);
            SelectObject(dis->hDC, self->_fontBtn);
            SetTextColor(dis->hDC, RGB(13, 13, 15));
            LPCWSTR txt = (dis->CtlID == ID_BTN_OPEN) ? L"Open existing"
                        : (dis->CtlID == ID_BTN_MODE) ? self->modeLabel()
                        : L"Exit";
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        self->OnCommand(LOWORD(wParam));
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        if (reading)
            self->_reader->OnPaint(hdc);
        else
            self->paintMenu(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;                        // всё рисуем сами в WM_PAINT

    case WM_TIMER:
        if (reading)
            self->_reader->OnTimer();
        break;

    case WM_KEYDOWN:
        if (reading && self->_reader->OnKey(static_cast<UINT>(wParam)))
        {
            self->closeReaderIfDone();
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        if (reading && self->_reader->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)))
            return 0;
        break;

    case WM_VSCROLL:
        if (reading && self->_reader->OnVScroll(reinterpret_cast<HWND>(lParam), wParam))
            return 0;
        break;

    case WM_LBUTTONDOWN:
        if (reading && self->_reader->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
        {
            self->closeReaderIfDone();
            return 0;
        }
        break;

    case WM_RBUTTONDOWN:
        if (reading)
        {
            self->_reader->Prev();
            return 0;
        }
        break;

    case WM_SCENE_IMAGE: // пришла картинка: SceneImage и HBITMAP теперь наши
    {
        auto* img = reinterpret_cast<SceneImage*>(lParam);
        self->onSceneImage(img->generation, img->frame, img->bmp);
        delete img;
        return 0;
    }

    case WM_SIZE:
    {
        RECT rc; GetClientRect(hWnd, &rc);
        self->layoutButtons();
        if (self->_reader)
            self->_reader->Resize(rc);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    }

    case WM_DPICHANGED:
    {
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize = { self->px(720), self->px(760) };
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ───────────────────────────────────────────────────────────
//                         run loop
// ───────────────────────────────────────────────────────────
int MenuWindow::Run(int nCmdShow)
{
    ShowWindow(_hWnd, nCmdShow);
    UpdateWindow(_hWnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}
