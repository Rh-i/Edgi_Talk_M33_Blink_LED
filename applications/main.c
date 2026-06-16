/*
 * Copyright (c) 2006-2023, RT-Thread Development Team
 *
 * M33 主函数 - IPC 测试接收器入口
 */

#include <board.h>
#include <rtthread.h>

#include "ipc_test.h"

#define LED_PIN_BLUE GET_PIN(16, 5)

int main(void)
{
    rt_kprintf("\r\n");
    rt_kprintf("============================================\r\n");
    rt_kprintf("  M33 IPC Test Receiver\r\n");
    rt_kprintf("  Core: Cortex-M33\r\n");
    rt_kprintf("============================================\r\n\r\n");

    /* 初始化 LED */
    rt_pin_mode(LED_PIN_BLUE, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN_BLUE, PIN_LOW);

    /* 初始化 IPC 测试接收器 */
    if (ipc_test_receiver_init() != RT_EOK)
    {
        rt_kprintf("[MAIN] IPC init failed, halt.\r\n");
        while (1)
        {
            rt_thread_mdelay(1000);
        }
    }
    else
    {
        rt_kprintf("[MAIN_M33] IPC init OK.\r\n");
    }

    /* 主循环: LED 心跳 + IPC 接收状态指示 */
    while (1)
    {
        /* LED 根据接收计数闪烁 */
        uint32_t cnt = ipc_test_get_rx_count();
        rt_pin_write(LED_PIN_BLUE, (cnt & 1) ? PIN_HIGH : PIN_LOW);
        rt_thread_mdelay(300);
    }

    return 0;
}
