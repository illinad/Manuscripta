// MenuWindow.h — MANUSCRIPTA start menu class interface
#pragma once
#include <windows.h>
#include <memory>          // std::unique_ptr
#include "ReaderPanel.h"
#include <string>

class MenuWindow
{
public:
    explicit MenuWindow(HINSTANCE hInstance);
    ~MenuWindow();

    int Run(int nCmdShow);

private:
    bool _frameCbRegistered{ false };
    // Internal helpers
    void RegisterClass();
    void CreateMainWindow(int nCmdShow);

    void OnPaint();
    void OnCommand(UINT id);
    void OnTimer();
    void OnClick(int x, int y);

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

    std::unique_ptr<ReaderPanel> _reader;
    HBITMAP   _background{};      // текущая иллюстрация, владеем ею
    unsigned  _generation = 0;    // номер открытой книги: картинки от прошлых отбрасываются

    // helpers
    void paintMenu(HDC hdc);
    HWND createButton(UINT id, int y, LPCWSTR text);
    LPCWSTR modeLabel() const;
    void showMenuButtons(bool show);
    void requestScene(const std::wstring& chunk);
    void releaseBook();
};
