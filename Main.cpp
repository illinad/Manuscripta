// Main.cpp — entry point for MANUSCRIPTA
// cl /EHsc /DUNICODE /D_UNICODE Main.cpp MenuWindow.cpp user32.lib gdi32.lib comctl32.lib

#include <windows.h>
#include "MenuWindow.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MenuWindow menu(hInst);
    return menu.Run(nCmdShow);
}
