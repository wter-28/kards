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
//   1. 任意时刻识别到“开始”        -> 连点 3 次，视为新一局开始
//   2. 结算页判定：“查看战场”/“失败”/“胜利”/ OCR“继续” 任一命中
//      -> 每轮点击屏幕中央，持续到识别到“开始”为止（对局已结束）
//   3. 任意时刻识别到“确定”        -> 连点 3 次
//   4. 任意时刻识别到“结束回合”    -> 连点 3 次
// 除结算页的中央点击外，其余点击一律连点 3 次，点完把光标移回屏幕中央（避免误触/光标遮挡）。
class BotEngine : public QObject
{
    Q_OBJECT

public:
    enum class Stage
    {
        Idle,
        LogicB_WaitStart,   // 逻辑b：保持执行，等待“开始”
        LogicA_InGame       // 逻辑a：保持执行，对局中
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
    void performClickTriple(const QPoint& screenPos);   // 连点 3 次并把光标移回屏幕中央
    void clickScreenCenter(int times);
    void buildPreview(const QImage& screen);

    // 优先级 1~4：命中任意一条就返回 true
    bool handlePriority();

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

    int    m_scanCount = 0;
    double m_lastCostMs = 0.0;

    QVector<QRect> m_drawRois;
    QVector<QRect> m_drawHits;
};
