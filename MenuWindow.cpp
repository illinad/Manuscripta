// MenuWindow.cpp
// ───────────────────────────────────────────────────────────
// Главное окно: вывод заставки-меню и делегирование событий
// компоненту ReaderPanel, когда активен режим чтения.
// ───────────────────────────────────────────────────────────
#include <windows.h>
#include <windowsx.h>      // ← GET_X_LPARAM / GET_Y_LPARAM
#include <memory>          // ← std::unique_ptr / make_unique
#include "MenuWindow.h"
#include "FileLoader.h"     // selectTxtFile / loadTextFileW
#include "Resource.h"       // ID кнопок
#include "ImageLoader.h"
#include "SceneFetcher.h"
#include "config.h"
#include <exception>

// ───── локальные константы оформления меню ─────
namespace {
    constexpr int  WIN_W = 1920;
    constexpr int  WIN_H = 1280;
    constexpr int  BTN_W = 400;
    constexpr int  BTN_H = 80;
    constexpr int  BTN_Y1 = 400;        // Open existing
    constexpr int  BTN_Y2 = 500;        // Mode
    constexpr int  BTN_Y3 = 600;        // Exit

    constexpr COLORREF CLR_BG = RGB(13, 13, 15);
    constexpr COLORREF CLR_ACC = RGB(0, 195, 198);
    constexpr COLORREF CLR_TXT = RGB(239, 232, 210);
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

HWND MenuWindow::createButton(UINT id, int y, LPCWSTR text)
{
    return CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        (WIN_W - BTN_W) / 2, y, BTN_W, BTN_H,
        _hWnd, (HMENU)id, _hInst, nullptr);
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

    // ─── логотип показываем ТОЛЬКО когда не в режиме загрузки ───
    {
        SelectObject(hdc, _fontTitle);
        SetTextColor(hdc, CLR_TXT);
        RECT rTitle{ 0, 160, WIN_W, 280 };
        DrawTextW(hdc, L"MANUSCRIPTA", -1, &rTitle, DT_CENTER | DT_TOP);

        SelectObject(hdc, _fontSub);
        SetTextColor(hdc, CLR_ACC);
        RECT rSub{ 0, 300, WIN_W, 360 };
        DrawTextW(hdc, L"Read. Reshape. Resonate.", -1, &rSub,
            DT_CENTER | DT_TOP);
    }
}


// ───────────────────────────────────────────────────────────
//                 MenuWindow implementation
// ───────────────────────────────────────────────────────────
MenuWindow::MenuWindow(HINSTANCE hInst) : _hInst(hInst)
{
    // 1) регистрируем класс окна
    WNDCLASSEX wc{ sizeof(wc) };
    wc.hInstance = _hInst;
    wc.lpfnWndProc = MenuWindow::WndProc;
    wc.lpszClassName = L"ManuscriptaMain";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassEx(&wc);

    // 2) создаём окно
    _hWnd = CreateWindowW(L"ManuscriptaMain", L"MANUSCRIPTA",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
        nullptr, nullptr, _hInst, this);

    // 3) шрифты
    _fontTitle = makeFont(L"Georgia", 96, true);
    _fontSub = makeFont(L"Segoe UI", 28);
    _fontBtn = makeFont(L"Georgia", 30);

    // 4) кнопки
    _btnOpen = createButton(ID_BTN_OPEN, BTN_Y1, L"Open existing");
    _btnMode = createButton(ID_BTN_MODE, BTN_Y2, modeLabel());
    _btnExit = createButton(ID_BTN_EXIT, BTN_Y3, L"Exit");

    ShowWindow(_hWnd, SW_SHOW);
    UpdateWindow(_hWnd);
}

MenuWindow::~MenuWindow()
{
    DeleteObject(_fontTitle);
    DeleteObject(_fontSub);
    DeleteObject(_fontBtn);
    releaseBook();
}


// helper ------------------------------------------------------------------
namespace
{
    constexpr UINT WM_SET_BG = WM_USER + 1;    // фоновый поток → окно: HBITMAP + номер книги
}

//--------------------------------------------------------------------------
// Запросить иллюстрацию для сцены (если ещё не запрашивали).
// Фоновый поток не трогает ни MenuWindow, ни ReaderPanel: только сеть,
// декодирование и PostMessage. Устаревшие картинки отсекаются по _generation.
//--------------------------------------------------------------------------
void MenuWindow::requestScene(const std::wstring& chunk)
{
    if (chunk.empty() || !_reader || !_reader->TryMarkFrameRequested(chunk))
        return;

    HWND hwnd = _hWnd;
    unsigned gen = _generation;
    fetchSceneAsync(chunk, [hwnd, gen](SceneApiResponse scene)
        {
            if (scene.imageUrl.empty())
                return;
            HBITMAP bmp = downloadBitmap(scene.imageUrl);
            if (bmp && !PostMessage(hwnd, WM_SET_BG, reinterpret_cast<WPARAM>(bmp), gen))
                DeleteObject(bmp);
        });
}

//--------------------------------------------------------------------------
// Закрыть текущую книгу: читалка, её иллюстрация и все запросы в полёте
//--------------------------------------------------------------------------
void MenuWindow::releaseBook()
{
    ++_generation;
    _reader.reset();
    if (_background) { DeleteObject(_background); _background = nullptr; }
}

//--------------------------------------------------------------------------
// MenuWindow::OnCommand
//--------------------------------------------------------------------------
void MenuWindow::OnCommand(UINT id)
{
    switch (id)
    {
        // ──────────────────────────────────────────────────────────────────
    case ID_BTN_OPEN:
    {
        // 1. Pick a text file ------------------------------------------------
        std::wstring path = selectTxtFile(_hWnd);
        if (path.empty()) break;

        // 2. Read it first: a file that can't be read must not crash the app
        std::wstring text;
        try
        {
            text = manuscripta::loadTextFileW(path);
        }
        catch (const std::exception&)
        {
            MessageBoxW(_hWnd, L"Could not read the selected file.",
                L"MANUSCRIPTA", MB_OK | MB_ICONERROR);
            break;
        }

        // 3. Fresh reader for every book -----------------------------------
        releaseBook();
        _reader = std::make_unique<ReaderPanel>(_hInst, _hWnd);

        if (_textOnly)
        {
            // текстовый режим: без запросов сцен
            _reader->SetText(text);
        }
        else
        {
            // 4. Scenes the reader asks for (current + prefetch) ------------
            _reader->SetOnFrameChange([this](const std::wstring& frame)
                {
                    requestScene(frame);
                });

            _reader->SetText(text);

            // 5. The first frame starts already visible, so request it and
            //    the next one here. Everything is async: the UI never waits.
            size_t nextStart = _reader->findNextParagraph(0, SKIP_ENDS);
            requestScene(_reader->GetFrameText(0, SKIP_ENDS));
            requestScene(_reader->GetFrameText(nextStart, SKIP_ENDS));
        }

        // 6. Hide menu buttons ----------------------------------------------
        showMenuButtons(false);
        break;
    }

    // ──────────────────────────────────────────────────────────────────
    case ID_BTN_MODE:
        _textOnly = !_textOnly;
        SetWindowTextW(_btnMode, modeLabel());
        InvalidateRect(_btnMode, nullptr, TRUE);
        break;

    // ──────────────────────────────────────────────────────────────────
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
        return TRUE;
    }
    if (!self) return DefWindowProc(hWnd, msg, wParam, lParam);

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
        if (self->_reader && self->_reader->IsActive())
            self->_reader->OnPaint(hdc);
        else
            self->paintMenu(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (self->_reader && self->_reader->IsActive())
            self->_reader->OnTimer();

        break;

    case WM_MOUSEWHEEL:
        if (self->_reader && self->_reader->IsActive() &&
            self->_reader->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)))
            return 0;
        break;

    case WM_VSCROLL:
        if (self->_reader && self->_reader->IsActive() &&
            self->_reader->OnVScroll(reinterpret_cast<HWND>(lParam), wParam))
            return 0;
        break;

    case WM_LBUTTONDOWN:
        if (self->_reader && self->_reader->IsActive() &&
            self->_reader->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
        {
            // панель закрылась → возвращаем меню
            if (!self->_reader->IsActive())
            {
                self->releaseBook();
                self->showMenuButtons(true);
            }
            return 0;
        }
        break;

    case WM_SET_BG: // пришла картинка: HBITMAP теперь наш
    {
        HBITMAP bmp = reinterpret_cast<HBITMAP>(wParam);
        if (static_cast<unsigned>(lParam) != self->_generation || !self->_reader)
        {
            DeleteObject(bmp);           // от уже закрытой книги
            return 0;
        }
        self->_reader->SetBackground(bmp);
        if (self->_background) DeleteObject(self->_background);
        self->_background = bmp;
        return 0;
    }

    case WM_SIZE:
        if (self->_reader)
        {
            RECT rc; GetClientRect(hWnd, &rc);
            self->_reader->Resize(rc);
        }
        break;

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
