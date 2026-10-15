#include "appconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QList<TemplateConfig> defaultTemplates()
{
    QList<TemplateConfig> list;

    auto add = [&list](const QString& key, const QString& name, const QString& file)
    {
        TemplateConfig item;
        item.key = key;
        item.displayName = name;
        item.fileName = file;
        item.roi = QRect();
        item.enabled = true;
        list.append(item);
    };

    add(QStringLiteral("img1"),  QStringLiteral("图片1  开始"),            QStringLiteral("1.png"));
    add(QStringLiteral("img2"),  QStringLiteral("图片2  选择要替换的卡牌"), QStringLiteral("2.png"));
    add(QStringLiteral("img3"),  QStringLiteral("图片3  确认"),            QStringLiteral("3.png"));
    add(QStringLiteral("turn1"), QStringLiteral("回合1  (1 K/1)"),         QStringLiteral("4.png"));
    add(QStringLiteral("img5"),  QStringLiteral("图片5  结束回合"),        QStringLiteral("5.png"));
    add(QStringLiteral("turn2"), QStringLiteral("回合2  (2 K/2)"),         QStringLiteral("6.png"));
    add(QStringLiteral("turn3"), QStringLiteral("回合3  (3 K/3)"),         QStringLiteral("7.png"));
    add(QStringLiteral("turn4"), QStringLiteral("回合4  (4 K/4)"),         QStringLiteral("8.png"));
    add(QStringLiteral("turn5"), QStringLiteral("回合5  (5 K/5)"),         QStringLiteral("9.png"));
    add(QStringLiteral("img10"), QStringLiteral("图片10  设置齿轮"),       QStringLiteral("10.png"));
    add(QStringLiteral("img11"), QStringLiteral("图片11  投降"),           QStringLiteral("11.png"));

    return list;
}

AppConfig defaultConfig()
{
    AppConfig config;
    config.assetsPath = QStringLiteral("D:\\MyDesktop\\kards auto bat\\assets");
    config.templates = defaultTemplates();
    return config;
}

static QJsonObject rectToJson(const QRect& rect)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), rect.x());
    obj.insert(QStringLiteral("y"), rect.y());
    obj.insert(QStringLiteral("w"), rect.width());
    obj.insert(QStringLiteral("h"), rect.height());
    return obj;
}

static QRect rectFromJson(const QJsonObject& obj)
{
    return QRect(obj.value(QStringLiteral("x")).toInt(),
                 obj.value(QStringLiteral("y")).toInt(),
                 obj.value(QStringLiteral("w")).toInt(),
                 obj.value(QStringLiteral("h")).toInt());
}

bool saveConfig(const AppConfig& config, const QString& path)
{
    QJsonObject root;
    root.insert(QStringLiteral("assetsPath"), config.assetsPath);
    root.insert(QStringLiteral("scanIntervalMs"), config.scanIntervalMs);
    root.insert(QStringLiteral("clickCooldownMs"), config.clickCooldownMs);
    root.insert(QStringLiteral("threshold"), config.threshold);
    root.insert(QStringLiteral("coarseFactor"), config.coarseFactor);
    root.insert(QStringLiteral("clickMode"), config.clickMode);
    root.insert(QStringLiteral("targetWindowTitle"), config.targetWindowTitle);
    root.insert(QStringLiteral("targetWindowHandle"), static_cast<double>(config.targetWindowHandle));
    root.insert(QStringLiteral("turnWaitTimeoutSec"), config.turnWaitTimeoutSec);
    root.insert(QStringLiteral("retryClickOnTimeout"), config.retryClickOnTimeout);
    root.insert(QStringLiteral("hasFallbackClickPoint"), config.hasFallbackClickPoint);

    QJsonObject point;
    point.insert(QStringLiteral("x"), config.fallbackClickPoint.x());
    point.insert(QStringLiteral("y"), config.fallbackClickPoint.y());
    root.insert(QStringLiteral("fallbackClickPoint"), point);

    QJsonArray array;
    for (const TemplateConfig& item : config.templates)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("key"), item.key);
        obj.insert(QStringLiteral("displayName"), item.displayName);
        obj.insert(QStringLiteral("fileName"), item.fileName);
        obj.insert(QStringLiteral("enabled"), item.enabled);
        obj.insert(QStringLiteral("roi"), rectToJson(item.roi));
        array.append(obj);
    }
    root.insert(QStringLiteral("templates"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool loadConfig(AppConfig& config, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    const QJsonObject root = document.object();
    AppConfig loaded = defaultConfig();

    loaded.assetsPath = root.value(QStringLiteral("assetsPath")).toString(loaded.assetsPath);
    loaded.scanIntervalMs = root.value(QStringLiteral("scanIntervalMs")).toInt(loaded.scanIntervalMs);
    loaded.clickCooldownMs = root.value(QStringLiteral("clickCooldownMs")).toInt(loaded.clickCooldownMs);
    loaded.threshold = root.value(QStringLiteral("threshold")).toDouble(loaded.threshold);
    loaded.coarseFactor = root.value(QStringLiteral("coarseFactor")).toInt(loaded.coarseFactor);
    loaded.clickMode = root.value(QStringLiteral("clickMode")).toInt(loaded.clickMode);
    loaded.targetWindowTitle = root.value(QStringLiteral("targetWindowTitle")).toString();
    loaded.targetWindowHandle = static_cast<qint64>(root.value(QStringLiteral("targetWindowHandle")).toDouble());
    loaded.turnWaitTimeoutSec = root.value(QStringLiteral("turnWaitTimeoutSec")).toInt(loaded.turnWaitTimeoutSec);
    loaded.retryClickOnTimeout = root.value(QStringLiteral("retryClickOnTimeout")).toBool(loaded.retryClickOnTimeout);
    loaded.hasFallbackClickPoint = root.value(QStringLiteral("hasFallbackClickPoint")).toBool(false);

    const QJsonObject point = root.value(QStringLiteral("fallbackClickPoint")).toObject();
    loaded.fallbackClickPoint = QPoint(point.value(QStringLiteral("x")).toInt(),
                                       point.value(QStringLiteral("y")).toInt());

    const QJsonArray array = root.value(QStringLiteral("templates")).toArray();
    if (!array.isEmpty())
    {
        QList<TemplateConfig> templates;
        for (const QJsonValue& value : array)
        {
            const QJsonObject obj = value.toObject();
            TemplateConfig item;
            item.key = obj.value(QStringLiteral("key")).toString();
            item.displayName = obj.value(QStringLiteral("displayName")).toString();
            item.fileName = obj.value(QStringLiteral("fileName")).toString();
            item.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
            item.roi = rectFromJson(obj.value(QStringLiteral("roi")).toObject());
            templates.append(item);
        }
        loaded.templates = templates;
    }

    config = loaded;
    return true;
}
