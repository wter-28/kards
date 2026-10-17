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

    add(QStringLiteral("start"),     QStringLiteral("图片1   开始"),        QStringLiteral("1.png"));
    add(QStringLiteral("battle"),    QStringLiteral("查看战场"),            QStringLiteral("12.png"));
    add(QStringLiteral("confirm"),   QStringLiteral("图片3   确定"),        QStringLiteral("3.png"));
    add(QStringLiteral("endTurn"),   QStringLiteral("图片5   结束回合"),    QStringLiteral("5.png"));
    add(QStringLiteral("menu"),      QStringLiteral("图片10  设置齿轮"),    QStringLiteral("10.png"));
    add(QStringLiteral("surrender"), QStringLiteral("图片11  投降"),        QStringLiteral("11.png"));

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
    root.insert(QStringLiteral("contOcrEnabled"), config.contOcrEnabled);
    root.insert(QStringLiteral("contOcrText"), config.contOcrText);
    root.insert(QStringLiteral("contOcrRoi"), rectToJson(config.contOcrRoi));

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
    loaded.contOcrEnabled = root.value(QStringLiteral("contOcrEnabled")).toBool(loaded.contOcrEnabled);
    loaded.contOcrText = root.value(QStringLiteral("contOcrText")).toString(loaded.contOcrText);
    loaded.contOcrRoi = rectFromJson(root.value(QStringLiteral("contOcrRoi")).toObject());

    // 以内置默认模板为基准，再用配置文件中同一键名的项覆盖文件名/启用状态/搜索区域。
    // 这样既能保留用户设置，也能在新增默认模板（键名）时自动补齐；
    // 旧版本遗留的、键名不一致的模板项会被忽略，不会破坏流程。
    QList<TemplateConfig> templates = defaultTemplates();
    const QJsonArray array = root.value(QStringLiteral("templates")).toArray();
    for (const QJsonValue& value : array)
    {
        const QJsonObject obj = value.toObject();
        const QString key = obj.value(QStringLiteral("key")).toString();

        for (TemplateConfig& item : templates)
        {
            if (item.key != key)
            {
                continue;
            }
            item.fileName = obj.value(QStringLiteral("fileName")).toString(item.fileName);
            item.enabled = obj.value(QStringLiteral("enabled")).toBool(item.enabled);
            item.roi = rectFromJson(obj.value(QStringLiteral("roi")).toObject());
            break;
        }
    }
    loaded.templates = templates;

    config = loaded;
    return true;
}
