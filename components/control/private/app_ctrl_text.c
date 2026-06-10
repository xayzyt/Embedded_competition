#include "app_ctrl_text.h"
#include <stdio.h>
#include "app_drone_ai.h"

// 控制/UI 文案组装集中在这里，避免控制状态机里混入大量 snprintf。

// 生成面向调试的完整遥测行。
// vision 无效但判定器仍处于 hold 状态时，继续显示上一帧偏差并标注丢失计数；
// 完全搜索状态则只显示等待提示，避免把无效数值误认为实时测量。
void app_ctrl_compose_detail(const app_dock_judge_result_t *dock,
    bool has_weight,
    int32_t weight_g,
    app_ch32_proto_stage_t proto_stage,
    char *buf,
    size_t buf_len)
{
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U))
    {
        return;
    }
    // 按对接判定的门控顺序给出下一步操作提示。
    if (!dock->vision_valid)
    {
        if (dock->state != APP_DOCK_STATE_SEARCHING)
        {
            snprintf(buf,
                buf_len,
                "dock dbg: hold:%u lost:%u dx:%ld dy:%ld z:%ldmm e:%.1f stage:%s",
                (unsigned)dock->invalid_hold_count,
                (unsigned)dock->lost_count,
                (long)dock->dx,
                (long)dock->dy,
                (long)dock->est_distance_mm,
                (double)dock->filtered_edge_px,
                app_ch32_link_proto_stage_name(proto_stage));
        }
        else
        {
            snprintf(buf, buf_len, "dock dbg: wait valid tag");
        }
        return;
    }
    snprintf(buf,
        buf_len,
        "dock dbg: id:%u c:%ld,%ld b:%ldx%ld dx:%ld dy:%ld z:%ldmm e:%.1f ang:%d st:%u score:%u wt:%s%ldg",
        (unsigned)dock->tag_id,
        (long)dock->filtered_center_x,
        (long)dock->filtered_center_y,
        (long)dock->bbox_w,
        (long)dock->bbox_h,
        (long)dock->dx,
        (long)dock->dy,
        (long)dock->est_distance_mm,
        (double)dock->filtered_edge_px,
        (int)dock->angle_deg,
        (unsigned)dock->stable_count,
        (unsigned)dock->hover_score,
        has_weight ? "" : "-",
        has_weight ? (long)weight_g : 0L);
}

// 按门控顺序只报告第一个未通过条件。
// 该顺序与 app_dock_judge_process() 的判定顺序一致，现场人员可以直接按提示调整。
static void app_ctrl_compose_guidance(const app_dock_judge_result_t *dock,
    char *buf,
    size_t buf_len)
{
    if ((dock == NULL) || (buf == NULL) || (buf_len == 0U))
    {
        return;
    }
    if (!dock->vision_valid)
    {
        // 没有稳定视觉结果时，先提示搜索目标，不继续检查后续门控。
        snprintf(buf, buf_len, "dock: searching target");
        return;
    }
    if (!dock->target_id_ok)
    {
        snprintf(buf, buf_len, "dock: wrong tag id");
        return;
    }
    if (!dock->centered_ok)
    {
        snprintf(buf, buf_len, "dock: align target center");
        return;
    }
    if (!dock->near_ok)
    {
        snprintf(buf, buf_len, "dock: move target closer");
        return;
    }
    if (!dock->stable_ok)
    {
        snprintf(buf, buf_len, "dock: hold hover stable");
        return;
    }
    if (!dock->distance_ok)
    {
        // 距离估计有效时给出远近方向，否则提示等待有效距离。
        if (dock->est_distance_mm > 0)
        {
            snprintf(buf,
                buf_len,
                (dock->est_distance_mm < 260) ? "dock: target too near" : "dock: target too far");
        }
        else
        {
            snprintf(buf, buf_len, "dock: wait valid distance");
        }
        return;
    }
    app_dock_judge_format_status(dock, buf, buf_len);
}

// 将高层任务状态转换为主状态栏文案。
// WAIT_APPROACH 阶段根据 apriltag_enabled 在 AI 确认提示与对接引导之间切换。
void app_ctrl_compose_task_status(const app_task_snapshot_t *task,
    const app_dock_judge_result_t *dock,
    bool ch32_ready,
    bool apriltag_enabled,
    char *buf,
    size_t buf_len)
{
    if (buf == NULL || buf_len == 0U || task == NULL)
    {
        return;
    }
    switch (task->state) {
    case APP_TASK_STATE_CONFIGURED:
        snprintf(buf,
            buf_len,
            ch32_ready ? "task: target=%u / remote ready" : "task: target=%u / wait CH32",
            (unsigned)task->target_id);
        break;
    case APP_TASK_STATE_WAIT_APPROACH: {
            char guide[72] = {0};
            if (apriltag_enabled)
            {
                app_ctrl_compose_guidance(dock, guide, sizeof(guide));
            }
            else
            {
                app_drone_ai_format_status(guide, sizeof(guide));
            }
            snprintf(buf, buf_len, "task: wait id=%u / %s", (unsigned)task->target_id, guide);
            break;
        }
    case APP_TASK_STATE_AUTH_PASSED:
        snprintf(buf,
            buf_len,
            "task: auth passed / matched id=%u",
            (unsigned)(task->matched_tag_id != 0U ? task->matched_tag_id : task->target_id));
        break;
    case APP_TASK_STATE_DOCKING:
        snprintf(buf, buf_len, "task: docking in progress");
        break;
    case APP_TASK_STATE_COMPLETED:
        snprintf(buf,
            buf_len,
            "task: completed / target=%u",
            (unsigned)task->target_id);
        break;
    case APP_TASK_STATE_FAULT:
        snprintf(buf,
            buf_len,
            "task: fault / %s",
            task->note[0] != '\0' ? task->note : "check CH32");
        break;
    case APP_TASK_STATE_CANCELLED:
        snprintf(buf, buf_len, "task: cancelled / target=%u", (unsigned)task->target_id);
        break;
    case APP_TASK_STATE_IDLE:
    default:
        snprintf(buf, buf_len, "task: idle");
        break;
    }
}
