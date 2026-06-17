/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * IPC Test 模块 - M33 侧接收实现
 *
 * 依赖: BSP_USING_IPC=y (menuconfig 中开启)
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "drv_ipc.h"      /* edge_rc_frame_t, EDGE_IPC_DEVICE_NAME, EDGE_IPC_CTRL_GET_STATS */
#include "ipc_common.h"   /* RC_CHANNEL_COUNT */
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
 * 获取应用层接收帧计数
 */
uint32_t ipc_test_get_rx_count(void)
{
    return ipc_rx_count;
}

/* ================================================================
 * IPC 发送 API (不做协议编码, 直接发送原始 channel 数据)
 * ================================================================ */

/**
 * 发送原始 channel 数据到 M55 (不做任何协议封装)
 * @param dev     IPC 设备句柄
 * @param channel 8 个 uint16_t 的 channel 数据 (RC_CHANNEL_COUNT)
 * @param seq     帧序号 (自动递增或用户指定)
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_send_raw_frame(rt_device_t dev,
                            const uint16_t channel[RC_CHANNEL_COUNT],
                            uint32_t seq)
{
    edge_rc_frame_t frame;

    if (dev == RT_NULL || channel == RT_NULL)
        return -RT_EINVAL;

    rt_memset(&frame, 0, sizeof(frame));

    frame.magic     = RC_MAGIC_WORD;
    frame.role      = RC_ROLE_M33;
    frame.client_id = CM33_IPC_PIPE_CLIENT_ID;
    frame.seq       = seq;

    rt_memcpy(frame.channel, channel, sizeof(frame.channel));

    frame.checksum = edge_rc_checksum(&frame);

    if (rt_device_write(dev, 0, &frame, 1) != 1)
        return -RT_EIO;

    return RT_EOK;
}

/**
 * 【用户接口】发送数据到 M55 (高度封装)
 *
 * 用户只需提供要发送的数据内容和长度, 函数自动完成:
 *   - 帧头填充 (magic, role, client_id)
 *   - 数据拷贝到 channel 区域
 *   - 校验和计算
 *   - 帧序号自动递增
 *   - 设备写入
 *
 * ==== 使用方法 (只需 2 行代码) ====
 *
 *   1. 获取 IPC 设备:
 *        rt_device_t ipc = rt_device_find("ipc0");
 *
 *   2. 发送数据:
 *        uint8_t buf[] = {0xAA, 0xBB, 0x01, 0x02};
 *        ipc_send_data(ipc, buf, sizeof(buf));
 *
 *   或者发送结构体:
 *        typedef struct { uint32_t id; float value; } my_msg_t;
 *        my_msg_t msg = { .id = 1, .value = 3.14f };
 *        ipc_send_data(ipc, (uint8_t *)&msg, sizeof(msg));
 *
 * ==== 注意事项 ====
 *   - 单帧最大 16 字节 (对应 channel[0..7] 共 8×uint16_t)
 *   - 数据按字节原样拷贝, M55 侧按相同结构解析即可
 *   - 发送前需确保 IPC 设备已打开 (ipc_test_receiver_init 已处理)
 *
 * @param dev  IPC 设备句柄 (通过 rt_device_find("ipc0") 获取)
 * @param data 要发送的数据指针
 * @param len  数据长度 (最大 16 字节)
 * @return RT_EOK 成功, 其他失败
 */
rt_err_t ipc_send_data(rt_device_t dev, const uint8_t *data, uint32_t len)
{
    uint16_t channel[RC_CHANNEL_COUNT];
    static uint32_t send_seq = 0;

    if (dev == RT_NULL)
        return -RT_ERROR;

    if (len > sizeof(channel))
        return -RT_EINVAL;

    /* 清零 channel, 将用户数据按字节拷贝 */
    rt_memset(channel, 0, sizeof(channel));
    if (data != RT_NULL && len > 0)
    {
        rt_memcpy(channel, data, len);
    }

    return ipc_send_raw_frame(dev, channel, send_seq++);
}

/* ================================================================
 * 内部: 帧解析与处理
 * ================================================================ */

/**
 * 接收并打印 M55 核心发送的 IPC 原始帧数据 (不做协议解析)
 */
static void ipc_process_frame(const edge_rc_frame_t *frame)
{
    uint32_t i;

    rt_kprintf("\r\n");
    rt_kprintf("╔══════════════════════════════════════════╗\r\n");
    rt_kprintf("║     [M33←M55] IPC 原始帧数据  #%-8lu  ║\r\n",
               (unsigned long)++ipc_rx_count);
    rt_kprintf("╠══════════════════════════════════════════╣\r\n");
    rt_kprintf("║ magic     : 0x%08lX                    ║\r\n",
               (unsigned long)frame->magic);
    rt_kprintf("║ role      : 0x%02X                        ║\r\n",
               (unsigned int)frame->role);
    rt_kprintf("║ client_id : %u                            ║\r\n",
               (unsigned int)frame->client_id);
    rt_kprintf("║ seq       : %lu                            ║\r\n",
               (unsigned long)frame->seq);
    rt_kprintf("║ intr_mask : 0x%04X                      ║\r\n",
               (unsigned int)frame->intr_mask);
    rt_kprintf("║ checksum  : 0x%08lX                    ║\r\n",
               (unsigned long)frame->checksum);
    rt_kprintf("╠══════════════════════════════════════════╣\r\n");

    /* 打印 channel[0..7] 原始数据 */
    for (i = 0; i < RC_CHANNEL_COUNT; i++)
    {
        rt_kprintf("║ channel[%lu]: 0x%04X (%5u)               ║\r\n",
                   (unsigned long)i,
                   (unsigned int)frame->channel[i],
                   (unsigned int)frame->channel[i]);
    }

    rt_kprintf("╚══════════════════════════════════════════╝\r\n");
    rt_kprintf("\r\n");
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

static void ipc_send_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_device_t dev = rt_device_find(EDGE_IPC_DEVICE_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("[IPC] device not found, send aborted\r\n");
        return;
    }

    /* 示例: 发送 4 字节测试数据 "TEST" */
    uint8_t test_data[] = {0x54, 0x45, 0x53, 0x54};
    rt_err_t ret = ipc_send_data(dev, test_data, sizeof(test_data));

    if (ret == RT_EOK)
    {
        rt_kprintf("[IPC] data sent OK (%u bytes)\r\n",
                   (unsigned int)sizeof(test_data));
    }
    else
    {
        rt_kprintf("[IPC] send FAILED (%d)\r\n", ret);
    }
}
// MSH_CMD_EXPORT(ipc_send, Send 4-byte test data to M55 via IPC);
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
