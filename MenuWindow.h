// MenuWindow.h — MANUSCRIPTA start menu class interface
#pragma once
#include <windows.h>
#include <memory>          // std::unique_ptr
#include <map>
#include <set>
#include <string>
#include "ReaderPanel.h"

class MenuWindow
{
public:
    explicit MenuWindow(HINSTANCE hInstance);
    ~MenuWindow();

    int Run(int nCmdShow);

private:
    void OnCommand(UINT id);
    void openBook();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    //---------------------------------------------------------------------
    // Data members
    //---------------------------------------------------------------------
    HINSTANCE _hInst{};
    HWND      _hWnd{};
    HWND      _btnOpen{};
    HWND      _btnExit{};
    HWND      _btnMode{};

    bool      _textOnly = true;   // режим чтения: только текст / с иллюстрациями

    HFONT     _fontTitle{};
    HFONT     _fontSub{};
    HFONT     _fontBtn{};

    // ───── открытая книга ─────
    std::unique_ptr<ReaderPanel> _reader;
    std::wstring _bookPath;
    unsigned  _generation = 0;    // номер открытой книги: картинки от прошлых отбрасываются
    std::map<size_t, HBITMAP> _sceneImages;   // кадр → иллюстрация (только рядом с текущим)
    std::set<size_t> _requestedScenes;        // кадры, для которых запрос уже отправлен

    // helpers
    int  px(int logical) const;
    void paintMenu(HDC hdc);
    HWND createButton(UINT id, LPCWSTR text);
    void layoutButtons();
    LPCWSTR modeLabel() const;
    void showMenuButtons(bool show);

    void onFrameChange(size_t frame);
    void requestScene(size_t frame);
    void onSceneImage(unsigned generation, size_t frame, HBITMAP bmp);
    void closeReaderIfDone();
    void releaseBook();
};
