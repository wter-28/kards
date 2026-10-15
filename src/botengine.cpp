#include "botengine.h"

#include "clicker.h"

#include <QDir>
#include <QPainter>
#include <QPen>

BotEngine::BotEngine(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &BotEngine::onTick);
}

BotEngine::~BotEngine()
{
}

void BotEngine::setConfig(const AppConfig& config)
{
    m_config = config;
}

AppConfig BotEngine::config() const
{
    return m_config;
}

QString BotEngine::turnKey(int turn) const
{
    switch (turn)
    {
    case 1: return QStringLiteral("turn1");
    case 2: return QStringLiteral("turn2");
    case 3: return QStringLiteral("turn3");
    case 4: return QStringLiteral("turn4");
    case 5: return QStringLiteral("turn5");
    default: break;
    }
    return QString();
}

QString BotEngine::stageName(Stage stage) const
{
    switch (stage)
    {
    case Stage::Idle:             return QStringLiteral("未运行");
    case Stage::WaitingImage1:    return QStringLiteral("等待图片1（开始）");
    case Stage::WaitingImage2:    return QStringLiteral("等待图片2（选择要替换的卡牌）");
    case Stage::WaitingImage3:    return QStringLiteral("等待图片3（确认）");
    case Stage::Playing:          return QStringLiteral("对局中：等待回合%1").arg(m_expectedTurn);
    case Stage::OpenSettings:     return QStringLiteral("等待图片10（设置齿轮）");
    case Stage::Surrender:        return QStringLiteral("等待图片11（投降）");
    case Stage::Returning:        return QStringLiteral("回主界面：点击图片1的位置");
    default: break;
    }
    return QStringLiteral("未知");
}

void BotEngine::setStage(Stage stage)
{
    if (m_stage == stage)
    {
        return;
    }
    m_stage = stage;
    emit stageChanged(stageName(stage));
}

void BotEngine::loadTemplates()
{
    m_entries.clear();

    QDir directory(m_config.assetsPath);
    for (const TemplateConfig& item : m_config.templates)
    {
        Entry entry;
        entry.roi = item.roi;
        entry.enabled = item.enabled;

        const QString filePath = directory.filePath(item.fileName);
        QImage image(filePath);
        if (image.isNull())
        {
            entry.valid = false;
            emit logMessage(QStringLiteral("模板加载失败：%1").arg(filePath), true);
        }
        else
        {
            entry.gray = Matcher::toGray(image);
            entry.valid = !entry.gray.isNull();
            if (entry.valid)
            {
                emit logMessage(QStringLiteral("已加载模板 %1  [%2x%3]  %4")
                                    .arg(item.displayName)
                                    .arg(entry.gray.width())
                                    .arg(entry.gray.height())
                                    .arg(item.roi.isEmpty() ? QStringLiteral("全屏搜索") : QStringLiteral("区域 %1,%2,%3,%4").arg(item.roi.x()).arg(item.roi.y()).arg(item.roi.width()).arg(item.roi.height())),
                                false);
            }
        }

        m_entries.insert(item.key, entry);
    }
}

bool BotEngine::matchKey(const QString& key, Matcher::Result* out)
{
    const auto iterator = m_entries.find(key);
    if (iterator == m_entries.end() || !iterator->valid || !iterator->enabled)
    {
        return false;
    }

    QRect roiImage;
    if (!iterator->roi.isEmpty())
    {
        roiImage = iterator->roi.translated(-m_currentOrigin);
        m_drawRois.append(iterator->roi);
    }

    Matcher::Result result = Matcher::match(m_currentGray,
                                            iterator->gray,
                                            roiImage,
                                            m_config.coarseFactor,
                                            m_config.threshold);

    if (result.found)
    {
        result.rect.translate(m_currentOrigin);
        m_drawHits.append(result.rect);
    }

    if (out != nullptr)
    {
        *out = result;
    }

    return result.found;
}

void BotEngine::performClick(const QPoint& screenPos)
{
    Clicker::click(screenPos, m_config.clickMode, m_config.targetWindowHandle);
}

void BotEngine::start()
{
    if (m_running)
    {
        return;
    }

    loadTemplates();
    for (const TemplateConfig& item : m_config.templates)
    {
        if (!item.enabled)
        {
            continue;
        }
        const auto iterator = m_entries.constFind(item.key);
        if (iterator == m_entries.constEnd() || !iterator->valid)
        {
            emit logMessage(QStringLiteral("缺少有效模板：%1，无法启动").arg(item.displayName), true);
            return;
        }
    }

    if (m_config.clickMode == Clicker::BackgroundClick && m_config.targetWindowHandle == 0)
    {
        const qint64 handle = Clicker::findWindowByTitle(m_config.targetWindowTitle);
        if (handle != 0)
        {
            m_config.targetWindowHandle = handle;
            emit logMessage(QStringLiteral("已按标题找到目标窗口：%1").arg(m_config.targetWindowTitle), false);
        }
        else
        {
            emit logMessage(QStringLiteral("未找到后台点击的目标窗口，将退化为前台真实点击"), true);
        }
    }

    m_running = true;
    m_scanCount = 0;
    m_expectedTurn = 1;
    m_hasImage1Pos = false;
    m_turnTimer.invalidate();
    m_stage = Stage::Idle;

    emit logMessage(QStringLiteral("=== 已启动 ==="), false);
    setStage(Stage::WaitingImage1);

    m_timer->start(100);
}

void BotEngine::stop()
{
    if (!m_running)
    {
        return;
    }

    m_running = false;
    m_timer->stop();
    setStage(Stage::Idle);
    emit logMessage(QStringLiteral("=== 已停止 ==="), false);
}

void BotEngine::onTick()
{
    if (!m_running)
    {
        return;
    }

    QElapsedTimer cost;
    cost.start();

    const QImage screen = m_capture.grabDesktop();
    if (screen.isNull())
    {
        emit logMessage(QStringLiteral("屏幕抓取失败"), true);
        m_timer->start(qMax(100, m_config.scanIntervalMs));
        return;
    }

    m_currentOrigin = m_capture.lastCaptureRect().topLeft();
    m_currentGray = Matcher::toGray(screen);
    m_clickedThisTick = false;
    m_drawRois.clear();
    m_drawHits.clear();

    switch (m_stage)
    {
    case Stage::Idle:
        setStage(Stage::WaitingImage1);
        break;

    case Stage::WaitingImage1:
        handleWaitingImage1();
        break;

    case Stage::WaitingImage2:
        handleWaitingImage2();
        break;

    case Stage::WaitingImage3:
        handleWaitingImage3();
        break;

    case Stage::Playing:
        handlePlaying();
        break;

    case Stage::OpenSettings:
        handleOpenSettings();
        break;

    case Stage::Surrender:
        handleSurrender();
        break;

    case Stage::Returning:
        handleReturning();
        break;

    default:
        break;
    }

    m_scanCount++;
    m_lastCostMs = static_cast<double>(cost.elapsed());
    emit statsChanged(m_scanCount, m_lastCostMs);

    buildPreview(screen);

    const int delay = m_clickedThisTick ? m_config.clickCooldownMs : m_config.scanIntervalMs;
    m_timer->start(qMax(30, delay));
}

void BotEngine::handleWaitingImage1()
{
    if (matchKey(QStringLiteral("img2"), nullptr))
    {
        emit logMessage(QStringLiteral("识别到 图片2（选择要替换的卡牌）"), false);
        setStage(Stage::WaitingImage3);
        return;
    }

    Matcher::Result result;
    if (matchKey(QStringLiteral("img1"), &result))
    {
        m_lastImage1Pos = result.rect.center();
        m_hasImage1Pos = true;
        performClick(m_lastImage1Pos);
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("识别到 图片1（开始），已点击"), false);
    }
}

void BotEngine::handleWaitingImage2()
{
    if (matchKey(QStringLiteral("img2"), nullptr))
    {
        emit logMessage(QStringLiteral("识别到 图片2（选择要替换的卡牌）"), false);
        setStage(Stage::WaitingImage3);
    }
}

void BotEngine::handleWaitingImage3()
{
    Matcher::Result result;
    if (matchKey(QStringLiteral("img3"), &result))
    {
        performClick(result.rect.center());
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("识别到 图片3（确认），已点击"), false);

        m_expectedTurn = 1;
        m_turnTimer.restart();
        setStage(Stage::Playing);
    }
}

void BotEngine::handlePlaying()
{
    if (m_expectedTurn > 5)
    {
        setStage(Stage::OpenSettings);
        return;
    }

    const QString key = turnKey(m_expectedTurn);

    Matcher::Result turnResult;
    if (matchKey(key, &turnResult))
    {
        Matcher::Result endTurnResult;
        if (matchKey(QStringLiteral("img5"), &endTurnResult))
        {
            performClick(endTurnResult.rect.center());
            m_clickedThisTick = true;
            emit logMessage(QStringLiteral("识别到 回合%1，已点击 图片5（结束回合）").arg(m_expectedTurn), false);

            m_expectedTurn++;
            m_turnTimer.restart();

            if (m_expectedTurn > 5)
            {
                emit logMessage(QStringLiteral("第5回合完成，接下来打开设置并投降"), false);
            }
        }
        else
        {
            emit logMessage(QStringLiteral("识别到 回合%1，但未找到 图片5（结束回合）").arg(m_expectedTurn), true);
        }
        return;
    }

    if (m_config.retryClickOnTimeout
        && m_turnTimer.isValid()
        && m_turnTimer.elapsed() > static_cast<qint64>(m_config.turnWaitTimeoutSec) * 1000)
    {
        Matcher::Result endTurnResult;
        if (matchKey(QStringLiteral("img5"), &endTurnResult))
        {
            performClick(endTurnResult.rect.center());
            m_clickedThisTick = true;
            emit logMessage(QStringLiteral("等待 回合%1 超时，重试点击 图片5（结束回合）").arg(m_expectedTurn), true);
            m_turnTimer.restart();
        }
    }
}

void BotEngine::handleOpenSettings()
{
    Matcher::Result result;
    if (matchKey(QStringLiteral("img10"), &result))
    {
        performClick(result.rect.center());
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("识别到 图片10（设置齿轮），已点击"), false);
        setStage(Stage::Surrender);
    }
}

void BotEngine::handleSurrender()
{
    Matcher::Result result;
    if (matchKey(QStringLiteral("img11"), &result))
    {
        performClick(result.rect.center());
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("识别到 图片11（投降），已点击"), false);
        setStage(Stage::Returning);
    }
}

void BotEngine::handleReturning()
{
    Matcher::Result result;
    if (matchKey(QStringLiteral("img1"), &result))
    {
        m_lastImage1Pos = result.rect.center();
        m_hasImage1Pos = true;
        performClick(m_lastImage1Pos);
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("识别到 图片1（开始），已点击，开始新一轮循环"), false);
        setStage(Stage::WaitingImage2);
        return;
    }

    if (m_hasImage1Pos)
    {
        performClick(m_lastImage1Pos);
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("未识别到 图片1，点击图片1的位置 (%1, %2) 继续等待").arg(m_lastImage1Pos.x()).arg(m_lastImage1Pos.y()), false);
    }
    else if (m_config.hasFallbackClickPoint)
    {
        performClick(m_config.fallbackClickPoint);
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("未识别到 图片1，点击备用坐标 (%1, %2) 继续等待").arg(m_config.fallbackClickPoint.x()).arg(m_config.fallbackClickPoint.y()), false);
    }
    else
    {
        emit logMessage(QStringLiteral("未识别到 图片1，且没有可用的点击位置"), true);
    }
}

void BotEngine::buildPreview(const QImage& screen)
{
    if (screen.isNull())
    {
        return;
    }

    const int maxWidth = 960;
    QImage scaled = screen;
    if (screen.width() > maxWidth)
    {
        scaled = screen.scaledToWidth(maxWidth, Qt::FastTransformation);
    }

    QImage frame = scaled.convertToFormat(QImage::Format_RGB32);
    const double ratio = static_cast<double>(scaled.width()) / static_cast<double>(screen.width());

    QPainter painter(&frame);

    painter.setPen(QPen(QColor(0, 160, 255), 1));
    for (const QRect& rect : m_drawRois)
    {
        const QRect imageRect = rect.translated(-m_currentOrigin);
        const QRect target(static_cast<int>(imageRect.x() * ratio),
                           static_cast<int>(imageRect.y() * ratio),
                           static_cast<int>(imageRect.width() * ratio),
                           static_cast<int>(imageRect.height() * ratio));
        painter.drawRect(target);
    }

    painter.setPen(QPen(QColor(0, 255, 0), 2));
    for (const QRect& rect : m_drawHits)
    {
        const QRect imageRect = rect.translated(-m_currentOrigin);
        const QRect target(static_cast<int>(imageRect.x() * ratio),
                           static_cast<int>(imageRect.y() * ratio),
                           static_cast<int>(imageRect.width() * ratio),
                           static_cast<int>(imageRect.height() * ratio));
        painter.drawRect(target);
    }

    painter.setPen(QPen(QColor(255, 210, 0), 2));
    painter.drawText(8, 20, stageName(m_stage));

    painter.end();

    emit frameReady(frame);
}
