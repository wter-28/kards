#pragma once

#include <QImage>
#include <QRect>

// 灰度模板匹配：归一化互相关（ZNCC）+ 图像金字塔加速
class Matcher
{
public:
    struct Result
    {
        bool   found = false;
        double score = 0.0;      // 匹配分数，范围 [-1, 1]
        QRect  rect;             // 匹配到的矩形（与传入图片同一坐标系）
    };

    // 转成灰度图
    static QImage toGray(const QImage& source);

    // 按整数倍降采样（盒式平均）
    static QImage downscale(const QImage& gray, int factor);

    // 在图片中查找模板
    //  roi          : 搜索区域（图片坐标系），为空表示整幅图片
    //  coarseFactor : 金字塔起始缩放；<=0 表示自动选择
    //  threshold    : 判定阈值
    static Result match(const QImage& grayImage,
                        const QImage& grayTemplate,
                        const QRect& roi,
                        int coarseFactor,
                        double threshold);

    // 根据区域与模板大小自动选择起始缩放倍率
    static int autoCoarseFactor(const QSize& roiSize, const QSize& templateSize);
};
