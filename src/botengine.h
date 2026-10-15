#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QVector>

#include "appconfig.h"
#include "matcher.h"
#include "screencapture.h"

// 自动挂机的状态机引擎（运行在工作线程中）
class BotEngine : public QObject
{
    Q_OBJECT

public:
    enum class Stage
    {
        Idle,               // 未运行
        WaitingImage1,      // 等待图片1（开始），识别到就点击
        WaitingImage2,      // 等待图片2（选择要替换的卡牌）
        WaitingImage3,      // 等待图片3（确认），识别到就点击
        Playing,            // 对局中：按回合依次点击图片5（结束回合）
        OpenSettings,       // 等待图片10（设置齿轮），识别到就点击
        Surrender,          // 等待图片11（投降），识别到就点击
        Returning           // 一直点击图片1的位置，直到再次识别到图片1
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
        QImage gray;        // 灰度模板
        QRect  roi;         // 屏幕坐标下的搜索区域，空表示全屏
        bool   valid = false;
        bool   enabled = true;
    };

    void loadTemplates();
    bool matchKey(const QString& key, Matcher::Result* out);
    void setStage(Stage stage);
    QString stageName(Stage stage) const;
    void performClick(const QPoint& screenPos);
    void buildPreview(const QImage& screen);
    QString turnKey(int turn) const;

    void handleWaitingImage1();
    void handleWaitingImage2();
    void handleWaitingImage3();
    void handlePlaying();
    void handleOpenSettings();
    void handleSurrender();
    void handleReturning();

    AppConfig     m_config;
    ScreenCapture m_capture;
    QTimer*       m_timer = nullptr;

    bool  m_running = false;
    Stage m_stage = Stage::Idle;

    QMap<QString, Entry> m_entries;

    QImage m_currentGray;
    QPoint m_currentOrigin;
    bool   m_clickedThisTick = false;

    int            m_expectedTurn = 1;
    QElapsedTimer  m_turnTimer;
    QPoint         m_lastImage1Pos;
    bool           m_hasImage1Pos = false;

    int    m_scanCount = 0;
    double m_lastCostMs = 0.0;

    QVector<QRect> m_drawRois;
    QVector<QRect> m_drawHits;
};
