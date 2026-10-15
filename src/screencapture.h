#pragma once

#include <QImage>
#include <QRect>

// 使用 Win32 GDI 抓取整个虚拟桌面的屏幕画面
class ScreenCapture
{
public:
    ScreenCapture();
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // 整个虚拟桌面的矩形（屏幕坐标，可能是多显示器拼接）
    static QRect virtualDesktopRect();

    // 抓取一帧画面；返回的图片像素 (0,0) 对应 virtualDesktopRect().topLeft()
    QImage grabDesktop();

    // 最近一次抓取对应的屏幕矩形
    QRect lastCaptureRect() const;

private:
    bool ensureResources(int width, int height);
    void release();

    void*          m_memDc = nullptr;
    void*          m_bitmap = nullptr;
    void*          m_oldBitmap = nullptr;
    unsigned char* m_bits = nullptr;
    int            m_width = 0;
    int            m_height = 0;
    QRect          m_captureRect;
};
