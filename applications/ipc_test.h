/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * IPC Test 模块 - 公共头文件
 * M33 侧 IPC 原始数据收发 (不做协议解析)
 */

#ifndef __IPC_TEST_H__
#define __IPC_TEST_H__

#include <rtthread.h>
#include <stdint.h>
#include "ipc_common.h"   /* RC_CHANNEL_COUNT */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 接收 API
 * ================================================================ */

/**
 * @brief 初始化 M33 IPC 测试接收器
 *        包括: 打开 ipc0 设备、注册回调、创建接收线程
 *        接收到 M55 数据后完整打印原始帧到日志
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_test_receiver_init(void);

/**
 * @brief 获取应用层接收帧计数
 */
uint32_t ipc_test_get_rx_count(void);

/* ================================================================
 * 发送 API (不做协议编码, 直接发送原始 channel 数据)
 * ================================================================ */

/**
 * @brief 发送原始 channel 数据到 M55
 * @param dev     IPC 设备句柄
 * @param channel 8 个 uint16_t 的 channel 数据 (RC_CHANNEL_COUNT)
 * @param seq     帧序号
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_send_raw_frame(rt_device_t dev,
                            const uint16_t channel[RC_CHANNEL_COUNT],
                            uint32_t seq);

/**
 * @brief 【用户接口】发送数据到 M55 (高度封装, 只需提供数据指针+长度)
 *
 * 函数自动完成帧封装 (magic/role/client_id/checksum/seq),
 * 用户只需关注要发送的业务数据。
 *
 * ==== 使用示例 ====
 * @code
 *   rt_device_t ipc = rt_device_find("ipc0");
 *
 *   // 发送简单字节数组
 *   uint8_t buf[] = {0xAA, 0xBB, 0x01, 0x02};
 *   ipc_send_data(ipc, buf, sizeof(buf));
 *
 *   // 发送结构体
 *   typedef struct { uint32_t id; float value; } msg_t;
 *   msg_t msg = { .id = 1, .value = 3.14f };
 *   ipc_send_data(ipc, (uint8_t *)&msg, sizeof(msg));
 * @endcode
 *
 * @param dev  IPC 设备句柄
 * @param data 要发送的数据指针
 * @param len  数据长度 (最大 16 字节)
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_send_data(rt_device_t dev, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __IPC_TEST_H__ */
