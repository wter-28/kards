#pragma once

#include <QImage>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QVector>

#include "appconfig.h"
#include "matcher.h"
#include "ocr.h"
#include "screencapture.h"

// 自动挂机引擎（运行在工作线程中）
//
// 设计依据（优先级从高到低）：
//   1. 任意时刻识别到“开始”        -> 立即点击，视为新一局开始，执行“逻辑a”
//   2. 结算页判定：“查看战场”/“失败”/“胜利”/ OCR“继续” 任一命中
//      -> 每轮点击屏幕中央，持续到识别到“开始”为止（对局已结束）
//   3. 任意时刻识别到“确定”        -> 立即点击
//   4. 对局中识别到“结束回合”      -> 立即点击，记录次数 x
//   5. 以上都没命中                -> 执行主逻辑（x >= 5 时：设置齿轮 -> 投降）
class BotEngine : public QObject
{
    Q_OBJECT

public:
    enum class Stage
    {
        Idle,
        LogicB_WaitStart,   // 逻辑b：保持执行，等待“开始”
        LogicA_InGame,      // 逻辑a：保持执行，对局中（点确定 / 结束回合，计数 x）
        OpenMenu,           // 主逻辑：点击“设置齿轮”
        Surrender           // 主逻辑：点击“投降”
    };
    Q_ENUM(Stage)

    explicit BotEngine(QObject* parent = nullptr);
    ~BotEngine() override;

    void setConfig(const AppConfig& config);
    AppConfig config() const;

public slots:
    void start();
    void stop();

signals:
    void logMessage(const QString& message, bool isError);
    void stageChanged(const QString& stageName);
    void frameReady(const QImage& frame);
    void statsChanged(int scanCount, double lastCostMs);

private slots:
    void onTick();

private:
    struct Entry
    {
        QImage gray;        // 边缘特征模板（由资源图经 Matcher::toEdge 得到）
        QRect  roi;         // 屏幕坐标下的搜索区域，空表示全屏
        bool   valid = false;
        bool   enabled = true;
    };

    void loadTemplates();
    bool matchKey(const QString& key, Matcher::Result* out);
    bool detectOcrText(const QString& target, const QRect& screenRoi, QPoint* screenPos);
    void setStage(Stage stage);
    QString stageName(Stage stage) const;
    void performClick(const QPoint& screenPos);
    void clickScreenCenter(int times);
    void buildPreview(const QImage& screen);

    // 优先级 1~3：命中任意一条就返回 true（本 tick 不再执行主逻辑）
    bool handlePriority();
    // 优先级 4：主逻辑
    void handleMainLogic();

    AppConfig     m_config;
    ScreenCapture m_capture;
    OcrService    m_ocr;
    QTimer*       m_timer = nullptr;

    bool  m_running = false;
    Stage m_stage = Stage::Idle;

    QMap<QString, Entry> m_entries;

    QImage m_currentScreen;
    QImage m_currentGray;
    QImage m_currentEdge;
    QPoint m_currentOrigin;
    bool   m_clickedThisTick = false;

    int m_endTurnCount = 0;      // 主逻辑里的 x：累计点击“结束回合”的次数

    int    m_scanCount = 0;
    double m_lastCostMs = 0.0;

    QVector<QRect> m_drawRois;
    QVector<QRect> m_drawHits;
};
