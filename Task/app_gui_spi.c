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

/* 共享状态变量（原 TouchGFX Data_Transfer.c 定义） */
uint8_t  if_now_SMT         = 0;
uint8_t  total_SMT          = 0;
uint8_t  now_SMT            = 0;
uint8_t  if_DOWNLOAD_READY  = 0;
volatile uint8_t g_gui_smt_start_req = 0;
volatile uint8_t g_gui_smt_pause_req = 0;

static uint32_t s_gui_queue_drop_cnt = 0;
static uint32_t s_gui_spi_tx_err = 0;
static uint32_t s_gui_spi_rx_err = 0;

/* LOG rate limit: max 20 frames per second */
static uint32_t s_gui_log_window_tick = 0;
static uint8_t  s_gui_log_window_cnt = 0;

/* Handshake/REQ_TX debug state */
static uint8_t  s_gui_req_tx_low_prev = 0;
static uint32_t s_gui_bad_frame_cnt = 0;

/* ======================== 引脚操作宏 ======================== */

#define GUI_CS_LOW()        (GUI_SPI_CS_PORT->BSRR = (uint32_t)(GUI_SPI_CS_PIN << 16))
#define GUI_CS_HIGH()       (GUI_SPI_CS_PORT->BSRR = GUI_SPI_CS_PIN)
#define GUI_DATA_RDY_LOW()  (GUI_DATA_RDY_PORT->BSRR = (uint32_t)(GUI_DATA_RDY_PIN << 16))
#define GUI_DATA_RDY_HIGH() (GUI_DATA_RDY_PORT->BSRR = GUI_DATA_RDY_PIN)
#define GUI_REQ_TX_ACTIVE() (HAL_GPIO_ReadPin(GUI_REQ_TX_PORT, GUI_REQ_TX_PIN) == GPIO_PIN_RESET)

static void gui_spi_delay_us(uint32_t us)
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = (HAL_RCC_GetSysClockFreq() / 1000000U) * us;
    while ((DWT->CYCCNT - start) < ticks) {
    }
}

/* ======================== G4 → G0B1 发送内部实现 ======================== */

static void gui_spi_tx_raw(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len != GUI_SPI_FRAME_SIZE) return;

    if (gui_spi_mutex != NULL) osMutexAcquire(gui_spi_mutex, osWaitForever);
    GUI_DATA_RDY_LOW();
    GUI_CS_LOW();
    gui_spi_delay_us(1);  /* SPI v1.6: CS low to first clock >= 1us */
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, 100);
    GUI_CS_HIGH();
    GUI_DATA_RDY_HIGH();  /* SPI v1.6: release DATA_RDY after CS high */
    gui_spi_delay_us(100); /* SPI v1.6: >=100us between transactions */
    if (st != HAL_OK) {
        s_gui_spi_tx_err++;
        PrintDebug("[GUI] SPI2 TX err=%lu\r\n", (unsigned long)s_gui_spi_tx_err);
    }
    if (gui_spi_mutex != NULL) osMutexRelease(gui_spi_mutex);
}

/* ======================== 公共 API ======================== */

void GUI_Send(const char *cmd)
{
    uint8_t buf[GUI_SPI_FRAME_SIZE];
    size_t len;

    if (cmd == NULL) return;

    memset(buf, 0, sizeof(buf));
    len = strlen(cmd);
    if (len >= GUI_SPI_FRAME_SIZE) len = GUI_SPI_FRAME_SIZE - 1;
    memcpy(buf, cmd, len);
    buf[len] = '\n';  /* SPI v1.6: newline terminator, remaining bytes stay 0x00 */
    gui_spi_tx_raw(buf, GUI_SPI_FRAME_SIZE);
}

void GUI_SPI_Send(const char *fmt, ...)
{
    char buf[GUI_SPI_FRAME_SIZE];
    va_list args;
    int len;

    if (fmt == NULL) return;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len < 0) return;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    buf[len] = '\0';
    GUI_Send(buf);
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

static void gui_spi_notify_text(const char *cmd, const char *msg)
{
    char safe[64];
    uint16_t n = 0;
    uint16_t i = 0;

    if (msg != NULL) {
        while (msg[i] != '\0' && n < 63U) {
            char c = msg[i++];
            if (c == '\r' || c == '\n') break;
            if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU) c = '?';
            safe[n++] = c;
        }
    }
    safe[n] = '\0';
    GUI_SPI_Send("%s:%s", cmd, safe);
}

static int gui_spi_log_ok(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_gui_log_window_tick) >= 1000U) {
        s_gui_log_window_tick = now;
        s_gui_log_window_cnt = 0U;
    }
    if (s_gui_log_window_cnt >= 20U) return 0;
    s_gui_log_window_cnt++;
    return 1;
}

void GUI_SPI_NotifyLog(const char *msg)
{
    if (!gui_spi_log_ok()) return;
    gui_spi_notify_text("LOG", msg);
}

void GUI_SPI_NotifyWifiStatus(const char *state)
{
    GUI_SPI_Send("WIFI_STATUS:%s", state);
}

void GUI_SPI_NotifyImportData(const char *content)
{
    gui_spi_notify_text("IMPORT_DATA", content);
}

void GUI_SPI_NotifyImportTotal(uint16_t total)
{
    GUI_SPI_Send("IMPORT_TOTAL:%u", total);
}

/* ======================== G0B1 → G4 命令接收 ======================== */

static int gui_read_frame(uint8_t rx[GUI_SPI_RX_BUF_SIZE])
{
    uint8_t tx[GUI_SPI_RX_BUF_SIZE];

    if (rx == NULL || !GUI_REQ_TX_ACTIVE()) return 0;

    memset(tx, 0, sizeof(tx));
    memset(rx, 0, GUI_SPI_RX_BUF_SIZE);

    if (gui_spi_mutex != NULL) osMutexAcquire(gui_spi_mutex, osWaitForever);
    if (!GUI_REQ_TX_ACTIVE()) {
        if (gui_spi_mutex != NULL) osMutexRelease(gui_spi_mutex);
        PrintDebug("[GUI] REQ_TX released before SPI read\r\n");
        return 0;
    }
    GUI_CS_LOW();
    gui_spi_delay_us(1);  /* SPI v1.6: CS low to first clock >= 1us */
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi2, tx, rx, GUI_SPI_RX_BUF_SIZE, 100);
    GUI_CS_HIGH();
    gui_spi_delay_us(100); /* SPI v1.6: >=100us between transactions */
    if (gui_spi_mutex != NULL) osMutexRelease(gui_spi_mutex);

    if (st != HAL_OK) {
        s_gui_spi_rx_err++;
        PrintDebug("[GUI] SPI2 RX err=%lu\r\n", (unsigned long)s_gui_spi_rx_err);
        return 0;
    }
    return 1;
}

static int gui_extract_line(const uint8_t *rx, uint16_t rx_len,
                            char *buf, uint16_t bufsize)
{
    uint16_t n = 0;

    if (rx == NULL || buf == NULL || bufsize == 0) return -1;
    for (uint16_t i = 0; i < rx_len; i++) {
        if (rx[i] == '\n') {
            buf[n] = '\0';
            return (int)n;
        }
        if (rx[i] == '\0') break;
        if (rx[i] != '\r' && n < bufsize - 1) buf[n++] = (char)rx[i];
    }
    buf[n] = '\0';
    return -1;
}

static int gui_parse_frame(const uint8_t *rx, HostParsed_t *out)
{
    char line[GUI_SPI_RX_BUF_SIZE];
    LineParser_t lp;
    uint16_t i;

    if (gui_extract_line(rx, GUI_SPI_RX_BUF_SIZE, line, sizeof(line)) < 0) return 0;

    LineParser_Init(&lp);
    memset(out, 0, sizeof(*out));
    for (i = 0; line[i] != '\0' && i < LINE_BUF_MAX - 1; i++) {
        LineParser_Feed(&lp, (uint8_t)line[i], out);
    }
    LineParser_Feed(&lp, '\n', out);
    return (out->cmd != HCMD_NONE && out->cmd != HCMD_UNKNOWN && out->cmd != HCMD_RAW_LINE);
}

int GUI_SPI_RecvPoll(char *buf, uint16_t bufsize)
{
    uint8_t rx[GUI_SPI_RX_BUF_SIZE];
    HostParsed_t parsed;

    if (buf == NULL || bufsize == 0) return 0;
    if (!GUI_REQ_TX_ACTIVE()) return 0;
    if (!gui_read_frame(rx)) return 0;
    if (!gui_parse_frame(rx, &parsed)) return 0;

    {
        uint16_t copy_len = parsed.raw_len;
        if (copy_len >= bufsize) copy_len = bufsize - 1;
        memcpy(buf, parsed.raw, copy_len);
        buf[copy_len] = '\0';
    }
    return 1;
}

void GUI_Poll(void)
{
    uint8_t rx[GUI_SPI_RX_BUF_SIZE];
    HostParsed_t parsed;

    if (!GUI_REQ_TX_ACTIVE()) return;
    if (!gui_read_frame(rx)) return;

    PrintDebug("[GUI] RX8: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);

    if (!gui_parse_frame(rx, &parsed)) {
        s_gui_bad_frame_cnt++;
        if (s_gui_bad_frame_cnt <= 3U || (s_gui_bad_frame_cnt % 100U) == 0U) {
            PrintDebug("[GUI] RX parse fail: no newline or unknown cmd (bad=%lu)\r\n",
                       (unsigned long)s_gui_bad_frame_cnt);
        }
        return;
    }

    PrintDebug("[GUI] RX cmd=%d raw=%.*s\r\n", (int)parsed.cmd,
               (int)parsed.raw_len, parsed.raw);

    if (parsed.cmd == HCMD_HANDSHAKE_REQ) {
        PrintDebug("[GUI] HANDSHAKE_REQ received, ACK send called\r\n");
        GUI_Send("HANDSHAKE_ACK");
        return;
    }

    if (gui_cmd_queue != NULL &&
        osMessageQueuePut(gui_cmd_queue, &parsed, 0, 10) != osOK) {
        s_gui_queue_drop_cnt++;
        PrintDebug("[GUI] cmd queue full, drop=%lu\r\n",
                   (unsigned long)s_gui_queue_drop_cnt);
    }
}

/* ======================== FreeRTOS 任务 ======================== */

void GUI_SPI_Task(void *argument)
{
    (void)argument;

    PrintDebug("[GUI] SPI task started, polling REQ_TX\r\n");

    for (;;) {
        uint8_t req_low = GUI_REQ_TX_ACTIVE() ? 1U : 0U;
        if (req_low != s_gui_req_tx_low_prev) {
            PrintDebug("[GUI] REQ_TX %s\r\n", req_low ? "LOW" : "HIGH");
            s_gui_req_tx_low_prev = req_low;
        }
        if (req_low) {
            GUI_Poll();
            osDelay(1);  /* keep reading while REQ_TX stays low */
        } else {
            osDelay(5);  /* <=10ms poll period */
        }
    }
}

/* ======================== 初始化 ======================== */

void GUI_SPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* SPI v1.6: set CS/DATA_RDY high before switching to outputs */
    HAL_GPIO_WritePin(GUI_SPI_CS_PORT, GUI_SPI_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GUI_DATA_RDY_PORT, GUI_DATA_RDY_PIN, GPIO_PIN_SET);

    /* ---- CS(PD10) 引脚：推挽输出，初始高电平（不选中） ---- */
    GPIO_InitStruct.Pin   = GUI_SPI_CS_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GUI_SPI_CS_PORT, &GPIO_InitStruct);
    GUI_CS_HIGH();

    /* ---- DATA_RDY(PD8) 引脚：推挽输出，初始高电平 ---- */
    GPIO_InitStruct.Pin   = GUI_DATA_RDY_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GUI_DATA_RDY_PORT, &GPIO_InitStruct);
    GUI_DATA_RDY_HIGH();

    /* ---- REQ_TX(PD9) 引脚：低有效输入 ---- */
    GPIO_InitStruct.Pin   = GUI_REQ_TX_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GUI_REQ_TX_PORT, &GPIO_InitStruct);

    /* ---- IRQ(PB12) 引脚：仅初始化输入，不做业务响应 ---- */
    GPIO_InitStruct.Pin   = GUI_IRQ_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GUI_IRQ_PORT, &GPIO_InitStruct);

    /* Parser state is now per-frame, no persistent cross-frame state needed */
    HAL_Delay(2000);  /* SPI v1.6: wait >=2s before first transaction */
}
