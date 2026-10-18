#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ────────────────────────────────────────────────────────────────
//  Читалка: делит текст на кадры (по SKIP_ENDS абзацев), печатает
//  кадр по буквам, листается вперёд/назад мышью и клавиатурой.
//  MenuWindow только создаёт её и передаёт входящие сообщения.
// ────────────────────────────────────────────────────────────────
class ReaderPanel
{
public:
    enum class Layout { TextOnly, Illustrated };

    ReaderPanel(HINSTANCE hInst, HWND hParent, Layout layout);
    ~ReaderPanel();

    // Загрузить текст и начать печать с кадра startFrame (сохранённая позиция)
    void SetText(const std::wstring& txt, size_t startFrame = 0);

    // ───── вызовы из родителя ─────
    void Resize(const RECT& rcClient);
    void OnPaint(HDC hdc);
    void OnTimer();
    bool OnMouseWheel(short delta);
    bool OnVScroll(HWND hScrollWnd, WPARAM wParam);
    bool OnClick(int x, int y);          // true → событие обработано
    bool OnKey(UINT vk);                 // true → событие обработано

    // ───── навигация ─────
    void Next();                         // допечатать кадр или перейти к следующему
    void Prev();                         // предыдущий кадр (сразу целиком)
    void Close();

    bool   IsActive() const { return _active; }
    size_t FrameCount() const { return _frames.size(); }
    size_t CurrentFrame() const { return _frame; }
    std::wstring GetFrameText(size_t frame) const;

    // Иллюстрация над текстом. Не владеем: картинкой управляет MenuWindow.
    void SetBackground(HBITMAP bmp);

    // Вызывается при переходе на кадр (в том числе на стартовый в SetText)
    void SetOnFrameChange(std::function<void(size_t)> cb);

private:
    struct Frame { size_t start, end; };   // [start, end) в _text

    void buildFrames();
    void goToFrame(size_t frame, bool animate);
    size_t frameLength() const;
    bool   frameDone() const { return _visible >= frameLength(); }

    // ───── раскладка и метрики ─────
    void updateFonts();
    int  px(int logical) const { return MulDiv(logical, _dpi, 96); }
    RECT textRect() const;               // где печатается текст (без прокрутки)
    void recalcTextMetrics();            // высота текста, _maxScroll, автопрокрутка
    void ensureScrollbar();
    void destroyScrollbar();
    void setScroll(int pos, bool byUser);

    void drawCloseButton(HDC hdc);
    void drawPauseButton(HDC hdc);
    void drawFooter(HDC hdc);

    void changeFontSize(int deltaPt);
    void changeSpeed(int delta);

    // ───── данные ─────
    HINSTANCE   _hInst{};
    HWND        _hParent{};
    HWND        _hScroll{};
    HFONT       _font{};
    HFONT       _fontSmall{};
    Layout      _layout;
    UINT        _dpi = 96;
    int         _fontPt = 14;            // размер текста, pt
    int         _speed = 1;              // символов за тик таймера

    std::wstring       _text;
    std::vector<Frame> _frames;
    size_t      _frame = 0;              // текущий кадр
    size_t      _visible = 0;            // сколько символов кадра уже «проявилось»
    bool        _paused = false;         // пауза печати (пробел / кнопка)
    bool        _active = false;

    int         _scrollPos = 0;          // текущий отступ вверх, px
    int         _maxScroll = 0;          // максимум прокрутки, px
    int         _textHeight = 0;         // полная высота разметки видимой части
    bool        _userScrolled = false;   // пользователь отмотал вверх → не автопрокручиваем

    RECT        _rcBox{};                // серый прямоугольник с текстом
    RECT        _rcImage{};              // область иллюстрации
    RECT        _rcClose{};              // круг-крестик
    RECT        _rcPauseBtn{};
    RECT        _rcFooter{};             // подсказки и прогресс под текстом

    HBITMAP     _bgBitmap = nullptr;
    std::function<void(size_t)> _onFrameChange;

    // ───── константы ─────
    static constexpr UINT TIMER_ID = 1;
    static constexpr UINT TICK_MS = 20;
    static constexpr size_t MAX_FRAME_CHARS = 2500;  // длиннее — режем по строке/предложению

    static constexpr COLORREF CLR_BOX = RGB(55, 55, 55);
    static constexpr COLORREF CLR_TEXT = RGB(245, 245, 245);
    static constexpr COLORREF CLR_HINT = RGB(130, 130, 130);
    static constexpr COLORREF CLR_CLOSE = RGB(55, 55, 55);
    static constexpr int BOX_R = 12;
    static constexpr int TEXT_MARGIN = 24;
};
