#include "app_gui_spi.h"
#include "main.h"
#include "cmsis_os.h"
#include "app_uart_parser.h"
#include "app_test.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

_Static_assert(GUI_SPI_RX_BUF_SIZE >= 64 && GUI_SPI_RX_BUF_SIZE <= LINE_BUF_MAX,
               "GUI SPI RX buffer must fit line parser limits");

/* ======================== 外部引用 ======================== */

/* SPI2 句柄，由 CubeMX 生成（spi.c）；G0B1 侧使用其 SPI1 */
extern SPI_HandleTypeDef hspi2;

/* GUI 命令接收队列，由 app_freertos.c 创建 */
osMessageQueueId_t gui_cmd_queue;
osMutexId_t gui_spi_mutex = NULL;

/* ======================== 内部状态 ======================== */

/* EXTI 中断标志：G0B1 拉低 INT 引脚 → ISR 置 1 → 任务消费后清 0 */
static volatile uint8_t s_gui_int_flag = 0;

/* 共享状态变量（原 TouchGFX Data_Transfer.c 定义） */
uint8_t  if_now_SMT         = 0;
uint8_t  total_SMT          = 0;
uint8_t  now_SMT            = 0;
uint8_t  if_DOWNLOAD_READY  = 0;
volatile uint8_t g_gui_smt_start_req = 0;
volatile uint8_t g_gui_smt_pause_req = 0;

/* 行解析器实例 */
static LineParser_t s_line_parser;
static uint32_t s_gui_queue_drop_cnt = 0;
static uint32_t s_gui_spi_tx_err = 0;
static uint32_t s_gui_spi_rx_err = 0;

/* ======================== CS 引脚操作宏 ======================== */

#define GUI_CS_LOW()   (GUI_SPI_CS_PORT->BSRR = (uint32_t)(GUI_SPI_CS_PIN << 16))
#define GUI_CS_HIGH()  (GUI_SPI_CS_PORT->BSRR = GUI_SPI_CS_PIN)

/* ======================== G4 → G0B1 发送内部实现 ======================== */

/* 最大发送帧长（含 \n 和 \0） */
#define GUI_TX_MAX  128

static void gui_spi_tx_raw(const char *data, uint16_t len)
{
    if (len == 0) return;

    if (gui_spi_mutex != NULL) osMutexAcquire(gui_spi_mutex, osWaitForever);
    GUI_CS_LOW();
    if (HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, 100) != HAL_OK) {
        s_gui_spi_tx_err++;
        PrintDebug("[GUI] SPI2 TX err=%lu\r\n", (unsigned long)s_gui_spi_tx_err);
    }
    GUI_CS_HIGH();
    if (gui_spi_mutex != NULL) osMutexRelease(gui_spi_mutex);
}

/* ======================== 公共 API ======================== */

void GUI_SPI_Send(const char *fmt, ...)
{
    char buf[GUI_TX_MAX];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, GUI_TX_MAX - 1, fmt, args);
    va_end(args);

    if (len < 0) return;
    if (len >= GUI_TX_MAX - 1) len = GUI_TX_MAX - 2;  /* truncate */

    /* 追加 \n */
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    gui_spi_tx_raw(buf, len + 1);
}

void GUI_SPI_NotifyTemp(float heat, float pcb)
{
    char buf1[32], buf2[32];
    snprintf(buf1, sizeof(buf1), "TEMP_HEAT:%.1f", (double)heat);
    snprintf(buf2, sizeof(buf2), "TEMP_PCB:%.1f",  (double)pcb);
    GUI_SPI_Send("%s", buf1);
    GUI_SPI_Send("%s", buf2);
}

void GUI_SPI_NotifySMTProgress(uint16_t current, uint16_t total)
{
    now_SMT = (uint8_t)current;
    total_SMT = (uint8_t)total;
    GUI_SPI_Send("SMT_PROGRESS:%u,%u", current, total);
}

void GUI_SPI_NotifySMTStatus(uint8_t is_smt)
{
    if_now_SMT = is_smt ? 1u : 0u;
    GUI_SPI_NotifySMTProgress(now_SMT, total_SMT);
}

void GUI_SPI_NotifyLog(const char *msg)
{
    GUI_SPI_Send("LOG:%.63s", msg);  /* 最多 63 字符 */
}

void GUI_SPI_NotifyWifiStatus(const char *state)
{
    GUI_SPI_Send("WIFI_STATUS:%s", state);
}

void GUI_SPI_NotifyImportData(const char *content)
{
    GUI_SPI_Send("IMPORT_DATA:%.63s", content);
}

void GUI_SPI_NotifyImportTotal(uint16_t total)
{
    GUI_SPI_Send("IMPORT_TOTAL:%u", total);
}

/* ======================== G0B1 → G4 命令接收 ======================== */

int GUI_SPI_RecvPoll(char *buf, uint16_t bufsize)
{
    uint8_t rx[GUI_SPI_RX_BUF_SIZE];
    uint16_t i;
    HostParsed_t tmp_parsed;

    if (bufsize == 0) return 0;

    if (!s_gui_int_flag) return 0;
    s_gui_int_flag = 0;

    /* 清空接收缓冲 */
    memset(rx, 0, sizeof(rx));

    /* SPI 读事务：主设备发送 dummy 字节，同时接收从设备数据 */
    if (gui_spi_mutex != NULL) osMutexAcquire(gui_spi_mutex, osWaitForever);
    GUI_CS_LOW();
    HAL_StatusTypeDef st = HAL_SPI_Receive(&hspi2, rx, GUI_SPI_RX_BUF_SIZE, 100);
    GUI_CS_HIGH();
    if (gui_spi_mutex != NULL) osMutexRelease(gui_spi_mutex);
    if (st != HAL_OK) {
        s_gui_spi_rx_err++;
        PrintDebug("[GUI] SPI2 RX err=%lu\r\n", (unsigned long)s_gui_spi_rx_err);
        LineParser_Init(&s_line_parser);
        return 0;
    }

    /* 喂入行解析器，检测 \n */
    for (i = 0; i < GUI_SPI_RX_BUF_SIZE; i++) {
        if (rx[i] == 0) break;  /* 从设备以 0 填充未使用部分 */
        LineParser_Feed(&s_line_parser, rx[i], &tmp_parsed);
        if (s_line_parser.complete) {
            /* 复制完整行到调用者缓冲区 */
            uint16_t copy_len = s_line_parser.idx;
            if (copy_len >= bufsize) copy_len = bufsize - 1;
            memcpy(buf, s_line_parser.buf, copy_len);
            buf[copy_len] = '\0';

            /* 去除尾部 \r\n */
            while (copy_len > 0 && (buf[copy_len - 1] == '\r' || buf[copy_len - 1] == '\n')) {
                buf[--copy_len] = '\0';
            }

            /* 重置解析器 */
            LineParser_Init(&s_line_parser);
            return 1;
        }
    }

    /* 未收到完整行（超长或被截断），重置解析器 */
    LineParser_Init(&s_line_parser);
    return 0;
}

/* ======================== EXTI 中断回调 ======================== */

void GUI_SPI_IntCallback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GUI_SPI_INT_PIN) {
        s_gui_int_flag = 1;
    }
}

/* ======================== FreeRTOS 任务 ======================== */

void GUI_SPI_Task(void *argument)
{
    char line[GUI_SPI_RX_BUF_SIZE];
    HostParsed_t parsed;

    LineParser_Init(&s_line_parser);

    for (;;) {
        /* 轮询接收 */
        while (GUI_SPI_RecvPoll(line, sizeof(line))) {
            LineParser_t lp;
            LineParser_Init(&lp);
            memset(&parsed, 0, sizeof(parsed));
            for (uint16_t i = 0; line[i] != '\0' && i < LINE_BUF_MAX - 1; i++) {
                LineParser_Feed(&lp, (uint8_t)line[i], &parsed);
            }
            LineParser_Feed(&lp, '\n', &parsed);
            if (parsed.cmd == HCMD_NONE) continue;
            if (osMessageQueuePut(gui_cmd_queue, &parsed, 0, 10) != osOK) {
                s_gui_queue_drop_cnt++;
                PrintDebug("[GUI] cmd queue full, drop=%lu\r\n",
                           (unsigned long)s_gui_queue_drop_cnt);
            }
        }

        osDelay(5);  /* 5ms 轮询周期 */
    }
}

/* ======================== 初始化 ======================== */

void GUI_SPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* ---- CS 引脚：推挽输出，初始高电平（不选中） ---- */
    GPIO_InitStruct.Pin   = GUI_SPI_CS_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GUI_SPI_CS_PORT, &GPIO_InitStruct);
    GUI_CS_HIGH();

    /* ---- INT 引脚：输入，下降沿中断 ---- */
    GPIO_InitStruct.Pin   = GUI_SPI_INT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GUI_SPI_INT_PORT, &GPIO_InitStruct);

    /* 使能 EXTI 中断（NVIC 优先级在 CubeMX 中配置） */
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);  /* PD8 → EXTI line 8（如改用其他引脚，需修改此处） */

    /* 初始化行解析器 */
    LineParser_Init(&s_line_parser);

    s_gui_int_flag = 0;
}
