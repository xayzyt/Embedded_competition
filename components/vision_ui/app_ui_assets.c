#include "lvgl.h"

// 天气图标索引表：把天气 API 的 code 映射到对应 LVGL 图片资源。
// 只暴露查询函数，避免主屏 UI 直接依赖每个 weather_x 图标符号。

LV_IMAGE_DECLARE(weather_0);
LV_IMAGE_DECLARE(weather_1);
LV_IMAGE_DECLARE(weather_2);
LV_IMAGE_DECLARE(weather_3);
LV_IMAGE_DECLARE(weather_4);
LV_IMAGE_DECLARE(weather_5);
LV_IMAGE_DECLARE(weather_6);
LV_IMAGE_DECLARE(weather_7);
LV_IMAGE_DECLARE(weather_8);
LV_IMAGE_DECLARE(weather_9);
LV_IMAGE_DECLARE(weather_10);
LV_IMAGE_DECLARE(weather_11);
LV_IMAGE_DECLARE(weather_12);
LV_IMAGE_DECLARE(weather_13);
LV_IMAGE_DECLARE(weather_14);
LV_IMAGE_DECLARE(weather_15);
LV_IMAGE_DECLARE(weather_16);
LV_IMAGE_DECLARE(weather_17);
LV_IMAGE_DECLARE(weather_18);
LV_IMAGE_DECLARE(weather_19);
LV_IMAGE_DECLARE(weather_20);
LV_IMAGE_DECLARE(weather_21);
LV_IMAGE_DECLARE(weather_22);
LV_IMAGE_DECLARE(weather_23);
LV_IMAGE_DECLARE(weather_24);
LV_IMAGE_DECLARE(weather_26);
LV_IMAGE_DECLARE(weather_27);
LV_IMAGE_DECLARE(weather_28);
LV_IMAGE_DECLARE(weather_29);
LV_IMAGE_DECLARE(weather_30);
LV_IMAGE_DECLARE(weather_31);
LV_IMAGE_DECLARE(weather_32);
LV_IMAGE_DECLARE(weather_33);
LV_IMAGE_DECLARE(weather_34);
LV_IMAGE_DECLARE(weather_35);
LV_IMAGE_DECLARE(weather_36);
LV_IMAGE_DECLARE(weather_37);
LV_IMAGE_DECLARE(weather_38);
LV_IMAGE_DECLARE(weather_99);

const lv_image_dsc_t *app_ui_weather_image_src(int weather_code)
{
    // 心知天气 code 与本地资源不是完全连续的，缺失项复用同类图标或兜底图。
    switch (weather_code) {
    case 0: return &weather_0;
    case 1: return &weather_1;
    case 2: return &weather_2;
    case 3: return &weather_3;
    case 4: return &weather_4;
    case 5: return &weather_5;
    case 6: return &weather_6;
    case 7: return &weather_7;
    case 8: return &weather_8;
    case 9: return &weather_9;
    case 10: return &weather_10;
    case 11: return &weather_11;
    case 12: return &weather_12;
    case 13: return &weather_13;
    case 14: return &weather_14;
    case 15: return &weather_15;
    case 16: return &weather_16;
    case 17: return &weather_17;
    case 18: return &weather_18;
    case 19: return &weather_19;
    case 20: return &weather_20;
    case 21: return &weather_21;
    case 22: return &weather_22;
    case 23: return &weather_23;
    case 24:
    case 25:
        return &weather_24;
    case 26: return &weather_26;
    case 27: return &weather_27;
    case 28: return &weather_28;
    case 29: return &weather_29;
    case 30: return &weather_30;
    case 31: return &weather_31;
    case 32: return &weather_32;
    case 33: return &weather_33;
    case 34: return &weather_34;
    case 35: return &weather_35;
    case 36: return &weather_36;
    case 37: return &weather_37;
    case 38: return &weather_38;
    case 99:
    default:
        return &weather_99;
    }
}
