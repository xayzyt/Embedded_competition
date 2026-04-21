function Format-ModuleComment([string[]]$lines) {
    if ($lines.Count -eq 0) { return '' }
    $text = "/* $($lines[0])`r`n"
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $text += " * $($lines[$i])`r`n"
    }
    $text += " */`r`n`r`n"
    return $text
}

function Format-FunctionComment([string]$line) {
    return "/* $line */`r`n"
}

function Get-ModuleCommentLines([string]$fileName) {
    switch ($fileName) {
        'app_apriltag.c' { return @('AprilTag 检测模块。', '负责把灰度输入阈值化、提取四边形候选，并解码 tag36h11 标签。') }
        'app_camera.c' { return @('相机预览与帧分发模块。', '负责管理采集缓冲区、预览画布、显示任务，并把可用帧提交给视觉链路。') }
        'app_ch32_link.c' { return @('ESP32 与 CH32 的串口链路模块。', '同时兼容旧文本协议和当前二进制协议，并维护 ready、ACK、重量等链路状态。') }
        'app_cloud.c' { return @('云端接入模块。', '负责 Wi-Fi、MQTT、命令解析，以及将任务快照同步到云端。') }
        'app_ctrl.c' { return @('高层控制模块。', '把触摸输入、视觉结果、CH32 状态和任务状态机组合成最终控制决策。') }
        'app_dock_judge.c' { return @('靠泊判定模块。', '对视觉检测结果做滤波、打分和阈值判断，给上层提供是否允许触发动作的结论。') }
        'app_task.c' { return @('任务状态机模块。', '负责维护目标 ID、订单流程、阶段切换以及对外事件通知。') }
        'app_ui.c' { return @('界面渲染模块。', '负责创建 LVGL HUD，并把视觉与任务信息转换成用户可读的界面元素。') }
        'app_video.c' { return @('底层视频采集模块。', '封装 V4L2 设备打开、缓冲区管理、取帧回帧和后台流任务。') }
        'app_vision.c' { return @('视觉任务模块。', '负责把预览帧转成灰度图、运行 AprilTag 检测，并缓存最新识别结果。') }
        'bsp_display_port.c' { return @('显示板级适配模块。', '把 BSP 提供的显示和触摸能力封装成应用层可直接调用的简化接口。') }
        'main.c' { return @('系统启动入口。', '负责初始化 NVS、显示、UI、视觉、靠泊判定、控制任务和云端接入。') }
        default { return @('模块实现文件。') }
    }
}

function Get-FunctionComment([string]$fileName, [string]$name) {
    switch ($fileName) {
        'app_apriltag.c' {
            switch -Regex ($name) {
                '^at_dbg_fail_stage_str$' { return '返回调试阶段枚举对应的文本描述。' }
                '^at_dbg_log_cells$' { return '输出候选标签各采样单元的调试信息。' }
                '^at_dbg_log_bits$' { return '输出候选标签位图和旋转后的调试结果。' }
                '^at_dbg_log_info$' { return '输出一次候选标签解码过程的综合调试信息。' }
                '^at_index$' { return '将二维坐标转换为工作缓冲区的一维索引。' }
                '^at_otsu_threshold$' { return '为当前灰度图计算 Otsu 二值化阈值。' }
                '^at_hamming64$' { return '计算两个 64 位码字之间的汉明距离。' }
                '^at_threshold_binary$' { return '将灰度输入阈值化到二值工作缓冲区。' }
                '^at_filter_component$' { return '按面积和边长等条件过滤不合格的连通域。' }
                '^at_candidate_touch_edge$' { return '判断候选框是否贴近图像边缘。' }
                '^at_candidate_too_large$' { return '判断候选框是否大到不合理。' }
                '^at_insert_candidate$' { return '把通过筛选的候选框插入有界候选列表。' }
                '^at_collect_candidates$' { return '扫描二值图并收集可能的标签候选。' }
                '^at_dist2$' { return '返回两点之间的平方距离。' }
                '^at_validate_quad$' { return '校验四边形顶点顺序和形状是否合法。' }
                '^at_solve_homography$' { return '求解标签网格到图像四边形的单应矩阵。' }
                '^at_project$' { return '利用单应矩阵把归一化点投影回图像坐标。' }
                '^at_sample_bilinear$' { return '对灰度图执行双线性采样。' }
                '^at_sample_cell$' { return '采样标签网格中的单个逻辑单元。' }
                '^at_rotate_bits_cw$' { return '将采样到的位图顺时针旋转 90 度。' }
                '^at_transform_name$' { return '返回位图变换模式对应的调试名称。' }
                '^at_pack_mode_name$' { return '返回码字打包模式对应的调试名称。' }
                '^at_transform_bits$' { return '按照目标朝向变换采样到的位图。' }
                '^at_build_code_family$' { return '按标签家族定义整理需要参与匹配的数据位。' }
                '^at_build_code_row_major$' { return '按行主序打包标签位图。' }
                '^at_build_code_col_major$' { return '按列主序打包标签位图。' }
                '^at_build_code_variant$' { return '生成用于匹配的不同位图排列版本。' }
                '^at_match_code$' { return '将候选码字与 tag36h11 码本比较并选出最佳匹配。' }
                '^at_decode_with_quad$' { return '对单个有效四边形执行完整的标签解码。' }
                '^app_apriltag_init$' { return '初始化 AprilTag 检测所需的长期工作缓冲区。' }
                '^app_apriltag_detect_tag36h11$' { return '在输入灰度图上运行完整的 tag36h11 检测流程。' }
                default { return 'AprilTag 检测链路使用的内部辅助函数。' }
            }
        }
        'app_camera.c' {
            switch -Regex ($name) {
                '^app_camera_msync_aligned$' { return '按缓存行对齐地址范围后执行缓存同步。' }
                '^app_camera_msync_m2c$' { return '在硬件读取前把内存修改同步到缓存一致状态。' }
                '^app_camera_msync_c2m$' { return '在软件读取前失效缓存，确保看到 DMA 写入的新数据。' }
                '^app_get_active_screen$' { return '返回当前可用于挂载预览控件的活动屏幕。' }
                '^app_camera_free_camera_buffers$' { return '释放相机采集链路使用的缓冲区。' }
                '^app_camera_free_display_buffers$' { return '释放显示和中转阶段使用的缓冲区。' }
                '^app_camera_alloc_display_buffers$' { return '分配显示任务和预览画布使用的缓冲区。' }
                '^app_camera_alloc_userptr_buffers$' { return '为 V4L2 USERPTR 模式分配采集缓冲区。' }
                '^app_camera_create_canvas$' { return '创建显示相机预览的 LVGL 画布。' }
                '^app_ppa_init$' { return '初始化像素处理加速器。' }
                '^calc_aspect_fit$' { return '计算保持宽高比的目标显示区域。' }
                '^app_image_process_scale_crop$' { return '将采集帧缩放裁剪到显示或采样目标缓冲区。' }
                '^app_camera_has_pending_stage$' { return '判断是否还有待显示的中转帧。' }
                '^app_camera_pick_writable_stage_buffer$' { return '挑选一个可写的中转缓冲区。' }
                '^app_camera_publish_stage_buffer$' { return '把已写好的中转缓冲区标记为可显示。' }
                '^app_camera_abandon_stage_buffer$' { return '在处理失败时放弃当前中转缓冲区。' }
                '^app_camera_take_ready_stage_buffer$' { return '取出一个已准备好的中转缓冲区供显示任务消费。' }
                '^app_camera_release_stage_buffer$' { return '在显示完成后回收一个中转缓冲区。' }
                '^app_camera_display_task$' { return '后台显示任务，负责把预处理后的帧刷到 UI。' }
                '^app_camera_frame_cb$' { return '视频帧回调，负责预览处理和视觉输入分发。' }
                '^app_camera_start_display_task$' { return '启动负责刷新相机预览的后台任务。' }
                '^app_camera_init$' { return '初始化相机预览模块的长期资源和显示对象。' }
                '^app_camera_preview_start$' { return '打开视频流并启动预览链路。' }
                '^app_camera_preview_stop$' { return '停止预览链路并释放当前采集资源。' }
                '^app_camera_is_preview_running$' { return '返回相机预览链路当前是否在运行。' }
                default { return '相机预览链路使用的内部辅助函数。' }
            }
        }
        'app_ch32_link.c' {
            switch -Regex ($name) {
                '^app_ch32_crc16_ibm$' { return '计算 CH32 二进制协议使用的 CRC16/IBM 校验值。' }
                '^app_ch32_read_i32_le$' { return '按小端序读取 32 位有符号整数。' }
                '^app_ch32_read_u16_le$' { return '按小端序读取 16 位无符号整数。' }
                '^app_ch32_legacy_cmd_to_proto$' { return '把旧文本命令映射到二进制协议命令枚举。' }
                '^app_ch32_proto_cmd_to_legacy$' { return '把二进制协议命令映射回旧文本命令。' }
                '^app_ch32_link_proto_stage_name$' { return '返回协议阶段枚举对应的文本名称。' }
                '^app_ch32_link_proto_error_name$' { return '返回协议错误码对应的文本名称。' }
                '^app_ch32_classify_legacy_line$' { return '识别收到的文本行属于 ACK、状态还是错误信息。' }
                '^app_ch32_proto_stage_indicates_ready$' { return '根据协议阶段判断 CH32 当前是否处于可触发状态。' }
                '^app_ch32_apply_common_side_effects$' { return '把 ready、重量和在线状态等公共副作用写回链路上下文。' }
                '^app_ch32_dispatch_msg$' { return '把解析好的协议消息分发给上层回调。' }
                '^app_ch32_dispatch_legacy_line$' { return '处理一条旧文本协议行并更新链路状态。' }
                '^app_ch32_parse_proto_frame$' { return '从串口接收缓冲区中解析一帧二进制协议数据。' }
                '^app_ch32_link_rx_task$' { return '串口接收任务，持续读取并解析 CH32 消息。' }
                '^app_ch32_link_prepare_tx_idle$' { return '在发命令前等待发送路径进入空闲状态。' }
                '^app_ch32_link_send_legacy_cmd$' { return '按旧文本协议发送单字符命令。' }
                '^app_ch32_link_send_proto$' { return '按二进制协议组帧并发送命令。' }
                '^app_ch32_link_wait_ack_for_cmd$' { return '等待目标命令对应的 ACK 到达。' }
                '^app_ch32_link_init$' { return '初始化串口、事件组和接收任务。' }
                '^app_ch32_link_deinit$' { return '释放串口链路占用的资源。' }
                '^app_ch32_link_send_cmd$' { return '发送一次命令但不主动等待 ACK。' }
                '^app_ch32_link_send_cmd_and_wait_ack$' { return '发送命令并同步等待 ACK 结果。' }
                '^app_ch32_link_probe_ready$' { return '主动探测 CH32 是否进入 ready 状态。' }
                '^app_ch32_link_wait_ready$' { return '阻塞等待链路变为 ready。' }
                '^app_ch32_link_is_ready$' { return '返回链路当前记录的 ready 状态。' }
                '^app_ch32_link_last_weight$' { return '返回最近一次上报的重量值。' }
                default { return 'CH32 串口链路使用的内部辅助函数。' }
            }
        }
        'app_cloud.c' {
            switch -Regex ($name) {
                '^app_cloud_json_get_string$' { return '从简单 JSON 字符串中提取指定键的字符串值。' }
                '^app_cloud_json_get_u16$' { return '从简单 JSON 字符串中提取指定键的 16 位整数值。' }
                '^app_cloud_parse_command_json$' { return '把云端下发的 JSON 负载解析成内部命令结构。' }
                '^app_cloud_init_netif_once$' { return '按需初始化网络接口。' }
                '^app_cloud_init_event_loop_once$' { return '按需初始化系统事件循环。' }
                '^app_cloud_build_topics$' { return '拼出当前设备使用的 MQTT 主题字符串。' }
                '^app_cloud_log_topics_once$' { return '在启动阶段输出一次当前主题配置。' }
                '^app_cloud_validate_config$' { return '检查 Wi-Fi 和 MQTT 配置是否完整可用。' }
                '^app_cloud_publish_raw$' { return '向指定 MQTT 主题直接发布原始负载。' }
                '^app_cloud_publish_ack$' { return '向云端发布一次命令 ACK。' }
                '^app_cloud_publish_task_snapshot_internal$' { return '把任务快照编码后发布到状态主题。' }
                '^app_cloud_on_task_event$' { return '响应任务状态变化并触发云端同步。' }
                '^app_cloud_create_mqtt_client$' { return '创建并配置 MQTT 客户端实例。' }
                '^app_cloud_destroy_mqtt_client$' { return '销毁当前 MQTT 客户端实例。' }
                '^app_cloud_start_mqtt_if_needed$' { return '在网络就绪后按需启动 MQTT 连接。' }
                '^app_cloud_receive_set_target$' { return '处理云端下发的目标 ID 更新命令。' }
                '^app_cloud_receive_start_task$' { return '处理云端下发的开始任务命令。' }
                '^app_cloud_receive_cancel$' { return '处理云端下发的取消任务命令。' }
                '^app_cloud_handle_command$' { return '分派一条已经解析完成的云端命令。' }
                '^app_cloud_handle_mqtt_data_event$' { return '处理 MQTT 数据事件中的业务负载。' }
                '^app_cloud_task$' { return '云端服务后台任务，负责等待网络并驱动 MQTT 生命周期。' }
                '^app_cloud_wifi_event_handler$' { return '处理 Wi-Fi 连接状态变化事件。' }
                '^app_cloud_mqtt_event_handler$' { return '处理 MQTT 连接、订阅和收包事件。' }
                '^app_cloud_init$' { return '初始化云端接入模块并注册相关回调。' }
                '^app_cloud_publish_task_snapshot$' { return '对外发布当前任务快照。' }
                default { return '云端接入链路使用的内部辅助函数。' }
            }
        }
        'app_ctrl.c' {
            switch -Regex ($name) {
                '^app_ctrl_now_ms$' { return '返回控制模块使用的毫秒时间基准。' }
                '^app_ctrl_deadline_active$' { return '判断指定的截止时间是否仍然有效。' }
                '^app_ctrl_set_notice_locked$' { return '在已持锁状态下更新界面提示文本。' }
                '^app_ctrl_set_notice$' { return '安全地更新控制层提示文本。' }
                '^app_ctrl_start_retrigger_cooldown_locked$' { return '在已持锁状态下启动重新触发冷却窗口。' }
                '^app_ctrl_line_has_done_keyword$' { return '判断 CH32 文本消息里是否包含完成关键字。' }
                '^app_ctrl_proto_stage_is_busy$' { return '根据协议阶段判断 CH32 当前是否忙碌。' }
                '^app_ctrl_proto_stage_status_text$' { return '返回协议阶段对应的用户状态文案。' }
                '^app_ctrl_compose_detail$' { return '拼装当前控制状态的详细说明文本。' }
                '^app_ctrl_compose_guidance$' { return '拼装当前界面需要展示的引导文案。' }
                '^app_ctrl_compose_task_status$' { return '拼装当前任务状态摘要文本。' }
                '^app_ctrl_apply_proto_msg_locked$' { return '在已持锁状态下应用一条 CH32 协议消息。' }
                '^app_ctrl_on_ch32_line$' { return '处理来自 CH32 链路的上行消息。' }
                '^app_ctrl_handle_touch$' { return '处理触摸输入触发的本地控制行为。' }
                '^app_ctrl_task$' { return '控制后台任务，持续协调视觉、任务和 CH32 状态。' }
                '^app_ctrl_init$' { return '初始化控制模块的内部状态。' }
                '^app_ctrl_start$' { return '启动控制后台任务。' }
                default { return '控制模块使用的内部辅助函数。' }
            }
        }
        'app_dock_judge.c' {
            switch -Regex ($name) {
                '^app_abs_i32$' { return '返回 32 位有符号整数的绝对值。' }
                '^app_sat_inc_u8$' { return '对 8 位计数器执行饱和加一。' }
                '^app_filter_ema_i32$' { return '对整数信号执行指数滑动平均滤波。' }
                '^app_filter_ema_f32$' { return '对浮点信号执行指数滑动平均滤波。' }
                '^app_clip_i32$' { return '把整数值限制在给定区间内。' }
                '^app_dock_apply_filter$' { return '对当前检测结果应用平滑滤波。' }
                '^app_dock_estimate_distance_mm$' { return '根据标签尺寸和焦距估算目标距离。' }
                '^app_dock_calc_hover_score$' { return '根据位置、尺度和稳定性计算靠泊评分。' }
                '^app_dock_fill_result_base$' { return '填充判定结果中的公共字段。' }
                '^app_dock_judge_get_default_config$' { return '返回默认的靠泊判定参数。' }
                '^app_dock_judge_init$' { return '初始化靠泊判定模块。' }
                '^app_dock_judge_get_config$' { return '读取当前的靠泊判定配置。' }
                '^app_dock_judge_set_target_id$' { return '更新当前允许识别的目标标签 ID。' }
                '^app_dock_judge_reset$' { return '重置滤波和稳定性统计状态。' }
                '^app_dock_judge_process$' { return '处理一帧视觉结果并输出靠泊判定。' }
                '^app_dock_judge_state_to_text$' { return '返回判定状态枚举对应的文本名称。' }
                '^app_dock_judge_format_status$' { return '把判定结果格式化成简短状态字符串。' }
                '^app_dock_judge_format_detail$' { return '把判定结果格式化成详细诊断字符串。' }
                default { return '靠泊判定模块使用的内部辅助函数。' }
            }
        }
        'app_task.c' {
            switch -Regex ($name) {
                '^app_task_now_ms$' { return '返回任务状态机使用的毫秒时间基准。' }
                '^app_task_change_state_locked$' { return '在已持锁状态下切换任务状态。' }
                '^app_task_emit_event$' { return '向已注册监听者广播一次任务事件。' }
                '^app_task_persist_target_id$' { return '把当前目标 ID 持久化到存储。' }
                '^app_task_load_target_id$' { return '从存储中加载上次保存的目标 ID。' }
                '^app_task_register_event_callback$' { return '注册任务事件回调。' }
                '^app_task_init$' { return '初始化任务状态机并恢复持久化配置。' }
                '^app_task_get_target_id$' { return '读取当前任务使用的目标 ID。' }
                '^app_task_set_target_id$' { return '更新任务使用的目标 ID。' }
                '^app_task_start_with_target$' { return '以指定目标 ID 启动一条新任务。' }
                '^app_task_start_local$' { return '使用当前配置在本地启动任务。' }
                '^app_task_submit_remote_request$' { return '记录一次来自远端的请求上下文。' }
                '^app_task_mark_auth_passed$' { return '把任务推进到认证通过阶段。' }
                '^app_task_mark_docking_started$' { return '把任务推进到已开始靠泊阶段。' }
                '^app_task_mark_completed$' { return '把任务推进到完成阶段。' }
                '^app_task_mark_fault$' { return '把任务推进到故障阶段。' }
                '^app_task_cancel$' { return '取消当前任务。' }
                '^app_task_reset_idle$' { return '把任务状态恢复到空闲。' }
                '^app_task_get_snapshot$' { return '读取当前任务快照。' }
                '^app_task_state_to_text$' { return '返回任务状态枚举对应的文本名称。' }
                '^app_task_format_brief$' { return '把任务快照格式化为简短摘要。' }
                default { return '任务状态机使用的内部辅助函数。' }
            }
        }
        'app_ui.c' {
            switch -Regex ($name) {
                '^app_get_active_screen$' { return '返回当前用于挂载 HUD 的活动屏幕对象。' }
                '^app_ui_style_' { return '为指定 HUD 控件应用统一样式。' }
                '^app_ui_state_color$' { return '根据当前锁定状态返回对应的界面颜色。' }
                '^app_ui_calc_fit_dims$' { return '计算图像按比例适配到显示区域后的尺寸。' }
                '^app_ui_get_rotated_dims$' { return '计算旋转后图像在屏幕上的宽高。' }
                '^app_ui_transform_src_point$' { return '把源图像坐标变换到当前显示方向下。' }
                '^app_ui_clamp_i32$' { return '把整数值限制在给定范围内。' }
                '^app_ui_map_bbox_to_screen$' { return '把检测框从源图像坐标映射到屏幕坐标。' }
                '^app_ui_update_lock_bar$' { return '更新锁定进度条的显示效果。' }
                '^app_ui_update_auth_banner$' { return '更新界面顶部的认证状态横幅。' }
                '^app_ui_set_track_box$' { return '更新目标跟踪框的位置和可见性。' }
                '^app_ui_create$' { return '创建主界面和全部 HUD 控件。' }
                '^app_ui_set_status$' { return '更新主状态文本。' }
                '^app_ui_set_coord$' { return '更新坐标或位置相关显示文本。' }
                '^app_ui_set_vision_text$' { return '更新视觉调试文本。' }
                '^app_ui_set_dock_text$' { return '更新靠泊调试文本。' }
                '^app_ui_set_hint_text$' { return '更新操作提示文本。' }
                '^app_ui_update_hud$' { return '根据最新视觉结果刷新 HUD 叠加层。' }
                default { return '界面模块使用的内部辅助函数。' }
            }
        }
        'app_video.c' {
            switch -Regex ($name) {
                '^app_video_main$' { return '预留的视频模块入口，当前不执行额外板级初始化。' }
                '^fourcc_to_str$' { return '把 FOURCC 像素格式编码转换为可打印字符串。' }
                '^app_video_open$' { return '打开视频设备并读取基础能力与格式信息。' }
                '^app_video_set_bufs$' { return '向视频设备申请并配置采集缓冲区。' }
                '^app_video_get_bufs$' { return '把可用缓冲区重新入队给视频驱动。' }
                '^app_video_get_buf_size$' { return '返回当前采集缓冲区的字节大小。' }
                '^video_receive_video_frame$' { return '从视频驱动取出一帧已采集的数据。' }
                '^video_operation_video_frame$' { return '把取出的帧交给已注册回调处理。' }
                '^video_free_video_frame$' { return '把处理完成的帧重新归还给驱动。' }
                '^video_stream_start$' { return '启动底层视频流。' }
                '^video_stream_stop$' { return '停止底层视频流。' }
                '^video_stream_task$' { return '后台视频任务，持续执行取帧、回调和回帧。' }
                '^app_video_stream_task_start$' { return '启动后台视频流任务。' }
                '^app_video_stream_task_stop$' { return '停止后台视频流任务。' }
                '^app_video_register_frame_operation_cb$' { return '注册视频帧处理回调。' }
                '^app_video_stream_wait_stop$' { return '等待视频流任务完全退出。' }
                default { return '视频采集模块使用的内部辅助函数。' }
            }
        }
        'app_vision.c' {
            switch -Regex ($name) {
                '^app_vision_now_ms$' { return '返回视觉模块使用的毫秒时间基准。' }
                '^app_rgb565_to_gray$' { return '把单个 RGB565 像素转换为灰度值。' }
                '^app_vision_snapshot$' { return '抓取一份线程安全的最新输入快照。' }
                '^app_vision_store_result$' { return '缓存一份最新的视觉识别结果。' }
                '^app_vision_get_latest_result$' { return '读取最近一次识别结果。' }
                '^app_vision_set_wait_text$' { return '在等待有效输入时更新界面提示。' }
                '^app_vision_update_result$' { return '把本轮识别结果同步到缓存和界面。' }
                '^app_vision_task$' { return '视觉后台任务，持续拉取灰度帧并执行 AprilTag 检测。' }
                '^app_vision_init$' { return '初始化视觉模块的工作缓冲区和任务状态。' }
                '^app_vision_start$' { return '启动视觉后台任务。' }
                '^app_vision_submit_frame$' { return '提交一帧新的灰度输入给视觉任务。' }
                default { return '视觉模块使用的内部辅助函数。' }
            }
        }
        'bsp_display_port.c' {
            switch -Regex ($name) {
                '^app_display_init$' { return '初始化板级显示、背光和可选触摸输入。' }
                '^app_display_lock$' { return '在访问 LVGL 对象前获取显示锁。' }
                '^app_display_unlock$' { return '在完成 LVGL 操作后释放显示锁。' }
                '^app_display_touch_read$' { return '读取当前触摸点，若未按下则返回 false。' }
                default { return '显示板级适配使用的内部辅助函数。' }
            }
        }
        'main.c' {
            switch -Regex ($name) {
                '^app_init_nvs$' { return '初始化 NVS，并在必要时执行擦除恢复。' }
                '^app_main$' { return '按系统启动顺序初始化各模块并进入主业务链路。' }
                default { return '系统启动阶段使用的内部辅助函数。' }
            }
        }
        default {
            return '模块内部辅助函数。'
        }
    }
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$files = Get-ChildItem 'c:\beifen\ESP32_P4_EV\test\main' -Filter *.c
$pattern = '(?ms)^(?<sig>\s*(?:static\s+)?(?:inline\s+)?(?:const\s+)?[A-Za-z_][A-Za-z0-9_\s]*?\s+\*?\s*(?<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*?\)\s*\{)'

foreach ($file in $files) {
    $text = Get-Content $file.FullName -Raw
    $spdx = ''
    if ($text -match '^(?s)(/\*.*?SPDX.*?\*/\s*)') {
        $spdx = $Matches[1]
        $text = $text.Substring($spdx.Length)
    }

    $text = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    $text = [regex]::Replace($text, '[ \t]+(\r?\n)', '$1')
    $text = [regex]::Replace($text, '(\r?\n){3,}', "`r`n`r`n")
    $text = $text.TrimStart()

    $text = [regex]::Replace($text, $pattern, {
        param($m)
        $name = $m.Groups['name'].Value
        $comment = Get-FunctionComment $file.Name $name
        (Format-FunctionComment $comment) + $m.Groups['sig'].Value
    })

    $module = Format-ModuleComment (Get-ModuleCommentLines $file.Name)
    $finalText = $spdx + $module + $text.TrimStart()
    [System.IO.File]::WriteAllText($file.FullName, $finalText, $utf8NoBom)
}
