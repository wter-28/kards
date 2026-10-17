#pragma once

#include <QMainWindow>
#include <QPoint>

#include "appconfig.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;

class BotEngine;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onBrowseAssets();
    void onStart();
    void onStop();
    void onPickRoi();
    void onClearRoi();
    void onPickOcrRoi();
    void onSaveConfig();
    void onReloadConfig();
    void onRefreshWindows();
    void onLogMessage(const QString& message, bool isError);
    void onStageChanged(const QString& stageName);
    void onFrameReady(const QImage& frame);
    void onStatsChanged(int scanCount, double lastCostMs);

private:
    void buildUi();
    QWidget* buildSettingsPanel();
    QWidget* buildTemplatePanel();
    void applyConfigToUi();
    void collectUiToConfig();
    void refreshTemplateTable();
    void updateOcrRoiLabel();
    QString configPath() const;
    bool validateTemplates(QString* errorMessage) const;

    AppConfig m_config;

    BotEngine* m_engine = nullptr;
    QThread*   m_thread = nullptr;

    QLineEdit*      m_assetsEdit = nullptr;
    QSpinBox*       m_scanSpin = nullptr;
    QSpinBox*       m_cooldownSpin = nullptr;
    QDoubleSpinBox* m_thresholdSpin = nullptr;
    QComboBox*      m_coarseCombo = nullptr;
    QComboBox*      m_clickModeCombo = nullptr;
    QComboBox*      m_windowCombo = nullptr;
    QCheckBox*      m_contOcrCheck = nullptr;
    QLineEdit*      m_contTextEdit = nullptr;
    QLabel*         m_ocrRoiLabel = nullptr;

    QTableWidget*   m_table = nullptr;
    QLabel*         m_previewLabel = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QLabel*         m_stageLabel = nullptr;
    QLabel*         m_statsLabel = nullptr;
    QPushButton*    m_startButton = nullptr;
    QPushButton*    m_stopButton = nullptr;
};
