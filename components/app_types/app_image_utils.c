#include "app_image_utils.h"

// 图像小工具：集中处理裁剪计算和 RGB565 颜色转换。

void app_image_calc_center_crop(uint32_t src_width,
                                uint32_t src_height,
                                uint32_t dst_width,
                                uint32_t dst_height,
                                uint32_t *crop_x,
                                uint32_t *crop_y,
                                uint32_t *crop_w,
                                uint32_t *crop_h)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = src_width;
    uint32_t h = src_height;
    if (src_width == 0U || src_height == 0U || dst_width == 0U || dst_height == 0U)
    {
        w = 0;
        h = 0;
    }
    // 原图更宽时裁掉左右两侧，保持目标宽高比。
    else if (((uint64_t)src_width * dst_height) > ((uint64_t)src_height * dst_width))
    {
        w = (uint32_t)(((uint64_t)src_height * dst_width) / dst_height);
        if (w == 0U || w > src_width)
        {
            w = src_width;
        }
        x = (src_width - w) / 2U;
    }
    // 原图更高时裁掉上下两侧，保持目标宽高比。
    else if (((uint64_t)src_width * dst_height) < ((uint64_t)src_height * dst_width))
    {
        h = (uint32_t)(((uint64_t)src_width * dst_height) / dst_width);
        if (h == 0U || h > src_height)
        {
            h = src_height;
        }
        y = (src_height - h) / 2U;
    }
    if (crop_x) *crop_x = x;
    if (crop_y) *crop_y = y;
    if (crop_w) *crop_w = w;
    if (crop_h) *crop_h = h;
}

uint8_t app_image_rgb565_to_r(uint16_t pixel)
{
    uint8_t r = (uint8_t)((pixel >> 11) & 0x1F);
    return (uint8_t)((r << 3) | (r >> 2));
}

uint8_t app_image_rgb565_to_g(uint16_t pixel)
{
    uint8_t g = (uint8_t)((pixel >> 5) & 0x3F);
    return (uint8_t)((g << 2) | (g >> 4));
}

uint8_t app_image_rgb565_to_b(uint16_t pixel)
{
    uint8_t b = (uint8_t)(pixel & 0x1F);
    return (uint8_t)((b << 3) | (b >> 2));
}

uint8_t app_image_rgb565_to_gray(uint16_t pixel)
{
    uint32_t r = app_image_rgb565_to_r(pixel);
    uint32_t g = app_image_rgb565_to_g(pixel);
    uint32_t b = app_image_rgb565_to_b(pixel);
    // 77/150/29 是 0.299/0.587/0.114 乘以 256 后的整数近似。
    return (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
}
