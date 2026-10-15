#pragma once

#include <QDialog>
#include <QImage>
#include <QRect>
#include <QWidget>

#include "screencapture.h"

class QLabel;

// 显示整屏截图并允许拖拽框选区域
class RoiCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit RoiCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setSelection(const QRect& imageRect);
    QRect selection() const;

signals:
    void selectionChanged(const QRect& imageRect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateTransform();
    QPoint toWidget(const QPoint& imagePoint) const;
    QPoint toImage(const QPoint& widgetPoint) const;

    QImage m_image;
    QRect  m_selection;
    QPoint m_dragStart;
    bool   m_dragging = false;

    double m_scale = 1.0;
    QPoint m_offset;
};

// ROI 框选对话框
class RoiPickerDialog : public QDialog
{
    Q_OBJECT

public:
    RoiPickerDialog(const QString& title,
                    const QRect& captureRect,
                    const QRect& initialScreenRect,
                    QWidget* parent = nullptr);

    // 返回屏幕坐标下的选择区域
    QRect selection() const;

private slots:
    void onRecapture();
    void onSelectionChanged(const QRect& imageRect);

private:
    ScreenCapture m_capture;
    QRect         m_captureRect;
    RoiCanvas*    m_canvas = nullptr;
    QLabel*       m_infoLabel = nullptr;
};
