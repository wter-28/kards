#include "botengine.h"

#include "clicker.h"

#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QPen>
#include <QThread>

namespace
{

// 累计点击“结束回合”达到该次数后，执行 设置齿轮 -> 投降
constexpr int kSurrenderThreshold = 5;

}   // namespace

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

QString BotEngine::stageName(Stage stage) const
{
    switch (stage)
    {
    case Stage::Idle:            return QStringLiteral("未运行");
    case Stage::LogicB_WaitStart: return QStringLiteral("逻辑b：等待「开始」");
    case Stage::LogicA_InGame:    return QStringLiteral("逻辑a：对局中（x=%1/%2）").arg(m_endTurnCount).arg(kSurrenderThreshold);
    case Stage::OpenMenu:         return QStringLiteral("主逻辑：等待「设置齿轮」");
    case Stage::Surrender:        return QStringLiteral("主逻辑：等待「投降」");
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
        const QImage image(filePath);
        if (image.isNull())
        {
            entry.valid = false;
            emit logMessage(QStringLiteral("模板加载失败：%1").arg(filePath), true);
        }
        else
        {
            entry.gray = Matcher::toEdge(Matcher::toGray(image));
            entry.valid = !entry.gray.isNull();
            if (entry.valid)
            {
                emit logMessage(QStringLiteral("已加载模板 %1  [%2x%3]  %4")
                                    .arg(item.displayName)
                                    .arg(entry.gray.width())
                                    .arg(entry.gray.height())
                                    .arg(item.roi.isEmpty()
                                             ? QStringLiteral("全屏搜索")
                                             : QStringLiteral("区域 %1,%2,%3,%4").arg(item.roi.x()).arg(item.roi.y()).arg(item.roi.width()).arg(item.roi.height())),
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

    Matcher::Result result = Matcher::match(m_currentEdge,
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

bool BotEngine::detectOcrText(const QString& target, const QRect& screenRoi, QPoint* screenPos)
{
    if (m_currentScreen.isNull() || target.isEmpty())
    {
        return false;
    }

    // 相对整张截图的裁剪区域（空 ROI 表示全屏）
    QRect crop = m_currentScreen.rect();
    if (!screenRoi.isEmpty())
    {
        crop = screenRoi.translated(-m_currentOrigin).intersected(m_currentScreen.rect());
        if (crop.isEmpty())
        {
            return false;
        }
    }

    QImage region = m_currentScreen.copy(crop);

    // 限制尺寸，避免超过 OCR 引擎可处理的上限
    constexpr int kMaxOcrDimension = 2000;
    double scale = 1.0;
    if (region.width() > kMaxOcrDimension || region.height() > kMaxOcrDimension)
    {
        scale = static_cast<double>(kMaxOcrDimension) / static_cast<double>(qMax(region.width(), region.height()));
        region = region.scaled(qMax(1, qRound(region.width() * scale)),
                               qMax(1, qRound(region.height() * scale)),
                               Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    }

    QList<OcrService::Word> words;
    QString error;
    if (!m_ocr.recognize(region, &words, &error))
    {
        // 只有子进程彻底不可用时才报错，避免每轮刷屏
        if (!m_ocr.isRunning())
        {
            emit logMessage(error, true);
        }
        return false;
    }

    QRect box;
    if (!OcrService::findText(words, target, &box))
    {
        return false;
    }

    QRect boxInCrop = box;
    if (scale != 1.0)
    {
        boxInCrop = QRect(qRound(box.x() / scale),
                          qRound(box.y() / scale),
                          qRound(box.width() / scale),
                          qRound(box.height() / scale));
    }

    if (screenPos != nullptr)
    {
        *screenPos = m_currentOrigin + crop.topLeft() + boxInCrop.center();
    }
    return true;
}

void BotEngine::performClick(const QPoint& screenPos)
{
    Clicker::click(screenPos, m_config.clickMode, m_config.targetWindowHandle);
}

// 单次点击容易因误触/丢帧而落空，除结算页外的所有点击都补一次，确保生效
void BotEngine::performClickTwice(const QPoint& screenPos)
{
    performClick(screenPos);
    QThread::msleep(140);
    performClick(screenPos);
}

void BotEngine::clickScreenCenter(int times)
{
    const QPoint center = ScreenCapture::virtualDesktopRect().center();
    for (int i = 0; i < times; ++i)
    {
        performClick(center);
        if (i + 1 < times)
        {
            QThread::msleep(120);
        }
    }
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

    if (m_config.contOcrEnabled)
    {
        emit logMessage(QStringLiteral("正在启动中文 OCR 引擎（首次约需数秒）..."), false);
        QString ocrError;
        if (!m_ocr.start(&ocrError))
        {
            emit logMessage(QStringLiteral("OCR 启动失败：%1").arg(ocrError), true);
            return;
        }
        emit logMessage(QStringLiteral("OCR 就绪，将按文字识别「%1」").arg(m_config.contOcrText), false);
    }

    m_running = true;
    m_scanCount = 0;
    m_endTurnCount = 0;
    m_stage = Stage::Idle;

    emit logMessage(QStringLiteral("=== 已启动 ==="), false);
    emit logMessage(QStringLiteral("任意时刻：识别「开始」→开始新一局；识别「查看战场」/「失败」/「胜利」或 OCR「继续」→判定结算页，持续点击屏幕中央直到出现「开始」"), false);
    emit logMessage(QStringLiteral("优先级3=确定，优先级4=结束回合，优先级5=主逻辑（x≥%1 则 设置→投降）")
                        .arg(kSurrenderThreshold),
                    false);
    setStage(Stage::LogicB_WaitStart);

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
    m_ocr.stop();
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
    m_currentScreen = screen;
    m_currentGray = Matcher::toGray(screen);
    m_currentEdge = Matcher::toEdge(m_currentGray);
    m_clickedThisTick = false;
    m_drawRois.clear();
    m_drawHits.clear();

    if (m_stage == Stage::Idle)
    {
        setStage(Stage::LogicB_WaitStart);
    }

    if (!handlePriority())
    {
        handleMainLogic();
    }

    m_scanCount++;
    m_lastCostMs = static_cast<double>(cost.elapsed());
    emit statsChanged(m_scanCount, m_lastCostMs);

    buildPreview(screen);

    const int delay = m_clickedThisTick ? m_config.clickCooldownMs : m_config.scanIntervalMs;
    m_timer->start(qMax(30, delay));
}

bool BotEngine::handlePriority()
{
    Matcher::Result result;

    // 优先级 1：任意时刻识别到「开始」——视为回到主菜单、开新一局，优先执行并结束本 tick。
    if (matchKey(QStringLiteral("start"), &result))
    {
        performClickTwice(result.rect.center());
        m_clickedThisTick = true;
        m_endTurnCount = 0;
        emit logMessage(QStringLiteral("优先级1：识别到「开始」，已点击（视为新一局开始）-> 执行逻辑a"), false);
        setStage(Stage::LogicA_InGame);
        return true;
    }

    // 优先级 2：结算页（对局结束）判定——「查看战场」/「失败」/「胜利」/ OCR「继续」任一命中即算。
    // 判定成立时，每轮都点击屏幕中央，持续到识别到「开始」为止（「开始」由优先级 1 接管）。
    bool onResultPage = matchKey(QStringLiteral("battle"), &result);
    if (!onResultPage)
    {
        onResultPage = matchKey(QStringLiteral("defeat"), &result);
    }
    if (!onResultPage)
    {
        onResultPage = matchKey(QStringLiteral("victory"), &result);
    }
    if (!onResultPage && m_config.contOcrEnabled)
    {
        QPoint contPos;
        onResultPage = detectOcrText(m_config.contOcrText, m_config.contOcrRoi, &contPos);
    }
    if (onResultPage)
    {
        m_clickedThisTick = true;
        m_endTurnCount = 0;
        emit logMessage(QStringLiteral("优先级2：已进入结算页，点击屏幕中央（将持续到出现「开始」）"), false);
        clickScreenCenter(1);
        setStage(Stage::LogicB_WaitStart);
        return true;
    }

    // 优先级 3：任意时刻识别到“确定”
    if (matchKey(QStringLiteral("confirm"), &result))
    {
        performClickTwice(result.rect.center());
        m_clickedThisTick = true;
        emit logMessage(QStringLiteral("优先级3：识别到「确定」，已点击"), false);
        return true;
    }

    // 优先级 4：对局中任意时刻识别到“结束回合”
    if (m_stage == Stage::LogicA_InGame && matchKey(QStringLiteral("endTurn"), &result))
    {
        performClickTwice(result.rect.center());
        m_clickedThisTick = true;
        m_endTurnCount++;
        emit logMessage(QStringLiteral("优先级3：识别到「结束回合」，已点击，记录次数 x=%1").arg(m_endTurnCount), false);

        if (m_endTurnCount >= kSurrenderThreshold)
        {
            emit logMessage(QStringLiteral("x ≥ %1，转入主逻辑：打开设置并投降").arg(kSurrenderThreshold), false);
            setStage(Stage::OpenMenu);
        }
        return true;
    }

    return false;
}

void BotEngine::handleMainLogic()
{
    Matcher::Result result;

    switch (m_stage)
    {
    case Stage::LogicB_WaitStart:
    case Stage::LogicA_InGame:
        // 逻辑a / 逻辑b：保持执行。等待的动作由优先级 1~3 每 tick 检查，这里不做额外点击。
        break;

    case Stage::OpenMenu:
        if (matchKey(QStringLiteral("menu"), &result))
        {
            performClickTwice(result.rect.center());
            m_clickedThisTick = true;
            emit logMessage(QStringLiteral("主逻辑：识别到「设置齿轮」，已点击"), false);
            setStage(Stage::Surrender);
        }
        break;

    case Stage::Surrender:
        if (matchKey(QStringLiteral("surrender"), &result))
        {
            performClickTwice(result.rect.center());
            m_clickedThisTick = true;
            m_endTurnCount = 0;
            emit logMessage(QStringLiteral("主逻辑：识别到「投降」，已点击，次数 x 清零 -> 执行逻辑b"), false);
            setStage(Stage::LogicB_WaitStart);
        }
        break;

    default:
        break;
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
        painter.drawRect(QRect(static_cast<int>(imageRect.x() * ratio),
                               static_cast<int>(imageRect.y() * ratio),
                               static_cast<int>(imageRect.width() * ratio),
                               static_cast<int>(imageRect.height() * ratio)));
    }

    painter.setPen(QPen(QColor(0, 255, 0), 2));
    for (const QRect& rect : m_drawHits)
    {
        const QRect imageRect = rect.translated(-m_currentOrigin);
        painter.drawRect(QRect(static_cast<int>(imageRect.x() * ratio),
                               static_cast<int>(imageRect.y() * ratio),
                               static_cast<int>(imageRect.width() * ratio),
                               static_cast<int>(imageRect.height() * ratio)));
    }

    painter.setPen(QPen(QColor(255, 210, 0), 2));
    painter.drawText(8, 20, stageName(m_stage));

    painter.end();

    emit frameReady(frame);
}
