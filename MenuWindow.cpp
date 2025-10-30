// MenuWindow.cpp
// ───────────────────────────────────────────────────────────
// Главное окно: вывод заставки-меню и делегирование событий
// компоненту ReaderPanel, когда активен режим чтения.
// ───────────────────────────────────────────────────────────
#include <windows.h>
#include <windowsx.h>      // ← GET_X_LPARAM / GET_Y_LPARAM
#include <memory>          // ← std::unique_ptr / make_unique
#include <utility>
#include "MenuWindow.h"
#include "FileLoader.h"     // selectTxtFile / loadTextFileW
#include "Resource.h"       // ID кнопок
#include "ImageCache.h"
#include "SceneFetcher.h"
#include "config.h"
#include <cmath>

// ───── локальные константы оформления меню ─────
namespace {
    constexpr int  WIN_W = 1920;
    constexpr int  WIN_H = 1280;
    constexpr int  BTN_W = 400;
    constexpr int  BTN_H = 80;
    constexpr int  BTN_Y1 = 400;        // Open existing
    constexpr int  BTN_Y2 = 500;        // Exit

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

void MenuWindow::paintMenu(HDC hdc)
{
    RECT rc; GetClientRect(_hWnd, &rc);
    HBRUSH brBG = CreateSolidBrush(CLR_BG);
    FillRect(hdc, &rc, brBG);
    DeleteObject(brBG);

    SetBkMode(hdc, TRANSPARENT);

    // ─── логотип показываем ТОЛЬКО когда не в режиме загрузки ───
    //if (!_forceSpinner)                 // <-- добавлено условие
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

    // ─── спиннер ───
    if (_showSpinner)
    {
        const int cx = WIN_W / 8;
        const int cy = (WIN_H / 4) * 3;
        const int r = 32;

        for (int i = 0; i < 12; ++i)
        {
            float angle = (_spinnerAngle + i * 30) * 3.14159f / 180.0f;
            int   x = int(cx + r * cos(angle));
            int   y = int(cy + r * sin(angle));
            int   alpha = 255 - i * (255 / 12);

            HBRUSH br = CreateSolidBrush(RGB(alpha, alpha, alpha));
            RECT dot = { x - 3, y - 3, x + 3, y + 3 };
            FillRect(hdc, &dot, br);
            DeleteObject(br);
        }
    }
}


// ───────────────────────────────────────────────────────────
//                 MenuWindow implementation
// ───────────────────────────────────────────────────────────
MenuWindow::MenuWindow(HINSTANCE hInst) : _hInst(hInst)
{
    _imgCache = std::make_shared<ImageCache>();

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
    _btnExit = createButton(ID_BTN_EXIT, BTN_Y2, L"Exit");

    ShowWindow(_hWnd, SW_SHOW);
    UpdateWindow(_hWnd);
}

MenuWindow::~MenuWindow()
{
    DeleteObject(_fontTitle);
    DeleteObject(_fontSub);
    DeleteObject(_fontBtn);
}


// helper ------------------------------------------------------------------
namespace
{
    constexpr UINT WM_SET_BG = WM_USER + 1;    // передаём HBITMAP в ReaderPanel
    constexpr UINT WM_SCENE_ERROR = WM_USER + 2; // передаём ошибку генерации
    constexpr UINT IDT_SPINNER = 2;            // таймер для крутилки

    void dispatchSceneToUi(const std::shared_ptr<ImageCache>& cache, HWND hwnd, const std::wstring& requestKey, SceneApiResponse&& scene)
    {
        if (!cache)
            return;

        scene.requestId = requestKey;

        if (!scene.imageUrl.empty())
        {
            if (HBITMAP bmp = cache->Get(scene.imageUrl))
            {
                PostMessage(hwnd, WM_SET_BG, reinterpret_cast<WPARAM>(bmp), 0);
            }
            return;
        }

        if (!scene.errorMessage.empty())
        {
            auto payload = new SceneApiResponse(std::move(scene));
            if (!PostMessage(hwnd, WM_SCENE_ERROR, reinterpret_cast<WPARAM>(payload), 0))
                delete payload;
        }
    }

    //-----------------------------------------------------------------------
    // утилита: отправить сцену на асинхр. загрузку (если ещё не запрошена)
    //-----------------------------------------------------------------------
    void enqueueScene(ReaderPanel* reader,
        const std::shared_ptr<ImageCache>& cache,
        HWND hwnd,
        const std::wstring& chunk)
    {
        if (!reader || !reader->TryMarkFrameRequested(chunk))   // уже была
            return;

        fetchSceneAsync(chunk, [cache, hwnd, chunk](SceneApiResponse r)
            {
                dispatchSceneToUi(cache, hwnd, chunk, std::move(r));
            });
    }
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

        // 2. Ensure reader exists ------------------------------------------
        if (!_reader)
            _reader = std::make_unique<ReaderPanel>(_hInst, _hWnd);

        std::wstring text = manuscripta::loadTextFileW(path);

        // 3. Show spinner *before* blocking network / disk work -------------
        _forceSpinner = true;   // Hide logo, block ReaderPanel paint
        _showSpinner = true;
        SetTimer(_hWnd, IDT_SPINNER, 100, nullptr);
        InvalidateRect(_hWnd, nullptr, FALSE);
        UpdateWindow(_hWnd);    // Immediate repaint so spinner is visible

        // Give the spinner at least one WM_PAINT / WM_TIMER  tick
        MSG m;
        while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }

        // 4. Register frame‑change callback --------------------------------
        _reader->SetOnFrameChange([this](const std::wstring& frame)
            {
                if (_reader->TryMarkFrameRequested(frame))
                {
                    auto cache = _imgCache;
                    HWND hwnd = _hWnd;
                    fetchSceneAsync(frame, [cache, hwnd, frame](SceneApiResponse scene)
                        {
                            dispatchSceneToUi(cache, hwnd, frame, std::move(scene));
                        });
                }
            });

        // 5. Feed the text (first frame will trigger the callback itself) ---
        _reader->SetText(text);

        // 6. Pre‑calculate first two chunks for scene fetching --------------
        std::wstring scene1 = _reader->GetFrameText(0, SKIP_ENDS);
        size_t nextStart = _reader->findNextParagraph(0, SKIP_ENDS);
        std::wstring scene2 = _reader->GetFrameText(nextStart, SKIP_ENDS);

        auto tryRequest = [this](const std::wstring& chunk)
            {
                if (_reader->TryMarkFrameRequested(chunk))
                {
                    auto cache = _imgCache;
                    HWND hwnd = _hWnd;
                    fetchSceneAsync(chunk, [cache, hwnd, chunk](SceneApiResponse scene)
                        {
                            dispatchSceneToUi(cache, hwnd, chunk, std::move(scene));
                        });
                }
            };

        // 7. Synchronous fetch for the very first scene ---------------------
        if (_reader->TryMarkFrameRequested(scene1))
        {
            SceneApiResponse first = fetchScene(scene1);
            first.requestId = scene1;
            if (!first.imageUrl.empty() && _imgCache)
            {
                if (HBITMAP bmp = _imgCache->Get(first.imageUrl))
                    _reader->SetBackground(bmp);
            }
            else if (!first.errorMessage.empty())
            {
                _reader->SetSceneError(first);
            }
        }

        // 8. Kick off asynchronous fetches for scene1 / scene2 -------------
        tryRequest(scene1);
        tryRequest(scene2);

        // 9. Hide spinner & reveal reader panel ----------------------------
        KillTimer(_hWnd, IDT_SPINNER);
        _forceSpinner = false;
        _showSpinner = false;
        InvalidateRect(_hWnd, nullptr, FALSE);

        // 10. Hide menu buttons ---------------------------------------------
        ShowWindow(_btnOpen, SW_HIDE);
        ShowWindow(_btnExit, SW_HIDE);
        break;
    }

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
        if (dis->CtlID == ID_BTN_OPEN || dis->CtlID == ID_BTN_EXIT)
        {
            FillRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(LTGRAY_BRUSH));
            SetBkMode(dis->hDC, TRANSPARENT);
            SelectObject(dis->hDC, self->_fontBtn);
            SetTextColor(dis->hDC, RGB(13, 13, 15));
            LPCWSTR txt = (dis->CtlID == ID_BTN_OPEN) ? L"Open existing" : L"Exit";
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
        if (self->_reader && self->_reader->IsActive() && !self->_forceSpinner)
            self->_reader->OnPaint(hdc);
        else
            self->paintMenu(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (self->_reader && self->_reader->IsActive())
            self->_reader->OnTimer();

        if (self->_showSpinner)
        {
            self->_spinnerAngle = (self->_spinnerAngle + 15) % 360;
            InvalidateRect(self->_hWnd, nullptr, FALSE);
            return 0;
        }
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
                self->_reader.reset();
                ShowWindow(self->_btnOpen, SW_SHOW);
                ShowWindow(self->_btnExit, SW_SHOW);
            }
            return 0;
        }
        break;

    case WM_USER + 1: // пришла картинка
    {
        HBITMAP bmp = (HBITMAP)wParam;
        if (self->_reader)
        {
            self->_reader->SetBackground(bmp);

            // выключаем спиннер, включаем рендер ReaderPanel
            self->_forceSpinner = false;
            self->_showSpinner = false;
            KillTimer(self->_hWnd, IDT_SPINNER);

            InvalidateRect(self->_hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_SCENE_ERROR:
    {
        auto payload = reinterpret_cast<SceneApiResponse*>(wParam);
        if (payload && self->_reader)
        {
            self->_reader->SetSceneError(*payload);
        }

        delete payload;

        self->_forceSpinner = false;
        self->_showSpinner = false;
        KillTimer(self->_hWnd, IDT_SPINNER);
        InvalidateRect(self->_hWnd, nullptr, FALSE);
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
