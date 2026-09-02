/*
 * 手机蓝牙遥控小车：UART1 蓝牙 + GPIO11 软件 UART 到 STM32
 *
 * 由于本车 Hi3861 固件的 UART1/UART2 同时启用会使 UART2 失效，
 * 本版本不调用 UartInit(UART2)，直接用 GPIO11 软件发送 115200 8N1。
 * UART1 仍由硬件串口接蓝牙模块：GPIO0/1，9600 8N1。
 * STM32 端继续使用任务24的 V2 10字节协议：
 * FC | 02 | 0A | 左轮int16 LE | 右轮int16 LE | seq | XOR | FD
 *
 * 命令：W前进，S后退，A左转，D右转，O停止，I/K速度100/150。
 * 兼容 App 的 A~K 按钮：B后退，C右转，E停止，F左转。
 */
#include <stdio.h>
#include <unistd.h>
#include "wifiiot_uart.h"
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifiiot_gpio.h"
#include "wifiiot_gpio_ex.h"
#include "hi_io.h"
#include "hi_time.h"

#define STM32_TX_GPIO 11
#define SOFTUART_BIT_US 9       /* 115200 baud: 8.68us/bit */
#define SELFTEST_AUTODRIVE 0    /* 1=上电自动发前进帧，0=蓝牙遥控 */

static int speed_level = 100;
static volatile int target_left = 0;
static volatile int target_right = 0;
static volatile uint8_t tx_seq = 0;

/* 软件 UART TX：空闲高，起始位低，8位数据LSB first，停止位高。 */
static void softuart_tx_byte(uint8_t value)
{
    unsigned int i;

    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE0);
    hi_udelay(SOFTUART_BIT_US);
    for (i = 0; i < 8; i++) {
        GpioSetOutputVal(STM32_TX_GPIO,
            (value & (1U << i)) ? WIFI_IOT_GPIO_VALUE1 : WIFI_IOT_GPIO_VALUE0);
        hi_udelay(SOFTUART_BIT_US);
    }
    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE1);
    hi_udelay(SOFTUART_BIT_US);
}

static void stm32_send_frame(int left, int right)
{
    uint8_t frame[10];
    uint8_t checksum;
    unsigned int i;

    if (left > 150) left = 150;
    if (left < -150) left = -150;
    if (right > 150) right = 150;
    if (right < -150) right = -150;

    frame[0] = 0xFC;
    frame[1] = 0x02;
    frame[2] = 0x0A;
    frame[3] = (uint8_t)(left & 0xFF);
    frame[4] = (uint8_t)((left >> 8) & 0xFF);
    frame[5] = (uint8_t)(right & 0xFF);
    frame[6] = (uint8_t)((right >> 8) & 0xFF);
    frame[7] = tx_seq++;
    checksum = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^
               frame[5] ^ frame[6] ^ frame[7];
    frame[8] = checksum;
    frame[9] = 0xFD;

    /* 软件 UART 是单一发送者，不存在 UART2 HAL 或发送缓冲竞争。 */
    for (i = 0; i < 10; i++) {
        softuart_tx_byte(frame[i]);
    }
}

static void set_target(int left, int right)
{
    target_left = left;
    target_right = right;
}

static void command_forward(void)  { set_target(speed_level, speed_level); }
static void command_backward(void) { set_target(-speed_level, -speed_level); }
static void command_left(void)     { set_target(0, speed_level + 15); }
static void command_right(void)    { set_target(speed_level + 15, 0); }
static void command_stop(void)     { set_target(0, 0); }

/* 独立电机发送任务：不读取 UART1，因此蓝牙接收阻塞不会影响电机帧。 */
static void motor_tx_task(void)
{
    for (;;) {
        stm32_send_frame(target_left, target_right);
        osDelay(5);                 /* 50ms，远小于 STM32 的 500ms 超时 */
    }
}

static void ble_rx_task(void)
{
    uint8_t byte;
    unsigned int n;

    printf("BLE control ready: W/A/S/D/O + I/K\r\n");
    for (;;) {
        n = UartRead(WIFI_IOT_UART_IDX_1, &byte, 1);
        if (n != 1) {
            osDelay(2);
            continue;
        }

        switch ((char)byte) {
            case 'w': case 'W': command_forward();  printf("CMD W: forward %d\r\n", speed_level); break;
            case 's': case 'S': command_backward(); printf("CMD S: backward %d\r\n", speed_level); break;
            case 'a': case 'A': command_left();     printf("CMD A: left\r\n"); break;
            case 'd': case 'D': command_right();    printf("CMD D: right\r\n"); break;
            case 'o': case 'O': command_stop();     printf("CMD O: stop\r\n"); break;
            case 'i': case 'I': speed_level = 100;  printf("CMD I: speed 100\r\n"); break;
            case 'k': case 'K': speed_level = 150;  printf("CMD K: speed 150\r\n"); break;
            case 'b': case 'B': command_backward(); printf("CMD B: backward %d\r\n", speed_level); break;
            case 'c': case 'C': command_right();    printf("CMD C: right\r\n"); break;
            case 'e': case 'E': command_stop();     printf("CMD E: stop\r\n"); break;
            case 'f': case 'F': command_left();     printf("CMD F: left\r\n"); break;
            case '\r': case '\n': case ' ': break;
            default: printf("CMD? [%02x]\r\n", (unsigned char)byte); break;
        }
    }
}

static void ble_control(void)
{
    osThreadAttr_t attr;

    GpioInit();

    /* 只把 GPIO11 配成普通 GPIO，完全绕开 UART2。 */
    hi_io_set_func(STM32_TX_GPIO, 0);
    GpioSetDir(STM32_TX_GPIO, WIFI_IOT_GPIO_DIR_OUT);
    GpioSetOutputVal(STM32_TX_GPIO, WIFI_IOT_GPIO_VALUE1);

#if !SELFTEST_AUTODRIVE
    /* 蓝牙唯一使用硬件 UART1。 */
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_0, WIFI_IOT_IO_FUNC_GPIO_0_UART1_TXD);
    IoSetFunc(WIFI_IOT_IO_NAME_GPIO_1, WIFI_IOT_IO_FUNC_GPIO_1_UART1_RXD);
    WifiIotUartAttribute uart_attr1 = {
        .baudRate = 9600,
        .dataBits = 8,
        .stopBits = 1,
        .parity = 0,
    };
    UartInit(WIFI_IOT_UART_IDX_1, &uart_attr1, NULL);
#endif

    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 1024 * 4;
    attr.priority = 25;

#if SELFTEST_AUTODRIVE
    printf("SELFTEST: soft UART auto forward...\r\n");
    for (;;) {
        stm32_send_frame(100, 100);
        osDelay(5);
    }
#else
    attr.name = "motor_tx";
    if (osThreadNew((osThreadFunc_t)motor_tx_task, NULL, &attr) == NULL) {
        printf("Failed to create motor_tx!\r\n");
    }
    attr.name = "ble_rx";
    if (osThreadNew((osThreadFunc_t)ble_rx_task, NULL, &attr) == NULL) {
        printf("Failed to create ble_rx!\r\n");
    }
#endif
}
APP_FEATURE_INIT(ble_control);
