#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTime>
#include <QVBoxLayout>

#include "botengine.h"
#include "clicker.h"
#include "roipicker.h"
#include "screencapture.h"

namespace
{

constexpr int kColumnName   = 0;
constexpr int kColumnFile   = 1;
constexpr int kColumnRoi    = 2;
constexpr int kColumnEnable = 3;

}   // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_config = defaultConfig();
    loadConfig(m_config, configPath());

    buildUi();
    applyConfigToUi();
    refreshTemplateTable();
    onRefreshWindows();

    m_engine = new BotEngine();
    m_thread = new QThread(this);
    m_engine->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_engine, &QObject::deleteLater);
    connect(m_engine, &BotEngine::logMessage, this, &MainWindow::onLogMessage, Qt::QueuedConnection);
    connect(m_engine, &BotEngine::stageChanged, this, &MainWindow::onStageChanged, Qt::QueuedConnection);
    connect(m_engine, &BotEngine::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_engine, &BotEngine::statsChanged, this, &MainWindow::onStatsChanged, Qt::QueuedConnection);

    m_thread->start();

    onLogMessage(QStringLiteral("配置文件：%1").arg(configPath()), false);
    onLogMessage(QStringLiteral("使用前建议先为每个模板框选“搜索区域”，可大幅提升识别速度与准确率"), false);
}

MainWindow::~MainWindow()
{
    if (m_engine != nullptr)
    {
        QMetaObject::invokeMethod(m_engine, [engine = m_engine]()
        {
            engine->stop();
        }, Qt::BlockingQueuedConnection);
    }

    if (m_thread != nullptr)
    {
        m_thread->quit();
        m_thread->wait(3000);
    }
}

QString MainWindow::configPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("kards_auto.json"));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("KARDS 自动挂机"));
    resize(1360, 820);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* topBar = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("开始"), central);
    m_stopButton = new QPushButton(QStringLiteral("停止"), central);
    m_startButton->setMinimumWidth(120);
    m_stopButton->setMinimumWidth(120);
    m_stopButton->setEnabled(false);

    m_stageLabel = new QLabel(QStringLiteral("状态：未运行"), central);
    m_statsLabel = new QLabel(QStringLiteral("识别次数：0   单次耗时：0 ms"), central);

    topBar->addWidget(m_startButton);
    topBar->addWidget(m_stopButton);
    topBar->addSpacing(20);
    topBar->addWidget(m_stageLabel);
    topBar->addStretch(1);
    topBar->addWidget(m_statsLabel);
    rootLayout->addLayout(topBar);

    auto* splitter = new QSplitter(Qt::Horizontal, central);

    auto* leftScroll = new QScrollArea(splitter);
    leftScroll->setWidgetResizable(true);
    auto* leftPanel = new QWidget(leftScroll);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->addWidget(buildSettingsPanel());
    leftLayout->addWidget(buildTemplatePanel());
    leftLayout->addStretch(1);
    leftScroll->setWidget(leftPanel);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);

    auto* previewGroup = new QGroupBox(QStringLiteral("实时画面（绿色=识别命中，蓝色=搜索区域）"), rightPanel);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_previewLabel = new QLabel(previewGroup);
    m_previewLabel->setMinimumSize(640, 360);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(QStringLiteral("background-color:#1e1e1e;color:#888;"));
    m_previewLabel->setText(QStringLiteral("尚未开始"));
    previewLayout->addWidget(m_previewLabel);
    rightLayout->addWidget(previewGroup, 3);

    auto* logGroup = new QGroupBox(QStringLiteral("运行日志"), rightPanel);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logView = new QPlainTextEdit(logGroup);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    logLayout->addWidget(m_logView);
    rightLayout->addWidget(logGroup, 2);

    splitter->addWidget(leftScroll);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 520 << 840);

    rootLayout->addWidget(splitter, 1);

    auto* bottomBar = new QHBoxLayout();
    auto* saveButton = new QPushButton(QStringLiteral("保存配置"), central);
    auto* reloadButton = new QPushButton(QStringLiteral("重新加载配置"), central);
    bottomBar->addWidget(saveButton);
    bottomBar->addWidget(reloadButton);
    bottomBar->addStretch(1);
    rootLayout->addLayout(bottomBar);

    setCentralWidget(central);

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::onSaveConfig);
    connect(reloadButton, &QPushButton::clicked, this, &MainWindow::onReloadConfig);
}

QWidget* MainWindow::buildSettingsPanel()
{
    auto* group = new QGroupBox(QStringLiteral("运行参数"));
    auto* form = new QFormLayout(group);

    auto* assetsRow = new QHBoxLayout();
    m_assetsEdit = new QLineEdit(group);
    auto* browseButton = new QPushButton(QStringLiteral("浏览..."), group);
    assetsRow->addWidget(m_assetsEdit, 1);
    assetsRow->addWidget(browseButton);
    form->addRow(QStringLiteral("模板图片目录"), assetsRow);

    m_scanSpin = new QSpinBox(group);
    m_scanSpin->setRange(50, 5000);
    m_scanSpin->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("识别间隔"), m_scanSpin);

    m_cooldownSpin = new QSpinBox(group);
    m_cooldownSpin->setRange(30, 5000);
    m_cooldownSpin->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("点击后冷却"), m_cooldownSpin);

    m_thresholdSpin = new QDoubleSpinBox(group);
    m_thresholdSpin->setRange(0.30, 1.00);
    m_thresholdSpin->setSingleStep(0.01);
    m_thresholdSpin->setDecimals(2);
    form->addRow(QStringLiteral("匹配阈值"), m_thresholdSpin);

    m_coarseCombo = new QComboBox(group);
    m_coarseCombo->addItem(QStringLiteral("自动"), 0);
    m_coarseCombo->addItem(QStringLiteral("1 (最精确最慢)"), 1);
    m_coarseCombo->addItem(QStringLiteral("2"), 2);
    m_coarseCombo->addItem(QStringLiteral("4"), 4);
    m_coarseCombo->addItem(QStringLiteral("8 (最快)"), 8);
    form->addRow(QStringLiteral("金字塔起始缩放"), m_coarseCombo);

    m_clickModeCombo = new QComboBox(group);
    m_clickModeCombo->addItem(QStringLiteral("前台真实点击 (SendInput)"), Clicker::FrontClick);
    m_clickModeCombo->addItem(QStringLiteral("后台消息点击 (PostMessage)"), Clicker::BackgroundClick);
    form->addRow(QStringLiteral("点击方式"), m_clickModeCombo);

    auto* windowRow = new QHBoxLayout();
    m_windowCombo = new QComboBox(group);
    m_windowCombo->setMinimumWidth(300);
    auto* refreshButton = new QPushButton(QStringLiteral("刷新"), group);
    windowRow->addWidget(m_windowCombo, 1);
    windowRow->addWidget(refreshButton);
    form->addRow(QStringLiteral("后台点击目标窗口"), windowRow);

    m_timeoutSpin = new QSpinBox(group);
    m_timeoutSpin->setRange(5, 600);
    m_timeoutSpin->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("等待回合超时"), m_timeoutSpin);

    m_retryCheck = new QCheckBox(QStringLiteral("超时后重试点击“结束回合”"), group);
    form->addRow(QStringLiteral(""), m_retryCheck);

    auto* fallbackRow = new QHBoxLayout();
    m_useFallbackCheck = new QCheckBox(QStringLiteral("使用"), group);
    m_fallbackXSpin = new QSpinBox(group);
    m_fallbackXSpin->setRange(-20000, 20000);
    m_fallbackYSpin = new QSpinBox(group);
    m_fallbackYSpin->setRange(-20000, 20000);
    fallbackRow->addWidget(m_useFallbackCheck);
    fallbackRow->addWidget(new QLabel(QStringLiteral("X:"), group));
    fallbackRow->addWidget(m_fallbackXSpin);
    fallbackRow->addWidget(new QLabel(QStringLiteral("Y:"), group));
    fallbackRow->addWidget(m_fallbackYSpin);
    form->addRow(QStringLiteral("图片1位置未知时备用坐标"), fallbackRow);

    connect(browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseAssets);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshWindows);

    return group;
}

QWidget* MainWindow::buildTemplatePanel()
{
    auto* group = new QGroupBox(QStringLiteral("模板与搜索区域"));
    auto* layout = new QVBoxLayout(group);

    m_table = new QTableWidget(0, 4, group);
    m_table->setHorizontalHeaderLabels(QStringList()
                                       << QStringLiteral("模板")
                                       << QStringLiteral("文件")
                                       << QStringLiteral("搜索区域（屏幕坐标）")
                                       << QStringLiteral("启用"));
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kColumnName, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColumnFile, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColumnRoi, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColumnEnable, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table);

    auto* buttonRow = new QHBoxLayout();
    auto* pickButton = new QPushButton(QStringLiteral("框选所选区域"), group);
    auto* clearButton = new QPushButton(QStringLiteral("清除所选区域"), group);
    auto* fullButton = new QPushButton(QStringLiteral("全部改为全屏搜索"), group);
    buttonRow->addWidget(pickButton);
    buttonRow->addWidget(clearButton);
    buttonRow->addWidget(fullButton);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    connect(pickButton, &QPushButton::clicked, this, &MainWindow::onPickRoi);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearRoi);
    connect(fullButton, &QPushButton::clicked, this, [this]()
    {
        for (TemplateConfig& item : m_config.templates)
        {
            item.roi = QRect();
        }
        refreshTemplateTable();
    });

    return group;
}

void MainWindow::applyConfigToUi()
{
    m_assetsEdit->setText(m_config.assetsPath);
    m_scanSpin->setValue(m_config.scanIntervalMs);
    m_cooldownSpin->setValue(m_config.clickCooldownMs);
    m_thresholdSpin->setValue(m_config.threshold);
    m_timeoutSpin->setValue(m_config.turnWaitTimeoutSec);
    m_retryCheck->setChecked(m_config.retryClickOnTimeout);

    const int coarseIndex = m_coarseCombo->findData(m_config.coarseFactor);
    m_coarseCombo->setCurrentIndex(coarseIndex >= 0 ? coarseIndex : 0);

    const int modeIndex = m_clickModeCombo->findData(m_config.clickMode);
    m_clickModeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);

    m_useFallbackCheck->setChecked(m_config.hasFallbackClickPoint);
    m_fallbackXSpin->setValue(m_config.fallbackClickPoint.x());
    m_fallbackYSpin->setValue(m_config.fallbackClickPoint.y());
}

void MainWindow::collectUiToConfig()
{
    m_config.assetsPath = m_assetsEdit->text().trimmed();
    m_config.scanIntervalMs = m_scanSpin->value();
    m_config.clickCooldownMs = m_cooldownSpin->value();
    m_config.threshold = m_thresholdSpin->value();
    m_config.coarseFactor = m_coarseCombo->currentData().toInt();
    m_config.clickMode = m_clickModeCombo->currentData().toInt();
    m_config.turnWaitTimeoutSec = m_timeoutSpin->value();
    m_config.retryClickOnTimeout = m_retryCheck->isChecked();

    m_config.hasFallbackClickPoint = m_useFallbackCheck->isChecked();
    m_config.fallbackClickPoint = QPoint(m_fallbackXSpin->value(), m_fallbackYSpin->value());

    if (m_windowCombo->currentIndex() >= 0)
    {
        const qint64 handle = m_windowCombo->currentData().toLongLong();
        if (handle != 0)
        {
            m_config.targetWindowHandle = handle;
            m_config.targetWindowTitle = m_windowCombo->currentText();
        }
    }

    for (int row = 0; row < m_table->rowCount() && row < m_config.templates.size(); ++row)
    {
        QTableWidgetItem* item = m_table->item(row, kColumnEnable);
        if (item != nullptr)
        {
            m_config.templates[row].enabled = (item->checkState() == Qt::Checked);
        }
    }
}

void MainWindow::refreshTemplateTable()
{
    m_table->setRowCount(m_config.templates.size());

    for (int row = 0; row < m_config.templates.size(); ++row)
    {
        const TemplateConfig& item = m_config.templates[row];

        auto* nameItem = new QTableWidgetItem(item.displayName);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, kColumnName, nameItem);

        auto* fileItem = new QTableWidgetItem(item.fileName);
        fileItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, kColumnFile, fileItem);

        const QString roiText = item.roi.isEmpty()
                                    ? QStringLiteral("全屏搜索")
                                    : QStringLiteral("%1, %2, %3, %4").arg(item.roi.x()).arg(item.roi.y()).arg(item.roi.width()).arg(item.roi.height());
        auto* roiItem = new QTableWidgetItem(roiText);
        roiItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        roiItem->setData(Qt::UserRole, QVariant::fromValue(item.roi));
        m_table->setItem(row, kColumnRoi, roiItem);

        auto* enableItem = new QTableWidgetItem();
        enableItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        enableItem->setCheckState(item.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, kColumnEnable, enableItem);
    }
}

void MainWindow::onBrowseAssets()
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择模板图片目录"), m_config.assetsPath);
    if (!directory.isEmpty())
    {
        m_assetsEdit->setText(directory);
    }
}

void MainWindow::onPickRoi()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_config.templates.size())
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先在下面的表格中选择一个模板"));
        return;
    }

    ScreenCapture capture;
    const QImage screen = capture.grabDesktop();
    if (screen.isNull())
    {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("屏幕截图失败"));
        return;
    }

    RoiPickerDialog dialog(m_config.templates[row].displayName,
                           capture.lastCaptureRect(),
                           m_config.templates[row].roi,
                           this);

    if (dialog.exec() == QDialog::Accepted)
    {
        m_config.templates[row].roi = dialog.selection();
        refreshTemplateTable();
        m_table->selectRow(row);
    }
}

void MainWindow::onClearRoi()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_config.templates.size())
    {
        return;
    }

    m_config.templates[row].roi = QRect();
    refreshTemplateTable();
    m_table->selectRow(row);
}

void MainWindow::onSaveConfig()
{
    collectUiToConfig();
    if (saveConfig(m_config, configPath()))
    {
        onLogMessage(QStringLiteral("配置已保存到 %1").arg(configPath()), false);
    }
    else
    {
        onLogMessage(QStringLiteral("配置保存失败"), true);
    }
}

void MainWindow::onReloadConfig()
{
    AppConfig config = defaultConfig();
    if (loadConfig(config, configPath()))
    {
        m_config = config;
        applyConfigToUi();
        refreshTemplateTable();
        onLogMessage(QStringLiteral("配置已重新加载"), false);
    }
    else
    {
        onLogMessage(QStringLiteral("没有可加载的配置文件"), true);
    }
}

void MainWindow::onRefreshWindows()
{
    const qint64 previousHandle = m_config.targetWindowHandle;
    QString previousTitle = m_config.targetWindowTitle;

    m_windowCombo->clear();
    m_windowCombo->addItem(QStringLiteral("（未选择）"), static_cast<qint64>(0));

    const auto windows = Clicker::enumerateWindows();
    for (const auto& window : windows)
    {
        m_windowCombo->addItem(window.second, window.first);
    }

    int targetIndex = 0;
    for (int i = 0; i < m_windowCombo->count(); ++i)
    {
        const qint64 handle = m_windowCombo->itemData(i).toLongLong();
        if (handle != 0 && handle == previousHandle)
        {
            targetIndex = i;
            break;
        }
        if (handle != 0 && !previousTitle.isEmpty() && m_windowCombo->itemText(i) == previousTitle)
        {
            targetIndex = i;
        }
    }
    m_windowCombo->setCurrentIndex(targetIndex);
}

bool MainWindow::validateTemplates(QString* errorMessage) const
{
    QStringList missing;
    for (const TemplateConfig& item : m_config.templates)
    {
        if (!item.enabled)
        {
            continue;
        }
        const QString path = QDir(m_config.assetsPath).filePath(item.fileName);
        if (!QFile::exists(path))
        {
            missing.append(QStringLiteral("%1 (%2)").arg(item.displayName, path));
        }
    }

    if (!missing.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("以下模板文件不存在：\n%1").arg(missing.join(QStringLiteral("\n")));
        }
        return false;
    }

    return true;
}

void MainWindow::onStart()
{
    collectUiToConfig();

    QString error;
    if (!validateTemplates(&error))
    {
        QMessageBox::warning(this, QStringLiteral("无法启动"), error);
        return;
    }

    if (m_config.clickMode == Clicker::BackgroundClick && m_config.targetWindowHandle == 0)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            QStringLiteral("提示"),
            QStringLiteral("当前选择“后台消息点击”，但没有指定目标窗口。\n是否改用前台真实点击？"),
            QMessageBox::Yes | QMessageBox::No);
        if (answer == QMessageBox::Yes)
        {
            m_config.clickMode = Clicker::FrontClick;
            m_clickModeCombo->setCurrentIndex(m_clickModeCombo->findData(Clicker::FrontClick));
        }
    }

    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);

    const AppConfig config = m_config;
    QMetaObject::invokeMethod(m_engine, [engine = m_engine, config]()
    {
        engine->setConfig(config);
        engine->start();
    }, Qt::QueuedConnection);
}

void MainWindow::onStop()
{
    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);

    QMetaObject::invokeMethod(m_engine, [engine = m_engine]()
    {
        engine->stop();
    }, Qt::QueuedConnection);
}

void MainWindow::onLogMessage(const QString& message, bool isError)
{
    const QString prefix = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1]%2 %3").arg(prefix, isError ? QStringLiteral(" [错误]") : QString(), message);
    m_logView->appendPlainText(line);
}

void MainWindow::onStageChanged(const QString& stageName)
{
    m_stageLabel->setText(QStringLiteral("状态：%1").arg(stageName));
}

void MainWindow::onFrameReady(const QImage& frame)
{
    if (frame.isNull())
    {
        return;
    }

    const QPixmap pixmap = QPixmap::fromImage(frame).scaled(m_previewLabel->size(),
                                                            Qt::KeepAspectRatio,
                                                            Qt::SmoothTransformation);
    m_previewLabel->setPixmap(pixmap);
}

void MainWindow::onStatsChanged(int scanCount, double lastCostMs)
{
    m_statsLabel->setText(QStringLiteral("识别次数：%1   单次耗时：%2 ms").arg(scanCount).arg(lastCostMs, 0, 'f', 1));
}
