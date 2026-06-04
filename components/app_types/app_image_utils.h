#pragma once
#include <stdint.h>

// 图像通用工具：不依赖具体业务模块，可被相机、视觉和抓图路径复用。

#ifdef __cplusplus
extern "C" {
#endif

// 按目标宽高比计算居中裁剪区域，输出仍位于原图坐标系。
void app_image_calc_center_crop(uint32_t src_width,
                                uint32_t src_height,
                                uint32_t dst_width,
                                uint32_t dst_height,
                                uint32_t *crop_x,
                                uint32_t *crop_y,
                                uint32_t *crop_w,
                                uint32_t *crop_h);
// RGB565 单通道展开到 8 bit。
uint8_t app_image_rgb565_to_r(uint16_t pixel);
uint8_t app_image_rgb565_to_g(uint16_t pixel);
uint8_t app_image_rgb565_to_b(uint16_t pixel);
// RGB565 转灰度，权重接近 BT.601 整数近似。
uint8_t app_image_rgb565_to_gray(uint16_t pixel);

#ifdef __cplusplus
}
#endif
