#pragma once
#include <windows.h>

void DrawDebugPoint(int x, int y, int size = 5, COLORREF color = RGB(255, 0, 0)) {
    // Get the Device Context of the entire screen (Desktop)
    // This allows us to draw over any window in Windowed Mode
    HDC hdc = GetDC(NULL);

    if (hdc) {
        // Create a colored brush
        HBRUSH brush = CreateSolidBrush(color);

        // Define the box area
        RECT rect = { x - size, y - size, x + size, y + size };

        // Draw it
        FillRect(hdc, &rect, brush);

        // Cleanup to prevent memory leaks
        DeleteObject(brush);
        ReleaseDC(NULL, hdc);
    }
}