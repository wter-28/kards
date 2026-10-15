#include "roipicker.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

RoiCanvas::RoiCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(640, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

void RoiCanvas::setImage(const QImage& image)
{
    m_image = image.convertToFormat(QImage::Format_RGB32);
    m_selection = QRect();
    updateTransform();
    update();
}

void RoiCanvas::setSelection(const QRect& imageRect)
{
    m_selection = imageRect.normalized();
    update();
}

QRect RoiCanvas::selection() const
{
    return m_selection.normalized();
}

void RoiCanvas::updateTransform()
{
    if (m_image.isNull() || width() <= 0 || height() <= 0)
    {
        m_scale = 1.0;
        m_offset = QPoint(0, 0);
        return;
    }

    const double scaleX = static_cast<double>(width()) / static_cast<double>(m_image.width());
    const double scaleY = static_cast<double>(height()) / static_cast<double>(m_image.height());
    m_scale = qMin(scaleX, scaleY);
    if (m_scale > 1.0)
    {
        m_scale = 1.0;
    }

    const int drawnWidth = static_cast<int>(m_image.width() * m_scale);
    const int drawnHeight = static_cast<int>(m_image.height() * m_scale);
    m_offset = QPoint((width() - drawnWidth) / 2, (height() - drawnHeight) / 2);
}

QPoint RoiCanvas::toWidget(const QPoint& imagePoint) const
{
    return QPoint(m_offset.x() + static_cast<int>(imagePoint.x() * m_scale),
                  m_offset.y() + static_cast<int>(imagePoint.y() * m_scale));
}

QPoint RoiCanvas::toImage(const QPoint& widgetPoint) const
{
    if (m_scale <= 0.0)
    {
        return QPoint(0, 0);
    }
    return QPoint(static_cast<int>(std::lround((widgetPoint.x() - m_offset.x()) / m_scale)),
                  static_cast<int>(std::lround((widgetPoint.y() - m_offset.y()) / m_scale)));
}

void RoiCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateTransform();
}

void RoiCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(20, 20, 20));

    if (m_image.isNull())
    {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("没有截图"));
        return;
    }

    const int drawnWidth = static_cast<int>(m_image.width() * m_scale);
    const int drawnHeight = static_cast<int>(m_image.height() * m_scale);

    painter.drawImage(QRect(m_offset.x(), m_offset.y(), drawnWidth, drawnHeight), m_image);

    const QRect normalized = m_selection.normalized();
    if (!normalized.isEmpty())
    {
        const QPoint topLeft = toWidget(normalized.topLeft());
        const QPoint bottomRight = toWidget(normalized.bottomRight());
        const QRect target(topLeft, bottomRight);

        painter.setPen(QPen(QColor(0, 255, 0), 2));
        painter.drawRect(target);
        painter.fillRect(target, QColor(0, 255, 0, 40));
    }
}

void RoiCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_image.isNull())
    {
        return;
    }

    m_dragging = true;
    m_dragStart = toImage(event->pos());
    m_selection = QRect();
    update();
}

void RoiCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
    {
        return;
    }

    QPoint current = toImage(event->pos());
    current.setX(qBound(0, current.x(), m_image.width() - 1));
    current.setY(qBound(0, current.y(), m_image.height() - 1));

    m_selection = QRect(m_dragStart, current).normalized();
    update();
}

void RoiCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
    {
        return;
    }

    m_dragging = false;
    emit selectionChanged(selection());
}

RoiPickerDialog::RoiPickerDialog(const QString& title,
                                 const QRect& captureRect,
                                 const QRect& initialScreenRect,
                                 QWidget* parent)
    : QDialog(parent),
      m_captureRect(captureRect)
{
    setWindowTitle(QStringLiteral("框选区域 - %1").arg(title));
    resize(1000, 640);

    auto* layout = new QVBoxLayout(this);

    m_infoLabel = new QLabel(this);
    layout->addWidget(m_infoLabel);

    m_canvas = new RoiCanvas(this);
    layout->addWidget(m_canvas, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* recapture = buttons->addButton(QStringLiteral("重新截图"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(recapture, &QPushButton::clicked, this, &RoiPickerDialog::onRecapture);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_canvas, &RoiCanvas::selectionChanged, this, &RoiPickerDialog::onSelectionChanged);

    onRecapture();

    if (!initialScreenRect.isEmpty() && !m_captureRect.isEmpty())
    {
        m_canvas->setSelection(initialScreenRect.translated(-m_captureRect.topLeft()));
        onSelectionChanged(m_canvas->selection());
    }
}

void RoiPickerDialog::onRecapture()
{
    const QImage screen = m_capture.grabDesktop();
    m_captureRect = m_capture.lastCaptureRect();
    m_canvas->setImage(screen);
    onSelectionChanged(m_canvas->selection());
}

void RoiPickerDialog::onSelectionChanged(const QRect& imageRect)
{
    if (imageRect.isEmpty())
    {
        m_infoLabel->setText(QStringLiteral("在截图上按住鼠标左键拖拽，框出该模板在游戏中的搜索区域（留出一定余量）"));
        return;
    }

    const QRect screenRect = imageRect.translated(m_captureRect.topLeft());
    m_infoLabel->setText(QStringLiteral("区域(屏幕坐标)  x=%1  y=%2  w=%3  h=%4")
                             .arg(screenRect.x())
                             .arg(screenRect.y())
                             .arg(screenRect.width())
                             .arg(screenRect.height()));
}

QRect RoiPickerDialog::selection() const
{
    const QRect imageRect = m_canvas->selection();
    if (imageRect.isEmpty())
    {
        return QRect();
    }
    return imageRect.translated(m_captureRect.topLeft());
}
