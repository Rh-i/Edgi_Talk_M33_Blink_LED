/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * IPC Test 模块 - M33 侧接收实现
 *
 * 依赖: BSP_USING_IPC=y (menuconfig 中开启)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "drv_ipc.h"      /* edge_rc_frame_t, EDGE_IPC_DEVICE_NAME, EDGE_IPC_CTRL_GET_STATS */
#include "ipc_common.h"   /* RC_CHANNEL_COUNT, RC_MAGIC_WORD, RC_ROLE_*, CM33_IPC_PIPE_CLIENT_ID */
#include "ipc_test.h"

/* ================================================================
 * 模块内部变量
 * ================================================================ */

static rt_device_t ipc_dev      = RT_NULL;   /* IPC 设备句柄 */
static rt_sem_t   ipc_rx_sem    = RT_NULL;   /* 接收通知信号量 */
static uint32_t   ipc_rx_count  = 0;         /* 应用层接收帧计数 */

/* ================================================================
 * 公共 API 实现
 * ================================================================ */

/**
 * 通过 IPC 发送一帧数据 (与 M55 ipc_frame_send 协议兼容)
 *
 * 帧格式:
 *   channel[0] = (data_len << 8) | cmd
 *   channel[1..] = payload data (最多 14 bytes)
 */
rt_err_t ipc_frame_send(rt_device_t dev, uint8_t cmd,
                        const uint8_t *data, uint32_t len,
                        uint32_t seq)
{
    edge_rc_frame_t frame;

    if (dev == RT_NULL || (len > 0 && data == RT_NULL))
        return -RT_EINVAL;
    /* channel[0] 被协议头占用, 数据最多 14 bytes */
    if (len > (RC_CHANNEL_COUNT - 1) * 2)
        return -RT_EINVAL;

    rt_memset(&frame, 0, sizeof(frame));

    /* 帧头 (M33 侧 drv_ipc 写入时会覆盖 magic/checksum/client_id) */
    frame.magic     = RC_MAGIC_WORD;
    frame.role      = RC_ROLE_M33;          /* M33 → M55 */
    frame.client_id = CM33_IPC_PIPE_CLIENT_ID;
    frame.seq       = seq;

    /* channel[0] 编码: 高字节=数据长度, 低字节=命令码 */
    frame.channel[0] = ((uint16_t)len << 8) | (uint16_t)cmd;

    /* 拷贝数据到 channel[1..] */
    if (data != RT_NULL && len > 0)
    {
        rt_memcpy(&frame.channel[1], data, len);
    }

    frame.checksum = edge_rc_checksum(&frame);

    if (rt_device_write(dev, 0, &frame, 1) != 1)
        return -RT_EIO;

    return RT_EOK;
}

/**
 * 获取应用层接收帧计数
 */
uint32_t ipc_test_get_rx_count(void)
{
    return ipc_rx_count;
}

/* ================================================================
 * 内部: 帧解析与处理
 * ================================================================ */

/**
 * 解析并处理一帧 IPC 数据
 *
 * M55 协议格式:
 *   channel[0] = (data_len << 8) | cmd
 *   channel[1..] = payload
 *   其中 cmd 来自 ipc_test.h (IPC_TEST_CMD_COUNTER = 0x10)
 */
static void ipc_process_frame(const edge_rc_frame_t *frame)
{
    /* 从 channel[0] 低字节提取命令码 */
    uint8_t cmd = (uint8_t)(frame->channel[0] & 0xFFU);
    uint8_t len = (uint8_t)((frame->channel[0] >> 8) & 0xFFU);

    switch (cmd)
    {
    case IPC_TEST_CMD_COUNTER:
    {
        ipc_test_data_t data;

        /* M55 将 ipc_test_data_t 放在 channel[1..] */
        rt_memcpy(&data, &frame->channel[1], sizeof(data));

        uint32_t now     = rt_tick_get();
        int32_t  latency = (int32_t)(now - data.timestamp);

        rt_kprintf("[IPC RX #%lu] counter=%lu, ts=%lu, "
                   "latency=%ld ticks, seq=%lu, role=0x%02X\r\n",
                   (unsigned long)++ipc_rx_count,
                   (unsigned long)data.counter,
                   (unsigned long)data.timestamp,
                   (long)latency,
                   (unsigned long)frame->seq,
                   (unsigned int)frame->role);

        /* 向 M55 发送 ACK 确认 */
        {
            ipc_test_data_t ack;
            ack.counter   = data.counter;
            ack.timestamp = now;
            ipc_frame_send(ipc_dev, IPC_TEST_CMD_COUNTER_ACK,
                           (const uint8_t *)&ack, sizeof(ack), frame->seq);
        }

        break;
    }
    case IPC_TEST_CMD_COUNTER_ACK:
    {
        /* M33 发出的 ACK, 防御性忽略 */
        break;
    }
    default:
        rt_kprintf("[IPC RX] unknown cmd=0x%02X len=%u, seq=%lu, role=0x%02X\r\n",
                   (unsigned int)cmd,
                   (unsigned int)len,
                   (unsigned long)frame->seq,
                   (unsigned int)frame->role);
        break;
    }
}

/* ================================================================
 * 内部: 接收回调 & 处理线程
 * ================================================================ */

/**
 * IPC 接收回调 (ISR 上下文, 仅释放信号量)
 */
static rt_err_t ipc_rx_indicate(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;
    rt_sem_release(ipc_rx_sem);
    return RT_EOK;
}

/**
 * IPC 接收处理线程入口
 */
static void ipc_receiver_thread_entry(void *parameter)
{
    (void)parameter;
    edge_rc_frame_t frame;

    rt_kprintf("[IPC] receiver thread started\r\n");

    while (1)
    {
        if (rt_sem_take(ipc_rx_sem, RT_WAITING_FOREVER) != RT_EOK)
            continue;

        /* 批量读出所有排队帧 */
        while (rt_device_read(ipc_dev, 0, &frame, 1) == 1)
        {
            ipc_process_frame(&frame);
        }
    }
}

/* ================================================================
 * MSH 统计命令
 * ================================================================ */

#ifdef RT_USING_MSH
static void ipc_stats(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    edge_ipc_device_stats_t stats;
    rt_device_t dev = rt_device_find(EDGE_IPC_DEVICE_NAME);

    if (dev == RT_NULL)
    {
        rt_kprintf("[IPC] device not found\r\n");
        return;
    }

    if (rt_device_control(dev, EDGE_IPC_CTRL_GET_STATS, &stats) != RT_EOK)
    {
        rt_kprintf("[IPC] stats query failed\r\n");
        return;
    }

    rt_kprintf("\r\n=== IPC Statistics ===\r\n");
    rt_kprintf("  TX OK:     %lu\r\n", (unsigned long)stats.tx_ok);
    rt_kprintf("  TX ERR:    %lu\r\n", (unsigned long)stats.tx_err);
    rt_kprintf("  RX OK:     %lu\r\n", (unsigned long)stats.rx_ok);
    rt_kprintf("  RX ERR:    %lu\r\n", (unsigned long)stats.rx_err);
    rt_kprintf("  RX DROP:   %lu\r\n", (unsigned long)stats.rx_drop);
    rt_kprintf("  SEMA FAIL: %lu\r\n", (unsigned long)stats.sema_fail);
    rt_kprintf("  App RX:    %lu\r\n", (unsigned long)ipc_rx_count);
    rt_kprintf("======================\r\n\r\n");
}
MSH_CMD_EXPORT(ipc_stats, Show IPC driver statistics);
#endif /* RT_USING_MSH */

/* ================================================================
 * 公共: 初始化入口
 * ================================================================ */

rt_err_t ipc_test_receiver_init(void)
{
    rt_kprintf("[IPC] initializing M33 receiver...\r\n");

    /* 1. 创建接收信号量 */
    ipc_rx_sem = rt_sem_create("ipc_rx", 0, RT_IPC_FLAG_FIFO);
    if (ipc_rx_sem == RT_NULL)
    {
        rt_kprintf("[IPC] ERROR: sem create failed\r\n");
        return -RT_ENOMEM;
    }

    /* 2. 查找 IPC 设备 */
    ipc_dev = rt_device_find(EDGE_IPC_DEVICE_NAME);
    if (ipc_dev == RT_NULL)
    {
        rt_kprintf("[IPC] ERROR: device '%s' not found\r\n"
                   "      Enable BSP_USING_IPC in menuconfig.\r\n",
                   EDGE_IPC_DEVICE_NAME);
        return -RT_ERROR;
    }

    /* 3. 打开设备 (带中断接收标志) */
    if (rt_device_open(ipc_dev, RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("[IPC] ERROR: device open failed\r\n");
        return -RT_ERROR;
    }

    /* 4. 注册接收回调 */
    rt_device_set_rx_indicate(ipc_dev, ipc_rx_indicate);

    /* 5. 创建接收处理线程 */
    rt_thread_t tid = rt_thread_create("ipc_rx",
                                        ipc_receiver_thread_entry,
                                        RT_NULL,
                                        2048,
                                        12,
                                        10);
    if (tid == RT_NULL)
    {
        rt_kprintf("[IPC] ERROR: thread create failed\r\n");
        return -RT_ENOMEM;
    }

    rt_thread_startup(tid);

    rt_kprintf("[IPC] receiver ready, waiting for M55 data...\r\n\r\n");
    return RT_EOK;
}
