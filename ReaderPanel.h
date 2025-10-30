#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include <unordered_set>
#include "ImageCache.h"
#include "SceneFetcher.h"

// ────────────────────────────────────────────────────────────────
//  Вертикальная читалка с собственным скроллбаром и «эффектом
//  появления» символов.  Полностью автономна: MenuWindow только
//  создаёт её и передаёт входящие сообщения.
// ────────────────────────────────────────────────────────────────
class ReaderPanel
{
public:
    explicit ReaderPanel(HINSTANCE hInst, HWND hParent);
    ~ReaderPanel();

    // Загрузить текст и стартовать анимацию «печати»
    void SetText(const std::wstring& txt);

    // ───── вызовы из родителя ─────
    void Resize(const RECT& rcClient);
    void OnPaint(HDC hdc);
    void OnTimer();
    bool OnMouseWheel(short delta);
    bool OnVScroll(HWND hScrollWnd, WPARAM wParam);
    bool OnClick(int x, int y);          // true → событие обработано

    bool IsActive() const { return _active; }
    void updateScrollInfo();
    std::wstring GetFirstFrame() const;
    void SetBackground(HBITMAP bmp);
    void SetSceneError(const SceneApiResponse& scene);
    void SetOnFrameChange(std::function<void(const std::wstring&)> cb);

    std::wstring GetFrameText(size_t start, int count) const;
    size_t findNextParagraph(size_t start, int count) const;
    size_t findParagraphEnd(size_t start) const;

    bool TryMarkFrameRequested(const std::wstring& frame);
private:
    // ─── scrolling state ─────────────────────────────
    int  _textHeight{ 0 };     // полная высота разметки
    bool _autoScroll{ true };  // true, пока пользователь не трогал колёсико

    void positionScrollbar();   // поставить ползунок в правый край _rcBox
    std::function<void(const std::wstring&)> _onFrameChange;
    void drawCloseButton(HDC hdc);
    void drawPauseButton(HDC hdc);
    void recalcTextMetrics();            // пересчитать высоту текста + _maxScroll
    void ensureScrollbar();              // создать/обновить _hScroll
    void destroyScrollbar();             // убрать при закрытии
    int measureHeightForRange(size_t start, size_t len) const;

    // ───── данные ─────
    HINSTANCE   _hInst{};
    HWND        _hParent{};
    HWND        _hScroll{};
    HFONT       _font{};

    std::wstring _text;
    size_t      _visible = 0;          // сколько символов уже «проявилось»
    int         _scrollPos = 0;          // текущий отступ вверх, px
    int         _maxScroll = 0;          // максимум прокрутки, px

    RECT        _rcBox{};               // серый прямоугольник
    RECT        _rcClose{};             // круг-крестик
    bool        _active = false;

    // ───── константы ─────
    static constexpr UINT TIMER_ID = 1;
    static constexpr UINT TICK_MS = 20;

    // оформление
    static constexpr COLORREF CLR_BOX = RGB(55, 55, 55);
    static constexpr COLORREF CLR_TEXT = RGB(245, 245, 245);
    static constexpr COLORREF CLR_CLOSE = RGB(55, 55, 55);
    static constexpr COLORREF CLR_SCENE_BG = RGB(24, 24, 26);
    static constexpr COLORREF CLR_SCENE_ERR = RGB(255, 120, 120);
    static constexpr int BOX_W = 1000;
    static constexpr int BOX_H = 260;
    static constexpr int BOX_R = 12;
    static constexpr int TEXT_MARGIN = 24;
    static const int SCROLL_W;
    bool _pendingSkip = false; // ждём показать до конца предложения
    bool _paused = false;
    RECT _rcPauseBtn{};
    // ───── новые переменные для кадрирования ─────
    size_t  _frameStart = 0;   // индекс первого символа текущего кадра
    size_t  _cursorPos = 0;   // куда продолжать после паузы
    bool _frameIdle = false;
    size_t _endOfFrame = 0; // конец текущего кадра (не включая)

    std::unordered_set<std::wstring> _requestedFrames;

    HBITMAP _bgBitmap = nullptr;
    ImageCache _imageCache;
    std::wstring _sceneStatusText;
    DWORD _sceneStatusCode = 0;
    DWORD _sceneWin32Error = 0;
};
