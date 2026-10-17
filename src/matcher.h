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

    // 边缘特征图：中心差分梯度幅值 + 噪声抑制。
    // 同款按钮（仅文字不同）在灰度上极易误配，改用边缘特征后，
    // 匹配由字形/图标结构主导，扁平背景噪声贡献被压掉，判别力大幅提升。
    static QImage toEdge(const QImage& gray);

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
