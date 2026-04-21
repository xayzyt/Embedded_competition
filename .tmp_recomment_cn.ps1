function Format-BlockComment {
    param([string[]]$Lines)
    if ($null -eq $Lines -or $Lines.Count -eq 0) {
        return ''
    }
    if ($Lines.Count -eq 1) {
        return "/* $($Lines[0]) */"
    }
    $body = @("/* $($Lines[0])")
    for ($i = 1; $i -lt $Lines.Count; $i++) {
        $body += " * $($Lines[$i])"
    }
    $body += ' */'
    return ($body -join "`r`n")
}

function Set-TopComment {
    param([string]$Content, [string[]]$Lines)
    $comment = (Format-BlockComment $Lines) + "`r`n`r`n"
    $pattern = '(?s)\A/\*.*?\*/\s*'
    if ([regex]::IsMatch($Content, $pattern)) {
        return [regex]::Replace($Content, $pattern, $comment, 1)
    }
    return $comment + $Content.TrimStart()
}

function Set-FunctionComment {
    param([string]$Content, [string]$FunctionName, [string]$CommentText)
    $escaped = [regex]::Escape($FunctionName)
    $comment = (Format-BlockComment @($CommentText)) + "`r`n"
    $pattern = "(?s)/\*.*?\*/\s*(?=(?:static\s+)?(?:inline\s+)?(?:const\s+)?[^\n;]*\b$escaped\s*\()"
    if ([regex]::IsMatch($Content, $pattern)) {
        return [regex]::Replace($Content, $pattern, $comment, 1)
    }
    $insertPattern = "(?m)^(?<sig>(?:static\s+)?(?:inline\s+)?(?:const\s+)?[^\n;]*\b$escaped\s*\([^;]*\)\s*)$"
    if ([regex]::IsMatch($Content, $insertPattern)) {
        return [regex]::Replace($Content, $insertPattern, "$comment`$1", 1)
    }
    Write-Warning "comment target not found: $FunctionName"
    return $Content
}

$files = @(
    @{
        Path = 'main\\bsp_display_port.c'
        Header = @('显示板级适配模块。', '把 BSP 提供的显示与触摸能力封装成应用层可直接调用的统一接口。')
        Functions = [ordered]@{
            'app_display_init' = '初始化显示、背光和触摸输入，并缓存后续要复用的显示句柄。'
            'app_display_lock' = '进入 LVGL 显示临界区，保证跨任务刷新界面时的线程安全。'
            'app_display_unlock' = '退出 LVGL 显示临界区，让其他任务继续访问显示资源。'
            'app_display_touch_read' = '读取当前触摸点坐标，并转换成上层使用的按下状态。'
        }
    },
    @{
        Path = 'main\\main.c'
        Header = @('系统启动入口。', '负责按顺序初始化 NVS、显示、视觉、控制、任务和云端模块。')
        Functions = [ordered]@{
            'app_init_nvs' = '初始化 NVS；如果发现分页损坏或版本不兼容，就先擦除再重建。'
            'app_main' = '按系统启动顺序拉起各业务模块，并进入整机主流程。'
        }
    },
    @{
        Path = 'main\\app_task.c'
        Header = @('任务上下文模块。', '负责管理目标 ID、任务状态、事件回调以及持久化配置。')
        Functions = [ordered]@{
            'app_task_now_ms' = '获取当前毫秒时间戳，供状态切换和超时判断复用。'
            'app_task_change_state_locked' = '在持锁条件下切换任务状态，并同步更新时间戳和提示信息。'
            'app_task_emit_event' = '把任务状态变化广播给已注册的事件回调。'
            'app_task_persist_target_id' = '把当前目标标签 ID 写入 NVS，便于下次上电恢复。'
            'app_task_load_target_id' = '从 NVS 读取上次保存的目标标签 ID。'
            'app_task_register_event_callback' = '注册任务事件回调，让 UI 或云端模块能跟踪状态变化。'
            'app_task_init' = '初始化任务上下文，并恢复上次保存的目标标签配置。'
            'app_task_get_target_id' = '返回当前任务使用的目标标签 ID。'
            'app_task_set_target_id' = '更新目标标签 ID，并立即持久化到 NVS。'
            'app_task_start_with_target' = '使用指定目标标签创建一条新任务，并把状态切到进行中。'
            'app_task_start_local' = '按当前目标标签直接发起本地任务。'
            'app_task_submit_remote_request' = '记录一次云端下发请求，供后续状态上报和关联。'
            'app_task_mark_auth_passed' = '标记已经通过目标识别/授权阶段，并记录匹配到的标签。'
            'app_task_mark_docking_started' = '标记任务已经进入靠泊动作执行阶段。'
            'app_task_mark_completed' = '把任务标记为完成，并记录最终说明信息。'
            'app_task_mark_fault' = '把任务标记为失败，并记录失败原因。'
            'app_task_cancel' = '取消当前任务，并向外部广播取消事件。'
            'app_task_reset_idle' = '把任务状态恢复为空闲，清理本轮任务遗留信息。'
            'app_task_get_snapshot' = '导出当前任务快照，供 UI 和云端统一读取。'
            'app_task_format_brief' = '把当前任务状态整理成适合展示的简短文本。'
        }
    },
    @{
        Path = 'main\\app_dock_judge.c'
        Header = @('靠泊判定模块。', '负责过滤视觉结果、估算距离，并判断是否达到允许执行动作的停靠姿态。')
        Functions = [ordered]@{
            'app_abs_i32' = '计算 32 位整数的绝对值。'
            'app_sat_inc_u8' = '对 8 位计数器做饱和自增，避免继续上溢。'
            'app_filter_ema_i32' = '对整数信号执行指数滑动平均，减少瞬时抖动。'
            'app_filter_ema_f32' = '对浮点信号执行指数滑动平均，平滑连续量变化。'
            'app_clip_i32' = '把整数裁剪到指定上下限之间。'
            'app_dock_apply_filter' = '把一帧视觉检测结果更新进内部滤波器状态。'
            'app_dock_estimate_distance_mm' = '根据标签在画面中的尺寸估算相机到目标的距离。'
            'app_dock_calc_hover_score' = '计算当前姿态与理想投递悬停位置的接近程度。'
            'app_dock_fill_result_base' = '填充判定结果里各状态共用的基础字段。'
            'app_dock_judge_get_default_config' = '返回默认的靠泊判定参数配置。'
            'app_dock_judge_init' = '初始化靠泊判定器和内部滤波状态。'
            'app_dock_judge_get_config' = '读取当前正在使用的靠泊判定配置。'
            'app_dock_judge_set_target_id' = '更新本轮允许匹配的目标标签 ID。'
            'app_dock_judge_reset' = '清空滤波与稳定计数，开始新一轮判定。'
            'app_dock_judge_process' = '输入一帧视觉结果，输出当前靠泊状态和辅助指标。'
            'app_dock_judge_format_status' = '把靠泊状态枚举转换成简短状态文案。'
            'app_dock_judge_format_detail' = '把距离、偏移和稳定计数整理成详情字符串。'
        }
    },
    @{
        Path = 'main\\app_ctrl.c'
        Header = @('本地控制状态机。', '负责把视觉结果、触摸输入、任务状态和 CH32 回报串成自动执行链路。')
        Functions = [ordered]@{
            'app_ctrl_now_ms' = '获取当前毫秒时间，用于冷却时间和提示超时判断。'
            'app_ctrl_deadline_active' = '判断某个截止时间是否仍然有效。'
            'app_ctrl_set_notice_locked' = '在持锁条件下更新界面提示文本及其失效时间。'
            'app_ctrl_set_notice' = '对外提供无锁版本的提示文本更新接口。'
            'app_ctrl_start_retrigger_cooldown_locked' = '启动动作触发后的冷却窗口，避免短时间重复下发命令。'
            'app_ctrl_line_has_done_keyword' = '判断旧文本协议消息里是否包含动作完成关键词。'
            'app_ctrl_proto_stage_is_busy' = '根据 CH32 协议阶段值判断当前是否仍在执行动作。'
            'app_ctrl_compose_detail' = '组合界面展示所需的靠泊细节说明。'
            'app_ctrl_compose_guidance' = '按照当前视觉、任务和控制状态生成用户引导语。'
            'app_ctrl_compose_task_status' = '生成顶部状态栏使用的任务状态描述。'
            'app_ctrl_apply_proto_msg_locked' = '把 CH32 上报的一帧协议消息折叠进控制状态机。'
            'app_ctrl_on_ch32_line' = '处理来自 CH32 的文本行或协议帧回调。'
            'app_ctrl_handle_touch' = '响应本地触摸交互，触发启动、取消或复位逻辑。'
            'app_ctrl_task' = '主控制任务，轮询视觉与靠泊结果并决定何时触发 CH32 动作。'
            'app_ctrl_init' = '初始化控制模块状态、同步对象和回调注册。'
            'app_ctrl_start' = '创建控制任务并启动本地控制状态机。'
        }
    },
    @{
        Path = 'main\\app_ch32_link.c'
        Header = @('ESP32 与 CH32 通信模块。', '负责串口收发、proto/legacy 协议兼容解析、ACK 等待和状态缓存。')
        Functions = [ordered]@{
            'app_ch32_crc16_ibm' = '计算 CH32 二进制协议使用的 CRC16/IBM 校验值。'
            'app_ch32_read_i32_le' = '按小端字节序从缓冲区读取一个 32 位有符号整数。'
            'app_ch32_read_u16_le' = '按小端字节序从缓冲区读取一个 16 位无符号整数。'
            'app_ch32_legacy_cmd_to_proto' = '把旧文本命令映射成二进制协议命令码。'
            'app_ch32_proto_cmd_to_legacy' = '把二进制协议命令码映射回旧文本命令。'
            'app_ch32_classify_legacy_line' = '判断一行旧协议文本属于 ACK、状态还是错误消息。'
            'app_ch32_proto_stage_indicates_ready' = '根据 CH32 上报的阶段值判断设备是否进入可执行状态。'
            'app_ch32_apply_common_side_effects' = '统一更新最近一次阶段、重量和 ready 标志等公共状态。'
            'app_ch32_dispatch_msg' = '把解析完成的二进制协议消息分发给上层回调。'
            'app_ch32_dispatch_legacy_line' = '解析并上报一行旧文本协议消息。'
            'app_ch32_parse_proto_frame' = '从接收缓冲中切分并校验一帧完整的二进制协议。'
            'app_ch32_link_rx_task' = '串口接收任务，持续消费输入数据并完成协议解析。'
            'app_ch32_link_prepare_tx_idle' = '在发送前等待发送路径空闲，降低粘包和冲突概率。'
            'app_ch32_link_send_legacy_cmd' = '按旧文本协议格式发送一条命令。'
            'app_ch32_link_send_proto' = '组装并发送一帧 CH32 二进制协议命令。'
            'app_ch32_link_wait_ack_for_cmd' = '等待指定命令的 ACK 到来，直到成功或超时。'
            'app_ch32_link_init' = '初始化串口链路、缓冲区和接收任务。'
            'app_ch32_link_deinit' = '停止接收任务并释放串口链路资源。'
            'app_ch32_link_send_cmd' = '对外统一发送命令，内部根据协议模式选择具体格式。'
            'app_ch32_link_send_cmd_and_wait_ack' = '发送命令后同步等待 ACK，供关键动作链路调用。'
            'app_ch32_link_probe_ready' = '主动探测 CH32 当前是否已经 ready。'
            'app_ch32_link_wait_ready' = '在给定超时时间内等待 CH32 进入 ready 状态。'
            'app_ch32_link_is_ready' = '返回最近一次链路状态里记录的 ready 结果。'
            'app_ch32_link_last_weight' = '返回 CH32 最近一次上报的重量值。'
        }
    },
    @{
        Path = 'main\\app_cloud.c'
        Header = @('云端接入模块。', '负责连接 Wi-Fi 与 MQTT，并把云端命令和本地任务状态互相同步。')
        Functions = [ordered]@{
            'app_cloud_json_get_string' = '从 JSON 对象中安全读取字符串字段。'
            'app_cloud_json_get_u16' = '从 JSON 对象中安全读取 16 位整数参数。'
            'app_cloud_parse_command_json' = '把 MQTT 下发的 JSON 负载解析成内部命令结构。'
            'app_cloud_init_netif_once' = '确保 TCP/IP 网络栈只被初始化一次。'
            'app_cloud_init_event_loop_once' = '确保系统事件循环只初始化一次。'
            'app_cloud_build_topics' = '根据配置拼接出 cmd、ack 和 state 等 MQTT 主题。'
            'app_cloud_log_topics_once' = '仅在首次启动时打印当前使用的 MQTT 主题。'
            'app_cloud_validate_config' = '检查 Wi-Fi 和 MQTT 配置项是否完整有效。'
            'app_cloud_publish_raw' = '向指定 MQTT 主题发布原始负载。'
            'app_cloud_publish_ack' = '向云端发布一条命令应答消息。'
            'app_cloud_publish_task_snapshot_internal' = '把当前任务快照整理成 JSON 并发布到状态主题。'
            'app_cloud_on_task_event' = '监听任务状态变化，并触发对应的云端状态上报。'
            'app_cloud_create_mqtt_client' = '按当前配置创建 MQTT 客户端实例。'
            'app_cloud_destroy_mqtt_client' = '销毁 MQTT 客户端并清理关联资源。'
            'app_cloud_start_mqtt_if_needed' = '在网络就绪后按需启动 MQTT 连接。'
            'app_cloud_receive_set_target' = '处理云端下发的目标标签切换命令。'
            'app_cloud_receive_start_task' = '处理云端下发的开始任务命令。'
            'app_cloud_receive_cancel' = '处理云端下发的取消任务命令。'
            'app_cloud_handle_command' = '根据解析结果分派具体的云端控制命令。'
            'app_cloud_handle_mqtt_data_event' = '处理 MQTT 收包事件并提取 JSON 负载。'
            'app_cloud_task' = '后台云端任务，负责等待网络和维护重连流程。'
            'app_cloud_wifi_event_handler' = '处理 Wi-Fi 与 IP 事件，维护联网状态机。'
            'app_cloud_mqtt_event_handler' = '处理 MQTT 连接、订阅、发布和接收事件。'
            'app_cloud_init' = '初始化云端模块，并注册所需的网络与任务回调。'
            'app_cloud_publish_task_snapshot' = '对外触发一次即时任务状态上报。'
        }
    },
    @{
        Path = 'main\\app_ui.c'
        Header = @('界面渲染模块。', '负责创建 LVGL HUD，并把视觉、任务和靠泊状态绘制成用户可读的界面元素。')
        Functions = [ordered]@{
            'app_get_active_screen' = '获取当前活跃屏幕对象，兼容不同 LVGL 版本的 API。'
            'app_ui_style_label' = '设置状态类文字标签的背景、留白和圆角样式。'
            'app_ui_style_hud_layer' = '把 HUD 覆盖层设置为透明且不可交互。'
            'app_ui_style_cross_line' = '设置准星线条的颜色和透明度。'
            'app_ui_style_track_box' = '设置目标跟踪框的边框样式。'
            'app_ui_style_lock_seg' = '设置锁定进度条单段的默认外观。'
            'app_ui_style_hint_label' = '设置底部提示条的视觉样式。'
            'app_ui_style_auth_label' = '设置识别成功横幅的视觉样式。'
            'app_ui_state_color' = '根据靠泊状态选择 HUD 要使用的颜色。'
            'app_ui_calc_fit_dims' = '计算源画面按比例适配到目标区域后的尺寸。'
            'app_ui_get_rotated_dims' = '根据旋转角度推导画面显示时的宽高。'
            'app_ui_transform_src_point' = '把源图像坐标按旋转方向转换到显示坐标系。'
            'app_ui_clamp_i32' = '把整数值限制在给定区间内。'
            'app_ui_map_bbox_to_screen' = '把视觉检测框映射到屏幕上的 HUD 坐标。'
            'app_ui_update_lock_bar' = '根据稳定帧数量刷新锁定进度条。'
            'app_ui_update_auth_banner' = '控制识别成功横幅的显示、着色与自动隐藏。'
            'app_ui_set_track_box' = '更新跟踪框的位置、大小和显隐状态。'
            'app_ui_create' = '创建 HUD 控件树并完成界面初始布局。'
            'app_ui_set_status' = '更新顶部状态文本。'
            'app_ui_set_coord' = '更新坐标或偏移信息文本。'
            'app_ui_set_vision_text' = '更新视觉识别结果文本。'
            'app_ui_set_dock_text' = '更新靠泊判定结果文本。'
            'app_ui_set_hint_text' = '更新底部提示文案。'
            'app_ui_update_hud' = '根据最新视觉和靠泊结果整体刷新 HUD 显示。'
        }
    },
    @{
        Path = 'main\\app_camera.c'
        Header = @('相机采集与显示模块。', '负责分配缓冲区、接收摄像头帧，并把数据分别送往显示和视觉线程。')
        Functions = [ordered]@{
            'app_camera_msync_aligned' = '检查地址和长度是否满足缓存同步接口要求的对齐限制。'
            'app_camera_msync_m2c' = '把主核写入的内存同步到外设可见的缓存视图。'
            'app_camera_msync_c2m' = '把外设写入的内存同步回 CPU 可见的缓存视图。'
            'app_get_active_screen' = '获取当前活跃屏幕对象，作为相机画面的挂载父对象。'
            'app_camera_free_camera_buffers' = '释放为摄像头采集申请的用户态缓冲区。'
            'app_camera_free_display_buffers' = '释放显示链路使用的画布和显示缓冲。'
            'app_camera_alloc_display_buffers' = '申请显示所需的双缓冲或单缓冲画布。'
            'app_camera_alloc_userptr_buffers' = '为摄像头 USERPTR 模式申请并初始化缓冲区。'
            'app_camera_create_canvas' = '创建承载相机预览画面的 LVGL 画布对象。'
            'app_ppa_init' = '初始化像素处理加速器，供后续缩放与裁剪使用。'
            'calc_aspect_fit' = '计算保持纵横比时最合适的输出尺寸和偏移。'
            'app_image_process_scale_crop' = '把原始图像缩放并裁剪到目标缓冲区。'
            'app_camera_has_pending_stage' = '判断是否仍有等待视觉线程消费的 stage 帧。'
            'app_camera_pick_writable_stage_buffer' = '挑选一个当前可写的 stage 缓冲给采集回调使用。'
            'app_camera_publish_stage_buffer' = '把刚写好的 stage 缓冲标记为可供视觉线程读取。'
            'app_camera_abandon_stage_buffer' = '放弃本次 stage 写入并回收对应缓冲区。'
            'app_camera_take_ready_stage_buffer' = '取出一帧已经准备好的 stage 数据。'
            'app_camera_release_stage_buffer' = '视觉线程处理完成后归还 stage 缓冲。'
            'app_camera_display_task' = '显示任务，负责把最新预览帧刷到屏幕上。'
            'app_camera_frame_cb' = '摄像头取帧回调，完成显示分发和视觉输入缓冲填充。'
            'app_camera_start_display_task' = '创建相机预览显示任务。'
            'app_camera_init' = '初始化相机、画布、缓冲区以及相关同步资源。'
            'app_camera_preview_start' = '启动相机预览与帧回调链路。'
            'app_camera_preview_stop' = '停止相机预览并回收运行中的显示链路。'
            'app_camera_is_preview_running' = '返回当前相机预览是否处于运行状态。'
        }
    },
    @{
        Path = 'main\\app_vision.c'
        Header = @('视觉处理模块。', '负责接收相机帧、运行 AprilTag 检测，并维护最近一次识别结果。')
        Functions = [ordered]@{
            'app_vision_now_ms' = '获取当前毫秒时间，用于给识别结果打时间戳。'
            'app_rgb565_to_gray' = '把 RGB565 图像转换成 AprilTag 检测所需的灰度图。'
            'app_vision_snapshot' = '抓取一份待处理帧及其元数据快照，供视觉线程使用。'
            'app_vision_store_result' = '把最新识别结果安全写入共享结果缓存。'
            'app_vision_get_latest_result' = '读取最近一次视觉识别结果快照。'
            'app_vision_set_wait_text' = '更新视觉线程在等待输入时对外暴露的提示文本。'
            'app_vision_update_result' = '根据本次检测输出刷新共享识别结果和状态文本。'
            'app_vision_task' = '视觉后台任务，持续消费输入帧并执行 AprilTag 检测。'
            'app_vision_init' = '初始化视觉模块、缓冲区和同步对象。'
            'app_vision_start' = '创建视觉任务并启动后台识别循环。'
            'app_vision_submit_frame' = '向视觉线程提交一帧待检测图像。'
        }
    },
    @{
        Path = 'main\\app_video.c'
        Header = @('底层视频采集模块。', '封装 V4L2 设备打开、缓冲管理、取帧回帧以及后台流任务。')
        Functions = [ordered]@{
            'app_video_main' = '预留的视频模块入口，当前不执行额外板级初始化。'
            'fourcc_to_str' = '把 FOURCC 像素格式编码转换为可打印字符串。'
            'app_video_open' = '打开视频设备，并读取或设置基础像素格式信息。'
            'app_video_set_bufs' = '向视频驱动申请缓冲区，并绑定用户提供的帧缓存。'
            'app_video_get_bufs' = '导出当前已经建立好的视频缓冲区指针。'
            'app_video_get_buf_size' = '返回当前单帧视频缓冲区的字节大小。'
            'video_receive_video_frame' = '从驱动取出一帧采集完成的视频数据。'
            'video_operation_video_frame' = '把当前视频帧交给已注册的业务回调处理。'
            'video_free_video_frame' = '把处理完成的缓冲重新入队给视频驱动。'
            'video_stream_start' = '通知驱动开始持续输出视频流。'
            'video_stream_stop' = '通知驱动停止视频流输出。'
            'video_stream_task' = '后台视频流任务，循环完成取帧、处理和回帧。'
            'app_video_stream_task_start' = '创建视频流后台任务并启动采集循环。'
            'app_video_stream_task_stop' = '请求视频流任务停止并结束采集。'
            'app_video_register_frame_operation_cb' = '注册每帧视频数据到达时要执行的处理回调。'
            'app_video_stream_wait_stop' = '等待视频流任务完全退出。'
        }
    },
    @{
        Path = 'main\\app_apriltag.c'
        Header = @('AprilTag 检测模块。', '负责完成灰度阈值化、候选四边形筛选、码字采样和 Tag36h11 解码。')
        Functions = [ordered]@{
            'at_dbg_log_cells' = '调试打印采样后的标签单元格矩阵。'
            'at_dbg_log_bits' = '调试打印解码阶段得到的位图布局。'
            'at_dbg_log_info' = '调试输出当前候选标签的关键信息。'
            'at_index' = '把二维图像坐标转换成线性缓冲区索引。'
            'at_otsu_threshold' = '使用 Otsu 方法为当前灰度图计算二值化阈值。'
            'at_hamming64' = '计算两个 64 位码字之间的汉明距离。'
            'at_threshold_binary' = '按照阈值把灰度图转换成黑白二值图。'
            'at_filter_component' = '根据面积和边界条件筛掉不合理的连通域。'
            'at_candidate_touch_edge' = '判断候选区域是否贴着图像边缘。'
            'at_candidate_too_large' = '判断候选区域是否大到超出合理标签范围。'
            'at_insert_candidate' = '把新的四边形候选按规则插入候选列表。'
            'at_collect_candidates' = '遍历二值图，收集可能属于标签的候选四边形。'
            'at_dist2' = '计算两点之间的平方距离，避免额外开方开销。'
            'at_validate_quad' = '检查候选四边形的几何形状是否满足解码要求。'
            'at_solve_homography' = '根据四边形顶点求解标签平面到图像平面的单应矩阵。'
            'at_project' = '使用单应矩阵把标签平面点投影到图像坐标。'
            'at_sample_bilinear' = '对灰度图执行双线性采样。'
            'at_sample_cell' = '在标签单元格区域内采样并估计黑白值。'
            'at_rotate_bits_cw' = '把采样位图顺时针旋转一圈，供不同朝向匹配。'
            'at_transform_bits' = '按指定朝向整理位图，转换到统一解码顺序。'
            'at_build_code_family' = '按协议位序构造 family 码字表示。'
            'at_build_code_row_major' = '按行优先顺序构造候选码字。'
            'at_build_code_col_major' = '按列优先顺序构造候选码字。'
            'at_build_code_variant' = '生成某种位序和朝向组合下的候选码字。'
            'at_match_code' = '在 Tag36h11 码本中查找距离最近的合法码字。'
            'at_decode_with_quad' = '对单个四边形完成采样、旋转匹配和最终解码。'
            'app_apriltag_init' = '初始化 AprilTag 检测器的参数和内部状态。'
            'app_apriltag_detect_tag36h11' = '在一帧灰度图中检测并解码 Tag36h11 标签。'
        }
    }
)

foreach ($item in $files) {
    $path = Join-Path (Get-Location) $item.Path
    $content = Get-Content -Raw -Encoding UTF8 $path
    $content = $content -replace '\\r\\n', "`r`n"
    $content = Set-TopComment $content $item.Header
    foreach ($entry in $item.Functions.GetEnumerator()) {
        $content = Set-FunctionComment $content $entry.Key $entry.Value
    }
    Set-Content -Encoding UTF8 $path $content
    Write-Host "updated $($item.Path)"
}
