#include "screencapture.h"

#include <QtGlobal>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

ScreenCapture::ScreenCapture()
{
}

ScreenCapture::~ScreenCapture()
{
    release();
}

QRect ScreenCapture::virtualDesktopRect()
{
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (w <= 0 || h <= 0)
    {
        x = 0;
        y = 0;
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    return QRect(x, y, w, h);
}

void ScreenCapture::release()
{
    if (m_memDc != nullptr && m_oldBitmap != nullptr)
    {
        SelectObject(static_cast<HDC>(m_memDc), static_cast<HBITMAP>(m_oldBitmap));
    }
    if (m_bitmap != nullptr)
    {
        DeleteObject(static_cast<HBITMAP>(m_bitmap));
        m_bitmap = nullptr;
    }
    if (m_memDc != nullptr)
    {
        DeleteDC(static_cast<HDC>(m_memDc));
        m_memDc = nullptr;
    }

    m_oldBitmap = nullptr;
    m_bits = nullptr;
    m_width = 0;
    m_height = 0;
}

bool ScreenCapture::ensureResources(int width, int height)
{
    if (m_memDc != nullptr && m_width == width && m_height == height && m_bits != nullptr)
    {
        return true;
    }

    release();

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr)
    {
        return false;
    }

    m_memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // 负高度 => 自上而下存储
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);

    ReleaseDC(nullptr, screenDc);

    if (m_memDc == nullptr || bitmap == nullptr || bits == nullptr)
    {
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }
        if (m_memDc != nullptr)
        {
            DeleteDC(static_cast<HDC>(m_memDc));
            m_memDc = nullptr;
        }
        return false;
    }

    m_bitmap = bitmap;
    m_bits = static_cast<unsigned char*>(bits);
    m_oldBitmap = SelectObject(static_cast<HDC>(m_memDc), bitmap);
    m_width = width;
    m_height = height;

    return true;
}

QImage ScreenCapture::grabDesktop()
{
    const QRect virtualRect = virtualDesktopRect();
    m_captureRect = virtualRect;

    if (!ensureResources(virtualRect.width(), virtualRect.height()))
    {
        return QImage();
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr)
    {
        return QImage();
    }

    BitBlt(static_cast<HDC>(m_memDc),
           0,
           0,
           virtualRect.width(),
           virtualRect.height(),
           screenDc,
           virtualRect.x(),
           virtualRect.y(),
           SRCCOPY);

    ReleaseDC(nullptr, screenDc);

    QImage view(m_bits,
                virtualRect.width(),
                virtualRect.height(),
                virtualRect.width() * 4,
                QImage::Format_RGB32);

    return view.copy();
}

QRect ScreenCapture::lastCaptureRect() const
{
    return m_captureRect;
}
