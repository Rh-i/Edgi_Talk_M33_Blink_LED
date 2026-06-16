/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * IPC Test 模块 - 公共头文件
 * M33 / M55 双核间计数器测试通信
 */

#ifndef __IPC_TEST_H__
#define __IPC_TEST_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * IPC 测试命令码 (与 M55 侧 ipc_test.h 保持一致)
 * 编码于 edge_rc_frame_t.channel[0] 低字节
 * ================================================================ */
#define IPC_TEST_CMD_COUNTER     0x10U   /* M55→M33 计数器测试 */
#define IPC_TEST_CMD_COUNTER_ACK 0x20U   /* M33→M55 应答 */

/* ================================================================
 * 数据包结构体 (packed, 小端, 8 bytes)
 * M55 发送时数据位于 channel[1..4] (counter) + channel[5..8] (timestamp)
 * ================================================================ */
typedef struct __attribute__((packed))
{
    uint32_t counter;   /* 递增计数器值 */
    uint32_t timestamp; /* 发送时的系统 tick (ms) */
} ipc_test_data_t;

/* ================================================================
 * 公共 API
 * ================================================================ */

/**
 * @brief 通过 IPC 发送一帧数据 (与 M55 协议兼容)
 *        编码: channel[0] = (len << 8) | cmd, 数据从 channel[1] 开始
 * @param dev  IPC 设备句柄
 * @param cmd  命令码 (填入 channel[0] 低字节)
 * @param data 数据指针
 * @param len  数据长度 (≤ 14 bytes)
 * @param seq  帧序号 (填入 edge_rc_frame_t.seq)
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_frame_send(rt_device_t dev, uint8_t cmd,
                        const uint8_t *data, uint32_t len,
                        uint32_t seq);

/**
 * @brief 初始化 M33 IPC 测试接收器
 *        包括: 打开 ipc0 设备、注册回调、创建接收线程
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_test_receiver_init(void);

/**
 * @brief 获取应用层接收帧计数
 */
uint32_t ipc_test_get_rx_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __IPC_TEST_H__ */
