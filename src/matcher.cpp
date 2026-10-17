#include "matcher.h"

#include <QThread>
#include <QVector>
#include <QtConcurrent/QtConcurrentMap>

#include <cmath>
#include <vector>

namespace
{

struct SearchBest
{
    int    x = 0;
    int    y = 0;
    double score = -2.0;
};

inline long long rectSum(const std::vector<long long>& integral,
                         int imageWidth,
                         int x,
                         int y,
                         int tw,
                         int th)
{
    const size_t stride = static_cast<size_t>(imageWidth + 1);
    const size_t a = static_cast<size_t>(y) * stride + static_cast<size_t>(x);
    const size_t b = static_cast<size_t>(y) * stride + static_cast<size_t>(x + tw);
    const size_t c = static_cast<size_t>(y + th) * stride + static_cast<size_t>(x);
    const size_t d = static_cast<size_t>(y + th) * stride + static_cast<size_t>(x + tw);
    return integral[d] - integral[b] - integral[c] + integral[a];
}

// 在固定尺寸的图片与模板上做 ZNCC 搜索
class NccSearcher
{
public:
    NccSearcher(const QImage& grayImage, const QImage& grayTemplate)
        : m_image(grayImage.convertToFormat(QImage::Format_Grayscale8)),
          m_template(grayTemplate.convertToFormat(QImage::Format_Grayscale8))
    {
        m_imgW = m_image.width();
        m_imgH = m_image.height();
        m_imgStride = m_image.bytesPerLine();
        m_tw = m_template.width();
        m_th = m_template.height();
        m_count = m_tw * m_th;

        m_maxX = m_imgW - m_tw;
        m_maxY = m_imgH - m_th;

        if (m_maxX < 0 || m_maxY < 0 || m_count <= 0)
        {
            m_valid = false;
            return;
        }

        double sum = 0.0;
        for (int y = 0; y < m_th; ++y)
        {
            const uchar* row = m_template.constScanLine(y);
            for (int x = 0; x < m_tw; ++x)
            {
                sum += row[x];
            }
        }
        const double mean = sum / m_count;

        m_zeroMean.resize(static_cast<size_t>(m_count));
        double varSum = 0.0;
        for (int y = 0; y < m_th; ++y)
        {
            const uchar* row = m_template.constScanLine(y);
            for (int x = 0; x < m_tw; ++x)
            {
                const double v = row[x] - mean;
                m_zeroMean[static_cast<size_t>(y) * m_tw + x] = static_cast<float>(v);
                varSum += v * v;
            }
        }
        m_templateVar = varSum;

        m_valid = m_templateVar > 1e-6;

        if (m_valid)
        {
            buildIntegral();
        }
    }

    bool valid() const
    {
        return m_valid;
    }

    SearchBest searchParallel(int x0, int y0, int x1, int y1) const
    {
        SearchBest best;
        if (!m_valid)
        {
            return best;
        }

        if (x1 > m_maxX)
        {
            x1 = m_maxX;
        }
        if (y1 > m_maxY)
        {
            y1 = m_maxY;
        }
        if (x1 < x0 || y1 < y0)
        {
            return best;
        }

        const int rows = y1 - y0 + 1;
        const int threadCount = qMax(1, QThread::idealThreadCount());

        if (rows < 6 || threadCount <= 1)
        {
            return searchStripe(x0, y0, x1, y1);
        }

        const int stripes = qMin(threadCount, rows);
        const int rowsPerStripe = (rows + stripes - 1) / stripes;

        QVector<int> starts;
        for (int i = 0; i < stripes; ++i)
        {
            const int startRow = y0 + i * rowsPerStripe;
            if (startRow > y1)
            {
                break;
            }
            starts.append(startRow);
        }

        const auto results = QtConcurrent::blockingMapped(starts, [this, x0, x1, y1, rowsPerStripe](int startRow)
        {
            const int endRow = qMin(startRow + rowsPerStripe - 1, y1);
            return searchStripe(x0, startRow, x1, endRow);
        });

        for (const SearchBest& item : results)
        {
            if (item.score > best.score)
            {
                best = item;
            }
        }

        return best;
    }

private:
    SearchBest searchStripe(int x0, int y0, int x1, int y1) const
    {
        SearchBest best;

        for (int y = y0; y <= y1; ++y)
        {
            const uchar* baseRow = m_image.constScanLine(y);

            for (int x = x0; x <= x1; ++x)
            {
                const long long sum = rectSum(m_integralSum, m_imgW, x, y, m_tw, m_th);
                const long long sum2 = rectSum(m_integralSum2, m_imgW, x, y, m_tw, m_th);

                const double varImage = static_cast<double>(sum2) - static_cast<double>(sum) * static_cast<double>(sum) / static_cast<double>(m_count);
                if (varImage <= 1e-6)
                {
                    continue;
                }

                double numerator = 0.0;
                for (int j = 0; j < m_th; ++j)
                {
                    const uchar* pixelRow = baseRow + static_cast<size_t>(j) * static_cast<size_t>(m_imgStride) + x;
                    const float* templateRow = m_zeroMean.data() + static_cast<size_t>(j) * m_tw;

                    double accumulator = 0.0;
                    for (int i = 0; i < m_tw; ++i)
                    {
                        accumulator += static_cast<double>(pixelRow[i]) * static_cast<double>(templateRow[i]);
                    }
                    numerator += accumulator;
                }

                const double denominator = std::sqrt(varImage * m_templateVar);
                if (denominator <= 1e-9)
                {
                    continue;
                }

                const double score = numerator / denominator;
                if (score > best.score)
                {
                    best.score = score;
                    best.x = x;
                    best.y = y;
                }
            }
        }

        return best;
    }

    void buildIntegral()
    {
        const size_t stride = static_cast<size_t>(m_imgW + 1);
        m_integralSum.assign(stride * static_cast<size_t>(m_imgH + 1), 0);
        m_integralSum2.assign(stride * static_cast<size_t>(m_imgH + 1), 0);

        for (int y = 0; y < m_imgH; ++y)
        {
            const uchar* row = m_image.constScanLine(y);
            const size_t currentBase = static_cast<size_t>(y + 1) * stride;
            const size_t previousBase = static_cast<size_t>(y) * stride;

            long long rowSum = 0;
            long long rowSum2 = 0;

            for (int x = 0; x < m_imgW; ++x)
            {
                const long long value = row[x];
                rowSum += value;
                rowSum2 += value * value;

                m_integralSum[currentBase + static_cast<size_t>(x) + 1] = m_integralSum[previousBase + static_cast<size_t>(x) + 1] + rowSum;
                m_integralSum2[currentBase + static_cast<size_t>(x) + 1] = m_integralSum2[previousBase + static_cast<size_t>(x) + 1] + rowSum2;
            }
        }
    }

    QImage m_image;
    QImage m_template;

    int m_imgW = 0;
    int m_imgH = 0;
    int m_imgStride = 0;
    int m_tw = 0;
    int m_th = 0;
    int m_count = 0;
    int m_maxX = 0;
    int m_maxY = 0;

    bool m_valid = false;
    double m_templateVar = 0.0;

    std::vector<float> m_zeroMean;
    std::vector<long long> m_integralSum;
    std::vector<long long> m_integralSum2;
};

}   // namespace

QImage Matcher::toGray(const QImage& source)
{
    if (source.isNull())
    {
        return QImage();
    }
    if (source.format() == QImage::Format_Grayscale8)
    {
        return source;
    }
    return source.convertToFormat(QImage::Format_Grayscale8);
}

QImage Matcher::toEdge(const QImage& gray)
{
    const QImage source = toGray(gray);
    if (source.isNull() || source.width() < 3 || source.height() < 3)
    {
        return source;
    }

    const int width = source.width();
    const int height = source.height();

    QImage result(width, height, QImage::Format_Grayscale8);
    if (result.isNull())
    {
        return source;
    }
    result.fill(0);

    // 噪声抑制门限（0~255 梯度尺度）：低于该强度的微小起伏视为噪声，
    // 只保留字形笔画、图标轮廓这类强边缘，避免石纹噪点主导相关计算。
    constexpr double kNoiseFloor = 8.0;

    for (int y = 1; y < height - 1; ++y)
    {
        const uchar* previousRow = source.constScanLine(y - 1);
        const uchar* currentRow = source.constScanLine(y);
        const uchar* nextRow = source.constScanLine(y + 1);
        uchar* outputRow = result.scanLine(y);

        for (int x = 1; x < width - 1; ++x)
        {
            const double gx = (static_cast<double>(currentRow[x + 1]) - static_cast<double>(currentRow[x - 1])) * 0.5;
            const double gy = (static_cast<double>(nextRow[x]) - static_cast<double>(previousRow[x])) * 0.5;

            double magnitude = std::sqrt(gx * gx + gy * gy) - kNoiseFloor;
            if (magnitude <= 0.0)
            {
                magnitude = 0.0;
            }
            else if (magnitude >= 255.0)
            {
                magnitude = 255.0;
            }
            outputRow[x] = static_cast<uchar>(magnitude);
        }
    }

    return result;
}

QImage Matcher::downscale(const QImage& gray, int factor)
{
    if (gray.isNull() || factor <= 1)
    {
        return gray;
    }

    const int width = gray.width() / factor;
    const int height = gray.height() / factor;
    if (width < 1 || height < 1)
    {
        return gray;
    }

    QImage result(width, height, QImage::Format_Grayscale8);
    if (result.isNull())
    {
        return gray;
    }

    const int block = factor * factor;

    for (int y = 0; y < height; ++y)
    {
        uchar* destination = result.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            int sum = 0;
            for (int dy = 0; dy < factor; ++dy)
            {
                const uchar* sourceRow = gray.constScanLine(y * factor + dy);
                for (int dx = 0; dx < factor; ++dx)
                {
                    sum += sourceRow[x * factor + dx];
                }
            }
            destination[x] = static_cast<uchar>(sum / block);
        }
    }

    return result;
}

int Matcher::autoCoarseFactor(const QSize& roiSize, const QSize& templateSize)
{
    int maxByTemplate = qMin(templateSize.width(), templateSize.height()) / 8;
    if (maxByTemplate < 1)
    {
        maxByTemplate = 1;
    }

    const qint64 area = static_cast<qint64>(roiSize.width()) * static_cast<qint64>(roiSize.height());

    int factor = 1;
    if (area > 1500000)
    {
        factor = 8;
    }
    else if (area > 400000)
    {
        factor = 4;
    }
    else if (area > 60000)
    {
        factor = 2;
    }

    if (factor > maxByTemplate)
    {
        factor = maxByTemplate;
    }
    if (factor > 8)
    {
        factor = 8;
    }
    if (factor < 1)
    {
        factor = 1;
    }

    return factor;
}

Matcher::Result Matcher::match(const QImage& grayImage,
                               const QImage& grayTemplate,
                               const QRect& roi,
                               int coarseFactor,
                               double threshold)
{
    Result result;

    const QImage image = toGray(grayImage);
    const QImage plainTemplate = toGray(grayTemplate);
    if (image.isNull() || plainTemplate.isNull())
    {
        return result;
    }

    QRect area = roi.isEmpty() ? QRect(0, 0, image.width(), image.height()) : roi.intersected(QRect(0, 0, image.width(), image.height()));
    if (area.width() < plainTemplate.width() || area.height() < plainTemplate.height())
    {
        return result;
    }

    const QImage sub = image.copy(area);
    const int templateWidth = plainTemplate.width();
    const int templateHeight = plainTemplate.height();

    int startFactor = coarseFactor;
    if (startFactor <= 0)
    {
        startFactor = autoCoarseFactor(area.size(), plainTemplate.size());
    }
    if (startFactor < 1)
    {
        startFactor = 1;
    }

    QVector<int> levels;
    int current = startFactor;
    while (current > 1)
    {
        levels.append(current);
        current /= 2;
    }
    levels.append(1);

    int bestX = 0;
    int bestY = 0;
    double bestScore = -2.0;
    bool haveCandidate = false;
    int previousFactor = 0;

    for (const int level : levels)
    {
        const QImage levelImage = downscale(sub, level);
        const QImage levelTemplate = downscale(plainTemplate, level);

        NccSearcher searcher(levelImage, levelTemplate);
        if (!searcher.valid())
        {
            continue;
        }

        int x0 = 0;
        int y0 = 0;
        int x1 = levelImage.width() - levelTemplate.width();
        int y1 = levelImage.height() - levelTemplate.height();

        if (haveCandidate)
        {
            const int centerX = bestX / level;
            const int centerY = bestY / level;
            const int radius = qMax(1, previousFactor * 2 / level);

            x0 = qMax(x0, centerX - radius);
            y0 = qMax(y0, centerY - radius);
            x1 = qMin(x1, centerX + radius);
            y1 = qMin(y1, centerY + radius);

            if (x1 < x0 || y1 < y0)
            {
                x0 = 0;
                y0 = 0;
                x1 = levelImage.width() - levelTemplate.width();
                y1 = levelImage.height() - levelTemplate.height();
            }
        }

        const SearchBest candidate = searcher.searchParallel(x0, y0, x1, y1);
        if (candidate.score <= -1.5)
        {
            continue;
        }

        bestX = candidate.x * level;
        bestY = candidate.y * level;
        bestScore = candidate.score;
        previousFactor = level;
        haveCandidate = true;
    }

    if (!haveCandidate)
    {
        return result;
    }

    result.score = bestScore;
    result.rect = QRect(area.x() + bestX, area.y() + bestY, templateWidth, templateHeight);
    result.found = bestScore >= threshold;

    return result;
}
