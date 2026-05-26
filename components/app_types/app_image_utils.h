#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_image_calc_center_crop(uint32_t src_width,
                                uint32_t src_height,
                                uint32_t dst_width,
                                uint32_t dst_height,
                                uint32_t *crop_x,
                                uint32_t *crop_y,
                                uint32_t *crop_w,
                                uint32_t *crop_h);
uint8_t app_image_rgb565_to_r(uint16_t pixel);
uint8_t app_image_rgb565_to_g(uint16_t pixel);
uint8_t app_image_rgb565_to_b(uint16_t pixel);
uint8_t app_image_rgb565_to_gray(uint16_t pixel);

#ifdef __cplusplus
}
#endif
