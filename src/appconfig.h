#pragma once

#include <QList>
#include <QRect>
#include <QString>

struct TemplateConfig
{
    QString key;                 // 内部标识，如 start / confirm / endTurn
    QString displayName;         // 界面显示名称
    QString fileName;            // 资源文件名，界面可编辑
    QRect   roi;                 // 搜索区域（屏幕坐标），为空表示全屏搜索
    bool    enabled = true;      // 是否参与识别
};

struct AppConfig
{
    QString assetsPath;                          // 模板图片目录
    int     scanIntervalMs   = 400;              // 每轮识别间隔（毫秒）
    int     clickCooldownMs  = 350;              // 点击后的冷却时间（毫秒）
    double  threshold        = 0.75;             // 匹配阈值（ZNCC）
    int     coarseFactor     = 0;                // 0 表示自动；否则为金字塔起始缩放（1/2/4/8）
    int     clickMode        = 0;                // 0 = 前台真实点击，1 = 后台消息点击
    QString targetWindowTitle;                   // 后台点击的目标窗口标题
    qint64  targetWindowHandle = 0;              // 后台点击的目标窗口句柄
    bool    contOcrEnabled   = true;             // 「继续」是否使用 OCR 文字识别
    QString contOcrText      = QStringLiteral("继续");   // OCR 查找的文字
    QRect   contOcrRoi;                          // OCR 搜索区域（屏幕坐标），空表示全屏
    QList<TemplateConfig> templates;
};

QList<TemplateConfig> defaultTemplates();
AppConfig defaultConfig();
bool saveConfig(const AppConfig& config, const QString& path);
bool loadConfig(AppConfig& config, const QString& path);
