/*
 * 逐行详细注释版说明：
 * 1) 这一版把原先偏空泛的说明改成了“逐行解释当前代码在干什么、为什么这么写”；
 * 2) 我尽量保证每一条有效代码前面都有一条可读注释，方便你顺着执行流程往下看；
 * 3) 注释只做解释，不改原来的接口、控制流和编译结果；你可以直接把这些文件替换回工程。
 */







/* 引入本项目的 app_ui 模块头文件；这样当前文件才能直接调用这个模块已经对外公开的函数和类型，而不用在这里重复声明一遍。 */
#include "app_ui.h"

/* 引入 stdio.h；标准输入输出头文件；常见的 printf、snprintf 等格式化输出接口都在这里声明。 */
#include <stdio.h>
/* 引入 inttypes.h；这个头文件为当前文件补充外部接口声明，避免编译器把后面用到的类型和函数当成未知符号。 */
#include <inttypes.h>

/* 引入 lvgl.h；LVGL 图形库总头文件；创建控件、刷新界面、管理样式都从这里进入。 */
#include "lvgl.h"
/* 引入 bsp/esp-bsp.h；乐鑫 BSP 总头文件；板级资源封装通常会从这里统一引入。 */
#include "bsp/esp-bsp.h"
/* 引入 bsp/display.h；BSP 显示相关头文件；屏幕句柄、显示锁、面板操作等接口一般在这里。 */
#include "bsp/display.h"














/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#ifndef BSP_CAMERA_ROTATION


































































































/* 定义宏 BSP_CAMERA_ROTATION；这里把“BSP、相机、ROTATION”集中写成常量 0，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define BSP_CAMERA_ROTATION 0
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif


/* 定义宏 HUD_SRC_W；这里把“HUD、源、W”集中写成常量 240，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define HUD_SRC_W               240
/* 定义宏 HUD_SRC_H；这里把“HUD、源、H”集中写成常量 180，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define HUD_SRC_H               180
/* 定义宏 HUD_LOCK_SEG_COUNT；这里把“HUD、锁、SEG、计数”集中写成常量 5，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define HUD_LOCK_SEG_COUNT      5
/* 定义宏 HUD_AUTH_SHOW_MS；这里把“HUD、AUTH、SHOW、毫秒”集中写成常量 1200，后面凡是依赖这个参数的地方都直接引用它，避免到处散落魔法数字。 */
#define HUD_AUTH_SHOW_MS        1200



/* 这里把 NULL 写入 static、lv、obj、t、s、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_status = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、coord；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_coord = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、视觉；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_vision = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、接驳；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_dock = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、hint；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_hint = NULL;


/* 这里把 NULL 写入 static、lv、obj、t、s、hud、layer；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_hud_layer = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、track、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_track_box = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、cross、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_cross_h = NULL;

/* 这里把 NULL 写入 static、lv、obj、t、s、cross、v；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_cross_v = NULL;

/* 这里把 {0} 写入 static、lv、obj、t、s、锁、seg、HUD、锁、SEG、计数；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_lock_seg[HUD_LOCK_SEG_COUNT] = {0};

/* 这里把 NULL 写入 static、lv、obj、t、s、auth；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
static lv_obj_t *s_auth = NULL;


/* 这里定义变量 s_have_last_box，类型是 static bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
static bool s_have_last_box = false;

/* 这里定义变量 s_last_box_x，类型是 static int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static int32_t s_last_box_x = 0;

/* 这里定义变量 s_last_box_y，类型是 static int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static int32_t s_last_box_y = 0;

/* 这里定义变量 s_last_box_w，类型是 static int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static int32_t s_last_box_w = 0;

/* 这里定义变量 s_last_box_h，类型是 static int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static int32_t s_last_box_h = 0;

/* 这里定义变量 s_last_hud_state，类型是 static app_dock_state_t，并且在声明时就把初值设成 APP_DOCK_STATE_SEARCHING；这样后面第一次使用它时就是一个确定状态。 */
static app_dock_state_t s_last_hud_state = APP_DOCK_STATE_SEARCHING;

/* 这里定义变量 s_auth_deadline_ms，类型是 static uint32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
static uint32_t s_auth_deadline_ms = 0;






/* 调用本项目模块接口 app_get_active_screen；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
static lv_obj_t *app_get_active_screen(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if LVGL_VERSION_MAJOR >= 9

    /* 这里把 lv_screen_active() 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return lv_screen_active();
/* 进入条件编译的兜底分支；前面的编译条件都不满足时会走这里。 */
#else

    /* 这里把 lv_scr_act() 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return lv_scr_act();
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_style_label；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_label(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202020), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_text_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_pad_all；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_pad_all(obj, 6, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_radius；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_radius(obj, 4, 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_style_hud_layer；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_hud_layer(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);

    /* 调用 LVGL 图形库接口 lv_obj_clear_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
/* 开始一段条件编译；只有满足这个编译期条件时，下面的代码才会被真正编进固件。 */
#if LVGL_VERSION_MAJOR >= 9

    /* 调用 LVGL 图形库接口 lv_obj_remove_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
/* 进入条件编译的兜底分支；前面的编译条件都不满足时会走这里。 */
#else

    /* 调用 LVGL 图形库接口 lv_obj_clear_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
/* 结束上面那段条件编译范围；从这一行往后，代码重新回到正常编译路径。 */
#endif
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_style_cross_line；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_cross_line(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x24D1A0), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_width；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_width(obj, 0, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_radius；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_radius(obj, 0, 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_style_track_box；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_track_box(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_width；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_width(obj, 3, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFD34D), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_radius；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_radius(obj, 0, 0);

    /* 调用 LVGL 图形库接口 lv_obj_add_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_style_lock_seg；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_lock_seg(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_radius；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_radius(obj, 3, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_width；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_width(obj, 1, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_color(obj, lv_color_hex(0x808080), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x3A3A3A), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}







/* 这里开始定义函数 app_ui_style_hint_label；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_hint_label(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101820), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_text_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_text_color(obj, lv_color_hex(0xD8F3FF), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_width；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_width(obj, 1, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2A4A58), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_pad_hor；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_pad_hor(obj, 8, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_pad_ver；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_pad_ver(obj, 5, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_radius；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_radius(obj, 4, 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_style_auth_label；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_style_auth_label(lv_obj_t *obj)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x163E31), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)192, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_width；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_width(obj, 2, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2FE0A5), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_text_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_text_color(obj, lv_color_hex(0xE9FFF7), 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_pad_hor；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_pad_hor(obj, 14, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_pad_ver；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_pad_ver(obj, 10, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_radius；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_radius(obj, 6, 0);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_state_color；返回类型是 static lv_color_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static lv_color_t app_ui_state_color(app_dock_state_t state, bool hold_box)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 hold_box；只有条件成立，后面的分支代码才会执行。 */
    if (hold_box) {

        /* 这里把 lv_color_hex(0x7E8A93) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return lv_color_hex(0x7E8A93);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (state) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_WRONG_ID:

            /* 这里把 lv_color_hex(0xFF5D5D) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return lv_color_hex(0xFF5D5D);

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_TRACKING:

            /* 这里把 lv_color_hex(0xFFD34D) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return lv_color_hex(0xFFD34D);

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_ALIGNED:

            /* 这里把 lv_color_hex(0x63D5FF) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return lv_color_hex(0x63D5FF);

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_READY_TO_DOCK:

            /* 这里把 lv_color_hex(0x31E08A) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return lv_color_hex(0x31E08A);

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case APP_DOCK_STATE_SEARCHING:

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:

            /* 这里把 lv_color_hex(0x7E8A93) 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
            return lv_color_hex(0x7E8A93);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_calc_fit_dims；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_calc_fit_dims(int32_t src_w, int32_t src_h, int32_t *fit_w, int32_t *fit_h)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (fit_w == NULL) || (fit_h == NULL) || (src_w <= 0) || (src_h <= 0)；只有条件成立，后面的分支代码才会执行。 */
    if ((fit_w == NULL) || (fit_h == NULL) || (src_w <= 0) || (src_h <= 0)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 src_aspect，类型是 float，并且在声明时就把初值设成 (float)src_w / (float)src_h；这样后面第一次使用它时就是一个确定状态。 */
    float src_aspect = (float)src_w / (float)src_h;

    /* 这里定义变量 dst_aspect，类型是 float，并且在声明时就把初值设成 (float)BSP_LCD_H_RES / (float)BSP_LCD_V_RES；这样后面第一次使用它时就是一个确定状态。 */
    float dst_aspect = (float)BSP_LCD_H_RES / (float)BSP_LCD_V_RES;


    /* 这里开始判断条件 src_aspect > dst_aspect；只有条件成立，后面的分支代码才会执行。 */
    if (src_aspect > dst_aspect) {
        /* 这里把 BSP_LCD_H_RES 写入 fit、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *fit_w = BSP_LCD_H_RES;
        /* 这里把 (int32_t)((float)BSP_LCD_H_RES / src_aspect + 0.5f) 写入 fit、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *fit_h = (int32_t)((float)BSP_LCD_H_RES / src_aspect + 0.5f);
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {
        /* 这里把 BSP_LCD_V_RES 写入 fit、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *fit_h = BSP_LCD_V_RES;
        /* 这里把 (int32_t)((float)BSP_LCD_V_RES * src_aspect + 0.5f) 写入 fit、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *fit_w = (int32_t)((float)BSP_LCD_V_RES * src_aspect + 0.5f);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_get_rotated_dims；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_get_rotated_dims(int32_t *rot_w, int32_t *rot_h)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (rot_w == NULL) || (rot_h == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((rot_w == NULL) || (rot_h == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 (BSP_CAMERA_ROTATION == 90) || (BSP_CAMERA_ROTATION == 270)；只有条件成立，后面的分支代码才会执行。 */
    if ((BSP_CAMERA_ROTATION == 90) || (BSP_CAMERA_ROTATION == 270)) {
        /* 这里把 HUD_SRC_H 写入 rot、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *rot_w = HUD_SRC_H;
        /* 这里把 HUD_SRC_W 写入 rot、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *rot_h = HUD_SRC_W;
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {
        /* 这里把 HUD_SRC_W 写入 rot、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *rot_w = HUD_SRC_W;
        /* 这里把 HUD_SRC_H 写入 rot、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *rot_h = HUD_SRC_H;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_transform_src_point；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_transform_src_point(float x, float y, float *out_x, float *out_y)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (out_x == NULL) || (out_y == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((out_x == NULL) || (out_y == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 switch 分支选择；通常是根据状态枚举或命令码决定走哪条处理路径。 */
    switch (BSP_CAMERA_ROTATION) {

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case 90:
            /* 这里把 (float)HUD_SRC_H - y 写入 out、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_x = (float)HUD_SRC_H - y;
            /* 这里把 x 写入 out、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_y = x;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case 180:
            /* 这里把 (float)HUD_SRC_W - x 写入 out、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_x = (float)HUD_SRC_W - x;
            /* 这里把 (float)HUD_SRC_H - y 写入 out、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_y = (float)HUD_SRC_H - y;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case 270:
            /* 这里把 y 写入 out、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_x = y;
            /* 这里把 (float)HUD_SRC_W - x 写入 out、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_y = (float)HUD_SRC_W - x;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;

        /* 这里是 switch 的一个 case；当上面的分支值等于这里的标签时，会执行下面的代码。 */
        case 0:

        /* 这里是 switch 的默认分支；当所有 case 都不匹配时，会落到这里。 */
        default:
            /* 这里把 x 写入 out、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_x = x;
            /* 这里把 y 写入 out、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            *out_y = y;

            /* 这里主动跳出当前分支或循环；避免继续落入后面的 case，或者结束本轮迭代。 */
            break;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_clamp_i32；返回类型是 static int32_t，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static int32_t app_ui_clamp_i32(int32_t v, int32_t lo, int32_t hi)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 v < lo；只有条件成立，后面的分支代码才会执行。 */
    if (v < lo) return lo;

    /* 这里开始判断条件 v > hi；只有条件成立，后面的分支代码才会执行。 */
    if (v > hi) return hi;

    /* 这里把 v 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return v;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_ui_map_bbox_to_screen；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_map_bbox_to_screen(const app_vision_result_t *vision,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      int32_t *x,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      int32_t *y,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      int32_t *w,
                                      /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                      int32_t *h)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 (vision == NULL) || (x == NULL) || (y == NULL) || (w == NULL) || (h == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((vision == NULL) || (x == NULL) || (y == NULL) || (w == NULL) || (h == NULL)) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 src_x1，类型是 float，并且在声明时就把初值设成 (float)vision->bbox_x；这样后面第一次使用它时就是一个确定状态。 */
    float src_x1 = (float)vision->bbox_x;

    /* 这里定义变量 src_y1，类型是 float，并且在声明时就把初值设成 (float)vision->bbox_y；这样后面第一次使用它时就是一个确定状态。 */
    float src_y1 = (float)vision->bbox_y;

    /* 这里定义变量 src_x2，类型是 float，并且在声明时就把初值设成 (float)(vision->bbox_x + vision->bbox_w)；这样后面第一次使用它时就是一个确定状态。 */
    float src_x2 = (float)(vision->bbox_x + vision->bbox_w);

    /* 这里定义变量 src_y2，类型是 float，并且在声明时就把初值设成 (float)(vision->bbox_y + vision->bbox_h)；这样后面第一次使用它时就是一个确定状态。 */
    float src_y2 = (float)(vision->bbox_y + vision->bbox_h);

    /* 这里定义变量 pts，类型是 float，并且在声明时就把初值设成 {；这样后面第一次使用它时就是一个确定状态。 */
    float pts[4][2] = {
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        {src_x1, src_y1},
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        {src_x2, src_y1},
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        {src_x2, src_y2},
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        {src_x1, src_y2},
    /* 这里结束一个结构体初始化、数组初始化或代码块，并顺带写上分号把整条语句收尾。 */
    };


    /* 这里定义变量 rot_w，类型是 int32_t，并且在声明时就把初值设成 HUD_SRC_W；这样后面第一次使用它时就是一个确定状态。 */
    int32_t rot_w = HUD_SRC_W;

    /* 这里定义变量 rot_h，类型是 int32_t，并且在声明时就把初值设成 HUD_SRC_H；这样后面第一次使用它时就是一个确定状态。 */
    int32_t rot_h = HUD_SRC_H;

    /* 调用本项目模块接口 app_ui_get_rotated_dims；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_get_rotated_dims(&rot_w, &rot_h);


    /* 这里定义变量 fit_w，类型是 int32_t，并且在声明时就把初值设成 rot_w；这样后面第一次使用它时就是一个确定状态。 */
    int32_t fit_w = rot_w;

    /* 这里定义变量 fit_h，类型是 int32_t，并且在声明时就把初值设成 rot_h；这样后面第一次使用它时就是一个确定状态。 */
    int32_t fit_h = rot_h;

    /* 调用本项目模块接口 app_ui_calc_fit_dims；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_calc_fit_dims(rot_w, rot_h, &fit_w, &fit_h);


    /* 这里定义变量 off_x，类型是 int32_t，并且在声明时就把初值设成 (BSP_LCD_H_RES - fit_w) / 2；这样后面第一次使用它时就是一个确定状态。 */
    int32_t off_x = (BSP_LCD_H_RES - fit_w) / 2;

    /* 这里定义变量 off_y，类型是 int32_t，并且在声明时就把初值设成 (BSP_LCD_V_RES - fit_h) / 2；这样后面第一次使用它时就是一个确定状态。 */
    int32_t off_y = (BSP_LCD_V_RES - fit_h) / 2;


    /* 这里定义变量 min_x，类型是 float，并且在声明时就把初值设成 100000.0f；这样后面第一次使用它时就是一个确定状态。 */
    float min_x = 100000.0f;

    /* 这里定义变量 min_y，类型是 float，并且在声明时就把初值设成 100000.0f；这样后面第一次使用它时就是一个确定状态。 */
    float min_y = 100000.0f;

    /* 这里定义变量 max_x，类型是 float，并且在声明时就把初值设成 -100000.0f；这样后面第一次使用它时就是一个确定状态。 */
    float max_x = -100000.0f;

    /* 这里定义变量 max_y，类型是 float，并且在声明时就把初值设成 -100000.0f；这样后面第一次使用它时就是一个确定状态。 */
    float max_y = -100000.0f;


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < 4; i++) {

        /* 这里定义变量 rx，类型是 float，并且在声明时就把初值设成 0.0f；这样后面第一次使用它时就是一个确定状态。 */
        float rx = 0.0f;

        /* 这里定义变量 ry，类型是 float，并且在声明时就把初值设成 0.0f；这样后面第一次使用它时就是一个确定状态。 */
        float ry = 0.0f;

        /* 调用本项目模块接口 app_ui_transform_src_point；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_transform_src_point(pts[i][0], pts[i][1], &rx, &ry);


        /* 这里定义变量 sx，类型是 float，并且在声明时就把初值设成 (float)off_x + rx * (float)fit_w / (float)rot_w；这样后面第一次使用它时就是一个确定状态。 */
        float sx = (float)off_x + rx * (float)fit_w / (float)rot_w;

        /* 这里定义变量 sy，类型是 float，并且在声明时就把初值设成 (float)off_y + ry * (float)fit_h / (float)rot_h；这样后面第一次使用它时就是一个确定状态。 */
        float sy = (float)off_y + ry * (float)fit_h / (float)rot_h;


        /* 这里开始判断条件 sx < min_x；只有条件成立，后面的分支代码才会执行。 */
        if (sx < min_x) min_x = sx;

        /* 这里开始判断条件 sy < min_y；只有条件成立，后面的分支代码才会执行。 */
        if (sy < min_y) min_y = sy;

        /* 这里开始判断条件 sx > max_x；只有条件成立，后面的分支代码才会执行。 */
        if (sx > max_x) max_x = sx;

        /* 这里开始判断条件 sy > max_y；只有条件成立，后面的分支代码才会执行。 */
        if (sy > max_y) max_y = sy;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里把 app_ui_clamp_i32((int32_t)(min_x + 0.5f), 0, BSP_LCD_H_RES - 1) 写入 x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *x = app_ui_clamp_i32((int32_t)(min_x + 0.5f), 0, BSP_LCD_H_RES - 1);
    /* 这里把 app_ui_clamp_i32((int32_t)(min_y + 0.5f), 0, BSP_LCD_V_RES - 1) 写入 y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *y = app_ui_clamp_i32((int32_t)(min_y + 0.5f), 0, BSP_LCD_V_RES - 1);
    /* 这里把 app_ui_clamp_i32((int32_t)(max_x - min_x + 0.5f), 12, BSP_LCD_H_RES) 写入 w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *w = app_ui_clamp_i32((int32_t)(max_x - min_x + 0.5f), 12, BSP_LCD_H_RES);
    /* 这里把 app_ui_clamp_i32((int32_t)(max_y - min_y + 0.5f), 12, BSP_LCD_V_RES) 写入 h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    *h = app_ui_clamp_i32((int32_t)(max_y - min_y + 0.5f), 12, BSP_LCD_V_RES);


    /* 这里开始判断条件 (*x + *w) > BSP_LCD_H_RES；只有条件成立，后面的分支代码才会执行。 */
    if ((*x + *w) > BSP_LCD_H_RES) {
        /* 这里把 BSP_LCD_H_RES - *x 写入 w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *w = BSP_LCD_H_RES - *x;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }

    /* 这里开始判断条件 (*y + *h) > BSP_LCD_V_RES；只有条件成立，后面的分支代码才会执行。 */
    if ((*y + *h) > BSP_LCD_V_RES) {
        /* 这里把 BSP_LCD_V_RES - *y 写入 h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        *h = BSP_LCD_V_RES - *y;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_update_lock_bar；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_update_lock_bar(const app_dock_judge_result_t *dock)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 filled，类型是 uint8_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    uint8_t filled = 0;

    /* 这里定义变量 active，类型是 lv_color_t，并且在声明时就把初值设成 lv_color_hex(0xFFD34D)；这样后面第一次使用它时就是一个确定状态。 */
    lv_color_t active = lv_color_hex(0xFFD34D);


    /* 这里开始判断条件 dock != NULL；只有条件成立，后面的分支代码才会执行。 */
    if (dock != NULL) {

        /* 这里把 (dock->stable_count > HUD_LOCK_SEG_COUNT) ? HUD_LOCK_SEG_COUNT : (uint8_t)dock->stable_count 写入 filled；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        filled = (dock->stable_count > HUD_LOCK_SEG_COUNT) ? HUD_LOCK_SEG_COUNT : (uint8_t)dock->stable_count;

        /* 这里开始判断条件 dock->state == APP_DOCK_STATE_READY_TO_DOCK；只有条件成立，后面的分支代码才会执行。 */
        if (dock->state == APP_DOCK_STATE_READY_TO_DOCK) {

            /* 这里把 lv_color_hex(0x31E08A) 写入 active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            active = lv_color_hex(0x31E08A);

            /* 这里把 HUD_LOCK_SEG_COUNT 写入 filled；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            filled = HUD_LOCK_SEG_COUNT;
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        } else if (dock->state == APP_DOCK_STATE_ALIGNED) {

            /* 这里把 lv_color_hex(0x63D5FF) 写入 active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            active = lv_color_hex(0x63D5FF);
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        } else if (dock->state == APP_DOCK_STATE_WRONG_ID) {

            /* 这里把 lv_color_hex(0xFF5D5D) 写入 active；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            active = lv_color_hex(0xFF5D5D);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {

        /* 这里开始判断条件 s_lock_seg[i] == NULL；只有条件成立，后面的分支代码才会执行。 */
        if (s_lock_seg[i] == NULL) {

            /* 这里直接结束当前这一轮循环，马上进入下一轮；常用于过滤掉当前不想处理的数据。 */
            continue;
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }


        /* 这里开始判断条件 i < filled；只有条件成立，后面的分支代码才会执行。 */
        if (i < filled) {

            /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_style_bg_color(s_lock_seg[i], active, 0);

            /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_style_bg_opa(s_lock_seg[i], LV_OPA_90, 0);

            /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_style_border_color(s_lock_seg[i], active, 0);
        /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
        } else {

            /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_style_bg_color(s_lock_seg[i], lv_color_hex(0x3A3A3A), 0);

            /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_style_bg_opa(s_lock_seg[i], LV_OPA_70, 0);

            /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_style_border_color(s_lock_seg[i], lv_color_hex(0x808080), 0);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_update_auth_banner；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_update_auth_banner(app_dock_state_t state)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里定义变量 now_ms，类型是 uint32_t，并且在声明时就把初值设成 lv_tick_get()；这样后面第一次使用它时就是一个确定状态。 */
    uint32_t now_ms = lv_tick_get();


    /* 这里开始判断条件 (state == APP_DOCK_STATE_READY_TO_DOCK；只有条件成立，后面的分支代码才会执行。 */
    if ((state == APP_DOCK_STATE_READY_TO_DOCK) &&
        /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
        (s_last_hud_state != APP_DOCK_STATE_READY_TO_DOCK)) {
        /* 这里把 now_ms + HUD_AUTH_SHOW_MS 写入 s、auth、deadline、毫秒；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_auth_deadline_ms = now_ms + HUD_AUTH_SHOW_MS;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 (s_auth != NULL) && (s_auth_deadline_ms != 0U) && (now_ms <= s_auth_deadline_ms)；只有条件成立，后面的分支代码才会执行。 */
    if ((s_auth != NULL) && (s_auth_deadline_ms != 0U) && (now_ms <= s_auth_deadline_ms)) {

        /* 调用 LVGL 图形库接口 lv_obj_clear_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_clear_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    } else if (s_auth != NULL) {

        /* 调用 LVGL 图形库接口 lv_obj_add_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 state 写入 s、last、hud、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    s_last_hud_state = state;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}





/* 这里开始定义函数 app_ui_set_track_box；返回类型是 static void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
static void app_ui_set_track_box(bool show,
                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                 int32_t x,
                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                 int32_t y,
                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                 int32_t w,
                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                 int32_t h,
                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                 lv_color_t color,
                                 /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                                 lv_opa_t opa)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这里开始判断条件 s_track_box == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_track_box == NULL) {
        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !show；只有条件成立，后面的分支代码才会执行。 */
    if (!show) {

        /* 调用 LVGL 图形库接口 lv_obj_add_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_add_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用 LVGL 图形库接口 lv_obj_set_pos；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_pos(s_track_box, x, y);

    /* 调用 LVGL 图形库接口 lv_obj_set_size；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_size(s_track_box, w, h);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_color(s_track_box, color, 0);

    /* 调用 LVGL 图形库接口 lv_obj_set_style_border_opa；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_set_style_border_opa(s_track_box, opa, 0);

    /* 调用 LVGL 图形库接口 lv_obj_clear_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_obj_clear_flag(s_track_box, LV_OBJ_FLAG_HIDDEN);
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_create；返回类型是 bool，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
bool app_ui_create(void)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里把 false 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
        return false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里把 app_get_active_screen() 写入 lv、obj、t、scr；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
    lv_obj_t *scr = app_get_active_screen();


    /* 这里开始判断条件 s_hud_layer == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_hud_layer == NULL) {

        /* 这里把 lv_obj_create(scr) 写入 s、hud、layer；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_hud_layer = lv_obj_create(scr);

        /* 调用本项目模块接口 app_ui_style_hud_layer；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_hud_layer(s_hud_layer);

        /* 调用 LVGL 图形库接口 lv_obj_set_size；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_size(s_hud_layer, BSP_LCD_H_RES, BSP_LCD_V_RES);

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_hud_layer, LV_ALIGN_CENTER, 0, 0);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_track_box == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_track_box == NULL) {

        /* 这里把 lv_obj_create(s_hud_layer) 写入 s、track、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_track_box = lv_obj_create(s_hud_layer);

        /* 调用本项目模块接口 app_ui_style_track_box；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_track_box(s_track_box);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_cross_h == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cross_h == NULL) {

        /* 这里把 lv_obj_create(s_hud_layer) 写入 s、cross、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_cross_h = lv_obj_create(s_hud_layer);

        /* 调用本项目模块接口 app_ui_style_cross_line；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_cross_line(s_cross_h);

        /* 调用 LVGL 图形库接口 lv_obj_set_size；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_size(s_cross_h, 48, 2);

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_cross_h, LV_ALIGN_CENTER, 0, 0);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_cross_v == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_cross_v == NULL) {

        /* 这里把 lv_obj_create(s_hud_layer) 写入 s、cross、v；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_cross_v = lv_obj_create(s_hud_layer);

        /* 调用本项目模块接口 app_ui_style_cross_line；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_cross_line(s_cross_v);

        /* 调用 LVGL 图形库接口 lv_obj_set_size；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_size(s_cross_v, 2, 48);

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_cross_v, LV_ALIGN_CENTER, 0, 0);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 seg_w，类型是 const int32_t，并且在声明时就把初值设成 30；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t seg_w = 30;

    /* 这里定义变量 seg_h，类型是 const int32_t，并且在声明时就把初值设成 14；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t seg_h = 14;

    /* 这里定义变量 seg_gap，类型是 const int32_t，并且在声明时就把初值设成 8；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t seg_gap = 8;

    /* 这里定义变量 total_w，类型是 const int32_t，并且在声明时就把初值设成 (HUD_LOCK_SEG_COUNT * seg_w) + ((HUD_LOCK_SEG_COUNT - 1) * seg_gap)；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t total_w = (HUD_LOCK_SEG_COUNT * seg_w) + ((HUD_LOCK_SEG_COUNT - 1) * seg_gap);

    /* 这里定义变量 start_x，类型是 const int32_t，并且在声明时就把初值设成 (BSP_LCD_H_RES - total_w) / 2；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t start_x = (BSP_LCD_H_RES - total_w) / 2;

    /* 这里定义变量 y，类型是 const int32_t，并且在声明时就把初值设成 22；这样后面第一次使用它时就是一个确定状态。 */
    const int32_t y = 22;


    /* 这里开始一个 for 循环；同一段逻辑会按计数器或索引重复执行多次。 */
    for (int i = 0; i < HUD_LOCK_SEG_COUNT; i++) {

        /* 这里开始判断条件 s_lock_seg[i] == NULL；只有条件成立，后面的分支代码才会执行。 */
        if (s_lock_seg[i] == NULL) {

            /* 这里把 lv_obj_create(scr) 写入 s、锁、seg、i；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
            s_lock_seg[i] = lv_obj_create(scr);

            /* 调用本项目模块接口 app_ui_style_lock_seg；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
            app_ui_style_lock_seg(s_lock_seg[i]);

            /* 调用 LVGL 图形库接口 lv_obj_set_size；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_size(s_lock_seg[i], seg_w, seg_h);

            /* 调用 LVGL 图形库接口 lv_obj_set_pos；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
            lv_obj_set_pos(s_lock_seg[i], start_x + i * (seg_w + seg_gap), y);
        /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
        }
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_auth == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_auth == NULL) {

        /* 这里把 lv_label_create(scr) 写入 s、auth；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_auth = lv_label_create(scr);

        /* 调用本项目模块接口 app_ui_style_auth_label；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_auth_label(s_auth);

        /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_label_set_text(s_auth, "AUTH PASSED");

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_auth, LV_ALIGN_CENTER, 0, -32);

        /* 调用 LVGL 图形库接口 lv_obj_add_flag；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_add_flag(s_auth, LV_OBJ_FLAG_HIDDEN);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_status == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_status == NULL) {

        /* 这里把 lv_label_create(scr) 写入 s、状态；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_status = lv_label_create(scr);

        /* 调用本项目模块接口 app_ui_style_label；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_label(s_status);

        /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_label_set_text(s_status, "dock: init");

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 8, 8);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_vision == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_vision == NULL) {

        /* 这里把 lv_label_create(scr) 写入 s、视觉；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_vision = lv_label_create(scr);

        /* 调用本项目模块接口 app_ui_style_label；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_label(s_vision);

        /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_label_set_text(s_vision, "vision: init");

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_vision, LV_ALIGN_TOP_RIGHT, -8, 8);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }




    /* 这里开始判断条件 s_dock == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_dock == NULL) {

        /* 这里把 lv_label_create(scr) 写入 s、接驳；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_dock = lv_label_create(scr);

        /* 调用本项目模块接口 app_ui_style_label；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_label(s_dock);

        /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_label_set_text(s_dock, "dock dbg: init");

        /* 调用 LVGL 图形库接口 lv_obj_set_width；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_width(s_dock, BSP_LCD_H_RES - 220);

        /* 调用 LVGL 图形库接口 lv_obj_set_style_text_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_style_text_align(s_dock, LV_TEXT_ALIGN_CENTER, 0);

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_dock, LV_ALIGN_BOTTOM_MID, 0, -8);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_hint == NULL；只有条件成立，后面的分支代码才会执行。 */
    if (s_hint == NULL) {

        /* 这里把 lv_label_create(scr) 写入 s、hint；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_hint = lv_label_create(scr);

        /* 调用本项目模块接口 app_ui_style_hint_label；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_style_hint_label(s_hint);

        /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_label_set_text(s_hint, "cloud dispatch enabled / no touch control");

        /* 调用 LVGL 图形库接口 lv_obj_align；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 46);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 s_status) lv_obj_move_foreground(s_status；只有条件成立，后面的分支代码才会执行。 */
    if (s_status) lv_obj_move_foreground(s_status);

    /* 这里开始判断条件 s_vision) lv_obj_move_foreground(s_vision；只有条件成立，后面的分支代码才会执行。 */
    if (s_vision) lv_obj_move_foreground(s_vision);

    /* 这里开始判断条件 s_coord) lv_obj_move_foreground(s_coord；只有条件成立，后面的分支代码才会执行。 */
    if (s_coord) lv_obj_move_foreground(s_coord);

    /* 这里开始判断条件 s_dock) lv_obj_move_foreground(s_dock；只有条件成立，后面的分支代码才会执行。 */
    if (s_dock) lv_obj_move_foreground(s_dock);

    /* 这里开始判断条件 s_hint) lv_obj_move_foreground(s_hint；只有条件成立，后面的分支代码才会执行。 */
    if (s_hint) lv_obj_move_foreground(s_hint);

    /* 这里开始判断条件 s_auth) lv_obj_move_foreground(s_auth；只有条件成立，后面的分支代码才会执行。 */
    if (s_auth) lv_obj_move_foreground(s_auth);


    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();

    /* 这里把 true 作为返回值交给调用者；调用当前函数的人会根据这个结果决定后续动作。 */
    return true;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_set_status；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ui_set_status(const char *text)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (text == NULL) || (s_status == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((text == NULL) || (s_status == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_label_set_text(s_status, text);

    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_set_coord；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ui_set_coord(int32_t x, int32_t y)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)x;
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    (void)y;
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_set_vision_text；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ui_set_vision_text(const char *text)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (text == NULL) || (s_vision == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((text == NULL) || (s_vision == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_label_set_text(s_vision, text);

    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_set_dock_text；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ui_set_dock_text(const char *text)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (text == NULL) || (s_dock == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((text == NULL) || (s_dock == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_label_set_text(s_dock, text);

    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_set_hint_text；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ui_set_hint_text(const char *text)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (text == NULL) || (s_hint == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((text == NULL) || (s_hint == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用 LVGL 图形库接口 lv_label_set_text；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
    lv_label_set_text(s_hint, text);

    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}






/* 这里开始定义函数 app_ui_update_hud；返回类型是 void，说明调用者执行完这段逻辑后会拿到这样一种结果。 */
void app_ui_update_hud(const app_vision_result_t *vision,
                       /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
                       const app_dock_judge_result_t *dock)
/* 从这一行开始进入上一条语句对应的代码块；后面缩进一级的内容都会属于这个作用域。 */
{

    /* 这里开始判断条件 (dock == NULL) || (s_hud_layer == NULL) || (s_track_box == NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((dock == NULL) || (s_hud_layer == NULL) || (s_track_box == NULL)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里开始判断条件 !bsp_display_lock(0)；只有条件成立，后面的分支代码才会执行。 */
    if (!bsp_display_lock(0)) {

        /* 这里直接结束当前函数，并且不返回额外数据；通常表示“后面的逻辑不必再继续执行了”。 */
        return;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 hold_box，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool hold_box = false;

    /* 这里定义变量 show_box，类型是 bool，并且在声明时就把初值设成 false；这样后面第一次使用它时就是一个确定状态。 */
    bool show_box = false;

    /* 这里定义变量 box_x，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t box_x = 0;

    /* 这里定义变量 box_y，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t box_y = 0;

    /* 这里定义变量 box_w，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t box_w = 0;

    /* 这里定义变量 box_h，类型是 int32_t，并且在声明时就把初值设成 0；这样后面第一次使用它时就是一个确定状态。 */
    int32_t box_h = 0;


    /* 这里开始判断条件 (vision != NULL) && vision->valid；只有条件成立，后面的分支代码才会执行。 */
    if ((vision != NULL) && vision->valid) {

        /* 调用本项目模块接口 app_ui_map_bbox_to_screen；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
        app_ui_map_bbox_to_screen(vision, &box_x, &box_y, &box_w, &box_h);

        /* 这里把 true 写入 s、have、last、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_have_last_box = true;

        /* 这里把 box_x 写入 s、last、box、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_last_box_x = box_x;

        /* 这里把 box_y 写入 s、last、box、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_last_box_y = box_y;

        /* 这里把 box_w 写入 s、last、box、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_last_box_w = box_w;

        /* 这里把 box_h 写入 s、last、box、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_last_box_h = box_h;

        /* 这里把 true 写入 show、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        show_box = true;
    /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
    } else if (s_have_last_box && (dock->invalid_hold_count > 0U) &&
               /* 这一行是普通 C 语句；它会直接影响当前函数内部的运行流程或数据状态。 */
               (dock->state != APP_DOCK_STATE_SEARCHING)) {
        /* 这里把 s_last_box_x 写入 box、x；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        box_x = s_last_box_x;

        /* 这里把 s_last_box_y 写入 box、y；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        box_y = s_last_box_y;

        /* 这里把 s_last_box_w 写入 box、w；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        box_w = s_last_box_w;

        /* 这里把 s_last_box_h 写入 box、h；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        box_h = s_last_box_h;

        /* 这里把 true 写入 show、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        show_box = true;

        /* 这里把 true 写入 hold、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        hold_box = true;
    /* 这里先结束前一个分支，再立刻切到 else 分支；意思就是“上面的条件不成立，那就改走另一套处理逻辑”。 */
    } else {

        /* 这里把 false 写入 s、have、last、box；这样后面读取这个变量/字段时，看到的就是当前这一步配置好的新值。 */
        s_have_last_box = false;
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 这里定义变量 box_color，类型是 lv_color_t，并且在声明时就把初值设成 app_ui_state_color(dock->state, hold_box)；这样后面第一次使用它时就是一个确定状态。 */
    lv_color_t box_color = app_ui_state_color(dock->state, hold_box);

    /* 这里定义变量 box_opa，类型是 lv_opa_t，并且在声明时就把初值设成 hold_box ? LV_OPA_50 : LV_OPA_COVER；这样后面第一次使用它时就是一个确定状态。 */
    lv_opa_t box_opa = hold_box ? LV_OPA_50 : LV_OPA_COVER;

    /* 调用本项目模块接口 app_ui_set_track_box；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_set_track_box(show_box, box_x, box_y, box_w, box_h, box_color, box_opa);


    /* 这里开始判断条件 (s_cross_h != NULL) && (s_cross_v != NULL)；只有条件成立，后面的分支代码才会执行。 */
    if ((s_cross_h != NULL) && (s_cross_v != NULL)) {

        /* 这里定义变量 cross_color，类型是 lv_color_t，并且在声明时就把初值设成 app_ui_state_color(dock->state, false)；这样后面第一次使用它时就是一个确定状态。 */
        lv_color_t cross_color = app_ui_state_color(dock->state, false);

        /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_style_bg_color(s_cross_h, cross_color, 0);

        /* 调用 LVGL 图形库接口 lv_obj_set_style_bg_color；这类函数通常直接影响界面对象、绘制刷新或输入处理。 */
        lv_obj_set_style_bg_color(s_cross_v, cross_color, 0);
    /* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
    }


    /* 调用本项目模块接口 app_ui_update_lock_bar；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_update_lock_bar(dock);

    /* 调用本项目模块接口 app_ui_update_auth_banner；这个名字前缀是 app_，说明它不是 ESP-IDF 原生 API，而是你工程自己封装的一层业务接口。 */
    app_ui_update_auth_banner(dock->state);


    /* 释放显示锁；和前面的 lock 配对，避免长期占住界面资源。 */
    bsp_display_unlock();
/* 这里结束当前代码块；离开这一层作用域后，前面在块内定义的局部变量也会随之失效。 */
}
