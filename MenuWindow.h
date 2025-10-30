// MenuWindow.h — MANUSCRIPTA start menu class interface
#pragma once
#include <windows.h>
#include <memory>          // std::unique_ptr
#include "ReaderPanel.h"
#include <string>
#include "ImageCache.h"

class MenuWindow
{
public:
    explicit MenuWindow(HINSTANCE hInstance);
    ~MenuWindow();

    int Run(int nCmdShow);

private:
    bool _frameCbRegistered{ false };
    bool _forceSpinner = false;
    bool _showSpinner = false;
    int  _spinnerAngle = 0;
    // Internal helpers
    ImageCache _imgCache;
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

    HFONT     _fontTitle{};
    HFONT     _fontSub{};
    HFONT     _fontBtn{};

    std::unique_ptr<ReaderPanel> _reader;

    // helpers
    void paintMenu(HDC hdc);
    HWND createButton(UINT id, int y, LPCWSTR text);
};
