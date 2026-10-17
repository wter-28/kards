#pragma once

#include <QList>
#include <QPair>
#include <QPoint>
#include <QString>

class Clicker
{
public:
    enum Mode
    {
        FrontClick      = 0,   // 前台真实点击（SendInput）
        BackgroundClick = 1    // 后台消息点击（PostMessage）
    };

    // 在屏幕坐标 screenPos 处点击一次
    static void click(const QPoint& screenPos, int mode, qint64 windowHandle);

    // 把鼠标移动到屏幕坐标 screenPos（不按下），用于点击后让光标离开按钮
    static void moveTo(const QPoint& screenPos);

    // 按标题查找顶层窗口句柄，找不到返回 0
    static qint64 findWindowByTitle(const QString& title);

    // 枚举当前可见的顶层窗口（句柄, 标题）
    static QList<QPair<qint64, QString>> enumerateWindows();

private:
    static void clickFront(const QPoint& screenPos);
    static void clickBackground(const QPoint& screenPos, qint64 windowHandle);
};
