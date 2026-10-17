#include "ocr.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStringList>

namespace
{

// 常驻 OCR 服务脚本（纯 ASCII，避免 PowerShell 5.1 读取脚本时的编码问题）。
// 协议：
//   子进程启动就绪后输出一行 READY（失败输出 #FATAL:...）；
//   之后父进程每送一行“图片路径”，子进程输出若干行词条，最后以 END 结束。
//   词条格式：W<TAB>文字<TAB>x<TAB>y<TAB>w<TAB>h<TAB>行号
const char* const kOcrServerScript = R"OCRPS(
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($WinRtTask, $ResultType)
{
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Globalization.Language, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.StorageFile, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.Streams.IRandomAccessStream, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.SoftwareBitmap, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Media.Ocr.OcrResult, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage((New-Object Windows.Globalization.Language 'zh-Hans-CN'))
if ($engine -eq $null) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() }

$utf8 = New-Object System.Text.UTF8Encoding($false)
$stdout = [Console]::OpenStandardOutput()

function WriteLine([string]$text)
{
    $bytes = $utf8.GetBytes($text + "`n")
    $stdout.Write($bytes, 0, $bytes.Length)
    $stdout.Flush()
}

if ($engine -eq $null) { WriteLine '#FATAL:no-ocr-engine'; exit 1 }

WriteLine 'READY'

$reader = New-Object System.IO.StreamReader([Console]::OpenStandardInput(), $utf8)
while ($true)
{
    $path = $reader.ReadLine()
    if ($path -eq $null) { break }
    $path = $path.Trim()
    if ($path -eq '') { continue }
    if ($path -eq 'QUIT') { break }

    try
    {
        $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($path)) ([Windows.Storage.StorageFile])
        $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
        $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
        $bitmap = Await ($decoder.GetSoftwareBitmapAsync([Windows.Graphics.Imaging.BitmapPixelFormat]::Bgra8, [Windows.Graphics.Imaging.BitmapAlphaMode]::Ignore)) ([Windows.Graphics.Imaging.SoftwareBitmap])
        $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])

        $lineIndex = 0
        foreach ($line in $result.Lines)
        {
            foreach ($word in $line.Words)
            {
                $b = $word.BoundingRect
                WriteLine ("W`t" + $word.Text + "`t" + [int]$b.X + "`t" + [int]$b.Y + "`t" + [int]$b.Width + "`t" + [int]$b.Height + "`t" + $lineIndex)
            }
            $lineIndex++
        }
        WriteLine 'END'
        $stream.Dispose()
    }
    catch
    {
        WriteLine ('#ERR:' + $_.Exception.Message)
        WriteLine 'END'
    }
}
)OCRPS";

}   // namespace

OcrService::OcrService()
{
}

OcrService::~OcrService()
{
    stop();
}

bool OcrService::isRunning() const
{
    return m_process != nullptr && m_process->state() == QProcess::Running;
}

bool OcrService::writeScriptFile(QString* errorMessage)
{
    const QString tempDir = QDir::tempPath();
    const QString scriptPath = QDir(tempDir).filePath(
        QStringLiteral("kards_ocr_server_%1.ps1").arg(QCoreApplication::applicationPid()));

    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入 OCR 脚本文件：%1").arg(scriptPath);
        }
        return false;
    }
    file.write(kOcrServerScript);
    file.close();

    m_scriptPath = scriptPath;
    m_imagePath = QDir(tempDir).filePath(
        QStringLiteral("kards_ocr_frame_%1.png").arg(QCoreApplication::applicationPid()));
    return true;
}

bool OcrService::ensureProcess(QString* errorMessage)
{
    if (m_process != nullptr)
    {
        m_process->kill();
        m_process->waitForFinished(2000);
        delete m_process;
        m_process = nullptr;
    }

    QString program = QStringLiteral("powershell");
    const QString systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    const QString fullPath = QDir(systemRoot).filePath(
        QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    if (QFile::exists(fullPath))
    {
        program = fullPath;
    }

    m_process = new QProcess();
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start(program, QStringList()
                                  << QStringLiteral("-NoProfile")
                                  << QStringLiteral("-ExecutionPolicy")
                                  << QStringLiteral("Bypass")
                                  << QStringLiteral("-File")
                                  << m_scriptPath);

    if (!m_process->waitForStarted(5000))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法启动 OCR 子进程（powershell）");
        }
        delete m_process;
        m_process = nullptr;
        return false;
    }

    // WinRT OCR 引擎初始化较慢，这里放宽等待时间
    const QString ready = readLine(15000);
    if (ready == QStringLiteral("READY"))
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        if (ready.startsWith(QStringLiteral("#FATAL")))
        {
            *errorMessage = QStringLiteral("系统未提供中文 OCR 引擎（%1）").arg(ready);
        }
        else if (ready.isEmpty())
        {
            *errorMessage = QStringLiteral("OCR 子进程启动超时");
        }
        else
        {
            *errorMessage = QStringLiteral("OCR 子进程异常：%1").arg(ready);
        }
    }

    m_process->kill();
    m_process->waitForFinished(2000);
    delete m_process;
    m_process = nullptr;
    return false;
}

bool OcrService::start(QString* errorMessage)
{
    if (isRunning())
    {
        return true;
    }
    if (m_scriptPath.isEmpty() && !writeScriptFile(errorMessage))
    {
        return false;
    }
    return ensureProcess(errorMessage);
}

void OcrService::stop()
{
    if (m_process != nullptr)
    {
        if (m_process->state() == QProcess::Running)
        {
            m_process->write("QUIT\n");
            m_process->waitForBytesWritten(500);
            m_process->waitForFinished(2000);
            if (m_process->state() != QProcess::NotRunning)
            {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }

        delete m_process;
        m_process = nullptr;
    }

    // 清理临时文件，避免每次运行在 TEMP 目录残留
    if (!m_scriptPath.isEmpty())
    {
        QFile::remove(m_scriptPath);
        m_scriptPath.clear();
    }
    if (!m_imagePath.isEmpty())
    {
        QFile::remove(m_imagePath);
        m_imagePath.clear();
    }
}

QString OcrService::readLine(int timeoutMs)
{
    if (m_process == nullptr)
    {
        return QString();
    }

    QElapsedTimer timer;
    timer.start();
    while (!m_process->canReadLine())
    {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
        {
            return QString();
        }
        if (!m_process->waitForReadyRead(remaining))
        {
            return QString();
        }
    }

    QByteArray raw = m_process->readLine();
    while (raw.endsWith('\n') || raw.endsWith('\r'))
    {
        raw.chop(1);
    }
    return QString::fromUtf8(raw);
}

bool OcrService::recognize(const QImage& image,
                           QList<Word>* words,
                           QString* errorMessage,
                           int timeoutMs)
{
    if (words != nullptr)
    {
        words->clear();
    }

    if (image.isNull())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("OCR 输入图片为空");
        }
        return false;
    }

    if (!isRunning() && !start(errorMessage))
    {
        return false;
    }

    if (!image.save(m_imagePath, "PNG"))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法保存 OCR 临时图片");
        }
        return false;
    }

    m_process->write(m_imagePath.toUtf8());
    m_process->write("\n");
    if (!m_process->waitForBytesWritten(2000))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("OCR 请求发送失败");
        }
        return false;
    }

    QList<Word> result;
    QString serverError;
    while (true)
    {
        const QString line = readLine(timeoutMs);
        if (line.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("OCR 识别超时或无响应");
            }
            return false;
        }
        if (line == QStringLiteral("END"))
        {
            break;
        }
        if (line.startsWith(QStringLiteral("#ERR")))
        {
            serverError = line.mid(4);
            continue;
        }
        if (line.startsWith(QStringLiteral("W\t")))
        {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() >= 7)
            {
                Word word;
                word.text = parts.at(1);
                word.box = QRect(parts.at(2).toInt(),
                                 parts.at(3).toInt(),
                                 parts.at(4).toInt(),
                                 parts.at(5).toInt());
                word.line = parts.at(6).toInt();
                result.append(word);
            }
        }
    }

    if (!serverError.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("OCR 识别失败：%1").arg(serverError);
        }
        return false;
    }

    if (words != nullptr)
    {
        *words = result;
    }
    return true;
}

bool OcrService::findText(const QList<Word>& words, const QString& target, QRect* box)
{
    if (target.isEmpty())
    {
        return false;
    }

    QList<int> lineIds;
    for (const Word& word : words)
    {
        if (!lineIds.contains(word.line))
        {
            lineIds.append(word.line);
        }
    }

    for (int lineId : lineIds)
    {
        QString text;
        QList<int> charOwner;   // 每个字符所属的词下标
        for (int i = 0; i < words.size(); ++i)
        {
            if (words.at(i).line != lineId)
            {
                continue;
            }
            text += words.at(i).text;
            for (int c = 0; c < words.at(i).text.size(); ++c)
            {
                charOwner.append(i);
            }
        }

        const int position = text.indexOf(target);
        if (position < 0)
        {
            continue;
        }

        const int end = position + target.size();
        QRect unionBox;
        bool hasBox = false;
        for (int c = position; c < end && c < charOwner.size(); ++c)
        {
            const QRect& wordBox = words.at(charOwner.at(c)).box;
            unionBox = hasBox ? unionBox.united(wordBox) : wordBox;
            hasBox = true;
        }

        if (box != nullptr)
        {
            *box = unionBox;
        }
        return true;
    }

    return false;
}
