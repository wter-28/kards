#include "clicker.h"

#include "screencapture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QThread>

namespace
{

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (IsWindowVisible(hwnd) == FALSE)
    {
        return TRUE;
    }

    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
    {
        return TRUE;
    }

    wchar_t buffer[512] = {0};
    const int copied = GetWindowTextW(hwnd, buffer, 511);
    if (copied <= 0)
    {
        return TRUE;
    }

    auto* list = reinterpret_cast<QList<QPair<qint64, QString>>*>(lParam);
    list->append(qMakePair(static_cast<qint64>(reinterpret_cast<qintptr>(hwnd)), QString::fromWCharArray(buffer)));

    return TRUE;
}

}   // namespace

void Clicker::click(const QPoint& screenPos, int mode, qint64 windowHandle)
{
    if (mode == BackgroundClick)
    {
        clickBackground(screenPos, windowHandle);
    }
    else
    {
        clickFront(screenPos);
    }
}

void Clicker::moveTo(const QPoint& screenPos)
{
    const QRect virtualRect = ScreenCapture::virtualDesktopRect();
    if (virtualRect.width() <= 1 || virtualRect.height() <= 1)
    {
        return;
    }

    long normalizedX = static_cast<long>((screenPos.x() - virtualRect.x()) * 65535.0 / (virtualRect.width() - 1));
    long normalizedY = static_cast<long>((screenPos.y() - virtualRect.y()) * 65535.0 / (virtualRect.height() - 1));
    normalizedX = qBound(0L, normalizedX, 65535L);
    normalizedY = qBound(0L, normalizedY, 65535L);

    INPUT move;
    ZeroMemory(&move, sizeof(move));
    move.type = INPUT_MOUSE;
    move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    move.mi.dx = normalizedX;
    move.mi.dy = normalizedY;

    SendInput(1, &move, sizeof(INPUT));
}

void Clicker::clickFront(const QPoint& screenPos)
{
    const QRect virtualRect = ScreenCapture::virtualDesktopRect();
    if (virtualRect.width() <= 1 || virtualRect.height() <= 1)
    {
        return;
    }

    long normalizedX = static_cast<long>((screenPos.x() - virtualRect.x()) * 65535.0 / (virtualRect.width() - 1));
    long normalizedY = static_cast<long>((screenPos.y() - virtualRect.y()) * 65535.0 / (virtualRect.height() - 1));
    normalizedX = qBound(0L, normalizedX, 65535L);
    normalizedY = qBound(0L, normalizedY, 65535L);

    INPUT moveAndDown[2];
    ZeroMemory(moveAndDown, sizeof(moveAndDown));

    moveAndDown[0].type = INPUT_MOUSE;
    moveAndDown[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    moveAndDown[0].mi.dx = normalizedX;
    moveAndDown[0].mi.dy = normalizedY;

    moveAndDown[1].type = INPUT_MOUSE;
    moveAndDown[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    SendInput(2, moveAndDown, sizeof(INPUT));

    QThread::msleep(30);

    INPUT up;
    ZeroMemory(&up, sizeof(up));
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &up, sizeof(INPUT));
}

void Clicker::clickBackground(const QPoint& screenPos, qint64 windowHandle)
{
    HWND hwnd = reinterpret_cast<HWND>(static_cast<qintptr>(windowHandle));
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE)
    {
        clickFront(screenPos);
        return;
    }

    POINT point;
    point.x = screenPos.x();
    point.y = screenPos.y();
    ScreenToClient(hwnd, &point);

    const LPARAM lParam = MAKELPARAM(static_cast<short>(point.x), static_cast<short>(point.y));

    PostMessageW(hwnd, WM_MOUSEMOVE, 0, lParam);
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    QThread::msleep(30);
    PostMessageW(hwnd, WM_LBUTTONUP, 0, lParam);
}

qint64 Clicker::findWindowByTitle(const QString& title)
{
    if (title.isEmpty())
    {
        return 0;
    }

    const std::wstring wide = title.toStdWString();
    HWND hwnd = FindWindowW(nullptr, wide.c_str());
    if (hwnd == nullptr)
    {
        return 0;
    }

    return static_cast<qint64>(reinterpret_cast<qintptr>(hwnd));
}

QList<QPair<qint64, QString>> Clicker::enumerateWindows()
{
    QList<QPair<qint64, QString>> list;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&list));
    return list;
}
