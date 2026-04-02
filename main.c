#include "main.h"
#include "stm32f1xx_hal.h"
#include <string.h>
#include "sniff_replies.h"

/* =========================================================
   Private variables
   ========================================================= */
I2C_HandleTypeDef hi2c1;

/* =========================================================
   Private function prototypes
   ========================================================= */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void sony_emul_init(void);
static void sony_emul_task(void);
static void sony_emul_hard_restart(void);
static void sony_watch_resn(void);
static void build_reply(void);

/* =========================================================
   USER PINS
   ========================================================= */
#define PIN_RESN_PORT GPIOA
#define PIN_RESN_PIN  GPIO_PIN_0     // PA0 RESN from Sony -> input

#define PIN_LINK_PORT GPIOA
#define PIN_LINK_PIN  GPIO_PIN_1     // PA1 LINK to Sony -> output

#define PIN_INT_PORT  GPIOA
#define PIN_INT_PIN   GPIO_PIN_2     // PA2 INT to Sony -> output

/* =========================================================
   I2C ADDRESSES (7-bit)
   ========================================================= */
#define ADDR40 0x40
#define ADDR41 0x41

/* =========================================================
   Register model / I2C state
   ========================================================= */
static uint8_t reg40[256];
static uint8_t reg41[256];

static volatile uint8_t  cur_addr7    = 0;
static volatile uint8_t  have_ptr     = 0;
static volatile uint8_t  write_active = 0;
static volatile uint8_t  ptr          = 0;

static uint8_t  rx_byte = 0;
static uint8_t  tx_buf[32];
static uint16_t reply_len = 0;

/* =========================================================
   Observed-sequence tracking
   ========================================================= */
static volatile uint8_t  seen_any_41      = 0;
static volatile uint8_t  seen_write46     = 0;
static volatile uint8_t  seen_bigblock50  = 0;
static volatile uint8_t  seen_write16     = 0;
static volatile uint8_t  seen_write10_85  = 0;
static volatile uint8_t  seen_read74      = 0;
static volatile uint32_t status_polls     = 0;
static volatile uint16_t big50_count      = 0;

/* =========================================================
   LINK / INT state
   ========================================================= */
static volatile uint8_t link_latched_high = 0;
static volatile uint8_t int_state_high    = 1;

/* =========================================================
   Smart reply state
   ========================================================= */
static uint32_t seq13 = 0;
static uint32_t seq41 = 0;
static uint32_t seq50 = 0;
static uint32_t seq74 = 0;

/* =========================================================
   Timing / reset tracking
   ========================================================= */
static uint32_t last_int_toggle_ms = 0;
static uint32_t last_link_high_ms  = 0;

static volatile uint8_t reset_pending = 0;
static uint8_t last_resn = 1;

/* =========================================================
   Helpers
   ========================================================= */
static inline uint8_t resn_high(void)
{
    return (HAL_GPIO_ReadPin(PIN_RESN_PORT, PIN_RESN_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

static inline void link_force_low(void)
{
    link_latched_high = 0;
    HAL_GPIO_WritePin(PIN_LINK_PORT, PIN_LINK_PIN, GPIO_PIN_RESET);
}

static inline void link_latch_high(void)
{
    link_latched_high = 1;
    HAL_GPIO_WritePin(PIN_LINK_PORT, PIN_LINK_PIN, GPIO_PIN_SET);
}

static inline void int_force_low(void)
{
    int_state_high = 0;
    HAL_GPIO_WritePin(PIN_INT_PORT, PIN_INT_PIN, GPIO_PIN_RESET);
}

static inline void int_force_high(void)
{
    int_state_high = 1;
    HAL_GPIO_WritePin(PIN_INT_PORT, PIN_INT_PIN, GPIO_PIN_SET);
}

static inline uint8_t *bank(void)
{
    return (cur_addr7 == ADDR41) ? reg41 : reg40;
}

static inline void reset_runtime_state(void)
{
    cur_addr7         = 0;
    have_ptr          = 0;
    write_active      = 0;
    ptr               = 0;

    rx_byte           = 0;
    reply_len         = 0;

    seq13             = 0;
    seq41             = 0;
    seq50             = 0;
    seq74             = 0;

    seen_any_41       = 0;
    seen_write46      = 0;
    seen_bigblock50   = 0;
    seen_write16      = 0;
    seen_write10_85   = 0;
    seen_read74       = 0;
    status_polls      = 0;

    big50_count       = 0;

    last_int_toggle_ms = HAL_GetTick();
    last_link_high_ms  = 0;
}

static inline uint8_t linked_ready(void)
{
    return link_latched_high;
}

/* =========================================================
   Smart reply generators
   ========================================================= */

/* ptr 0x13:
   replay captured 2-byte stream
*/
static void smart_reply_ptr13(void)
{
    memcpy(tx_buf,
           reply_40_ptr13[seq13 % (sizeof(reply_40_ptr13) / sizeof(reply_40_ptr13[0]))],
           2);
    reply_len = 2;
    seq13++;
}

/* ptr 0x41:
   before link: stable 04 F0
   after link: replay captured stream
*/
static void smart_reply_ptr41(void)
{
    if (!linked_ready()) {
        tx_buf[0] = 0x04;
        tx_buf[1] = 0xF0;
    } else {
        memcpy(tx_buf,
               reply_40_ptr41[seq41 % (sizeof(reply_40_ptr41) / sizeof(reply_40_ptr41[0]))],
               2);
        seq41++;
    }
    reply_len = 2;
}

/* ptr 0x74:
   before link: 00 00
   after link: mostly 03 00, occasional 02 00
*/
static void smart_reply_ptr74(void)
{
    seen_read74 = 1;

    if (!linked_ready()) {
        tx_buf[0] = 0x00;
        tx_buf[1] = 0x00;
    } else {
        tx_buf[0] = ((seq74 & 0x07u) == 0u) ? 0x02 : 0x03;
        tx_buf[1] = 0x00;
    }

    reply_len = 2;
    seq74++;
}

/* ptr 0x50:
   before link: first captured frame
   after link: replay captured frames
*/
static void smart_reply_ptr50(void)
{
    if (!linked_ready()) {
        memcpy(tx_buf, reply_40_ptr50[0], 17);
    } else {
        memcpy(tx_buf,
               reply_40_ptr50[seq50 % (sizeof(reply_40_ptr50) / sizeof(reply_40_ptr50[0]))],
               17);
        seq50++;
    }

    reply_len = 17;
}

/* =========================================================
   Reply engine
   ========================================================= */
static void build_reply(void)
{
    uint8_t p = ptr;

    tx_buf[0] = 0x00;
    tx_buf[1] = 0x00;
    reply_len = 2;

    if (cur_addr7 == ADDR40) {
        switch (p) {
        case 0x13:
            smart_reply_ptr13();
            return;

        case 0x41:
            status_polls++;
            smart_reply_ptr41();
            return;

        case 0x50:
            status_polls++;
            smart_reply_ptr50();
            return;

        case 0x74:
            status_polls++;
            smart_reply_ptr74();
            return;

        default:
            tx_buf[0] = reg40[p];
            tx_buf[1] = reg40[(uint8_t)(p + 1)];
            reply_len = 2;
            return;
        }
    }

    if (cur_addr7 == ADDR41) {
        uint8_t pp = p;
        for (uint8_t i = 0; i < sizeof(tx_buf); i++) {
            tx_buf[i] = reg41[pp++];
        }
        reply_len = sizeof(tx_buf);
    }
}

/* =========================================================
   RESN watch / hard restart
   ========================================================= */
static void sony_watch_resn(void)
{
    uint8_t now_resn = resn_high();

    if (now_resn != last_resn) {
        last_resn = now_resn;
        reset_pending = 1;
    }
}

static void sony_emul_hard_restart(void)
{
    __disable_irq();

    link_force_low();
    int_force_high();
    reset_runtime_state();

    __enable_irq();

    HAL_I2C_DeInit(&hi2c1);
    MX_I2C1_Init();

    __HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_BERR);
    __HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_ARLO);
    __HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_OVR);
    __HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_AF);

    HAL_I2C_EnableListen_IT(&hi2c1);
}

/* =========================================================
   HAL callbacks
   ========================================================= */
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c,
                          uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
    (void)hi2c;

    if (!resn_high()) {
        write_active = 0;
        have_ptr = 0;
        HAL_I2C_EnableListen_IT(&hi2c1);
        return;
    }

    cur_addr7    = (uint8_t)(AddrMatchCode >> 1);
    have_ptr     = 0;
    write_active = 0;

    if (TransferDirection == I2C_DIRECTION_TRANSMIT) {
        write_active = 1;
        HAL_I2C_Slave_Seq_Receive_IT(&hi2c1, &rx_byte, 1, I2C_FIRST_FRAME);
        return;
    }

    build_reply();
    HAL_I2C_Slave_Seq_Transmit_IT(&hi2c1, tx_buf, reply_len, I2C_FIRST_FRAME);
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    (void)hi2c;

    if (!write_active) return;
    if (!resn_high())  return;

    if (!have_ptr) {
        ptr = rx_byte;
        have_ptr = 1;
        HAL_I2C_Slave_Seq_Receive_IT(&hi2c1, &rx_byte, 1, I2C_NEXT_FRAME);
        return;
    }

    {
        uint8_t current_ptr = ptr;
        uint8_t *b = bank();
        b[current_ptr] = rx_byte;

        if (cur_addr7 == ADDR41) {
            seen_any_41 = 1;
        }

        if (cur_addr7 == ADDR40 && current_ptr == 0x46 && rx_byte == 0x00) {
            seen_write46 = 1;
        }

        if (cur_addr7 == ADDR40 && current_ptr == 0x16) {
            seen_write16 = 1;
        }

        if (cur_addr7 == ADDR40 && current_ptr == 0x10 && rx_byte == 0x85) {
            seen_write10_85 = 1;
        }

        if (cur_addr7 == ADDR40) {
            if (current_ptr == 0x50) {
                big50_count = 1;
            } else if (current_ptr > 0x50 && big50_count != 0) {
                big50_count++;
            } else {
                big50_count = 0;
            }

            if (big50_count >= 8) {
                seen_bigblock50 = 1;
            }
        }

        ptr = (uint8_t)(current_ptr + 1);
    }

    HAL_I2C_Slave_Seq_Receive_IT(&hi2c1, &rx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_SlaveListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    (void)hi2c;
    write_active = 0;
    have_ptr = 0;
    HAL_I2C_EnableListen_IT(&hi2c1);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    __HAL_I2C_DISABLE(hi2c);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_BERR);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_ARLO);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_OVR);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
    __HAL_I2C_ENABLE(hi2c);

    write_active = 0;
    have_ptr = 0;

    HAL_I2C_EnableListen_IT(hi2c);
}

/* =========================================================
   Emulator init / task
   ========================================================= */
static void sony_emul_init(void)
{
    memset(reg40, 0x00, sizeof(reg40));
    memset(reg41, 0x00, sizeof(reg41));

    link_force_low();
    int_force_high();
    reset_runtime_state();

    last_resn = resn_high();
    reset_pending = 0;

    HAL_I2C_EnableListen_IT(&hi2c1);
}

static void sony_emul_task(void)
{
    uint32_t now = HAL_GetTick();

    sony_watch_resn();

    if (reset_pending) {
        reset_pending = 0;
        sony_emul_hard_restart();
        return;
    }

    if (!resn_high()) {
        link_force_low();
        int_force_high();
        return;
    }

    /* raise LINK once Sony is clearly in normal polling loop */
    if (!linked_ready() &&
        seen_any_41 &&
        seen_read74 &&
        status_polls >= 4) {
        link_latch_high();
        int_force_high();
        last_link_high_ms  = now;
        last_int_toggle_ms = now;
    }

    if (!linked_ready()) {
        return;
    }

    /* keep INT mostly high, short occasional low pulse */
    if (int_state_high) {
        if ((now - last_int_toggle_ms) >= 20u) {
            int_force_low();
            last_int_toggle_ms = now;
        }
    } else {
        if ((now - last_int_toggle_ms) >= 2u) {
            int_force_high();
            last_int_toggle_ms = now;
        }
    }
}

/* =========================================================
   MAIN
   ========================================================= */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_I2C1_Init();

    sony_emul_init();

    while (1) {
        sony_emul_task();
    }
}

/* =========================================================
   CLOCK / INIT
   ========================================================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL5;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.OwnAddress1 = (ADDR40 << 1);
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_ENABLE;
    hi2c1.Init.OwnAddress2 = (ADDR41 << 1);
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_I2C_EnableListen_IT(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(PIN_LINK_PORT, PIN_LINK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_INT_PORT,  PIN_INT_PIN,  GPIO_PIN_SET);

       /* RESN from Sony */
    GPIO_InitStruct.Pin = PIN_RESN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;//GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(PIN_RESN_PORT, &GPIO_InitStruct);

    /* LINK to Sony */
    GPIO_InitStruct.Pin = PIN_LINK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;//GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIN_LINK_PORT, &GPIO_InitStruct);

    /* INT to Sony */
    GPIO_InitStruct.Pin = PIN_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;//GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIN_INT_PORT, &GPIO_InitStruct);
}

void I2C1_EV_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void)
{
    HAL_I2C_ER_IRQHandler(&hi2c1);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}





