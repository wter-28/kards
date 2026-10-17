#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

class QProcess;

// 基于 Windows 自带 OCR 引擎（Windows.Media.Ocr）的文字识别服务。
//
// 实现方式：常驻一个 PowerShell 子进程，通过标准输入输出交换“图片路径 -> 词表”，
// 避免每次识别都重新初始化 WinRT 引擎。
//   首次启动约需数秒初始化引擎；引擎就绪后单次识别约 10~15ms。
//
// 说明：Windows OCR 会把中文按“单字”切分成多个词（例如“继续”返回“继”“续”两个词），
// 因此匹配时按“同一行内拼接后的文本包含目标串”来判定。
class OcrService
{
public:
    struct Word
    {
        QString text;     // 识别到的词
        QRect   box;      // 包围盒（相对传入图片左上角）
        int     line = 0; // 所属文本行的序号（用于按行拼接）
    };

    OcrService();
    ~OcrService();

    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    bool isRunning() const;

    // 启动子进程并等待 OCR 引擎就绪
    bool start(QString* errorMessage);

    // 关闭子进程
    void stop();

    // 识别一张图片；words 为识别结果（坐标相对 image 左上角）
    bool recognize(const QImage& image,
                   QList<Word>* words,
                   QString* errorMessage,
                   int timeoutMs = 3000);

    // 在识别结果中查找目标文字（按行拼接后做子串匹配）
    // 命中时返回该文字在图片坐标下的包围盒
    static bool findText(const QList<Word>& words, const QString& target, QRect* box);

private:
    bool    ensureProcess(QString* errorMessage);
    bool    writeScriptFile(QString* errorMessage);
    QString readLine(int timeoutMs);

    QProcess* m_process = nullptr;
    QString   m_scriptPath;
    QString   m_imagePath;
};
