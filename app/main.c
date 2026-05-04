#include "mik32_hal.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_usart.h"
#include "xprintf.h"

#define UART_BAUDRATE      115200U
#define PRINT_PERIOD_MS    5000U

static void SystemClock_Config(void);
static void USART0_Init(void);
static void Interrupts_Init(void);

static USART_HandleTypeDef husart0;

int main(void)
{
    SystemClock_Config();
    HAL_Init();
    USART0_Init();
    Interrupts_Init();

    while (1) {
        xprintf("hello world\r\n");
        HAL_DelayMs(PRINT_PERIOD_MS);
    }
}

void trap_handler(void)
{
    if (EPIC_CHECK_UART_0()) {
        /* Future UART interrupt handling goes here. */
    }

    HAL_EPIC_Clear(0xFFFFFFFF);
}

static void SystemClock_Config(void)
{
    PCC_InitTypeDef PCC_OscInit = {0};

    PCC_OscInit.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;
    PCC_OscInit.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;
    PCC_OscInit.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;
    PCC_OscInit.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;
    PCC_OscInit.AHBDivider = 0;
    PCC_OscInit.APBMDivider = 0;
    PCC_OscInit.APBPDivider = 0;
    PCC_OscInit.HSI32MCalibrationValue = 128;
    PCC_OscInit.LSI32KCalibrationValue = 8;
    PCC_OscInit.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    PCC_OscInit.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;

    HAL_PCC_Config(&PCC_OscInit);
}

static void USART0_Init(void)
{
    husart0.Instance = UART_0;
    husart0.transmitting = Enable;
    husart0.receiving = Disable;
    husart0.frame = Frame_8bit;
    husart0.parity_bit = Disable;
    husart0.parity_bit_inversion = Disable;
    husart0.bit_direction = LSB_First;
    husart0.data_inversion = Disable;
    husart0.tx_inversion = Disable;
    husart0.rx_inversion = Disable;
    husart0.swap = Disable;
    husart0.lbm = Disable;
    husart0.stop_bit = StopBit_1;
    husart0.mode = Asynchronous_Mode;
    husart0.xck_mode = XCK_Mode3;
    husart0.last_byte_clock = Disable;
    husart0.overwrite = Disable;
    husart0.rts_mode = AlwaysEnable_mode;
    husart0.dma_tx_request = Disable;
    husart0.dma_rx_request = Disable;
    husart0.channel_mode = Duplex_Mode;
    husart0.tx_break_mode = Disable;
    husart0.Interrupt.peie = Disable;
    husart0.Interrupt.txeie = Disable;
    husart0.Interrupt.tcie = Disable;
    husart0.Interrupt.rxneie = Disable;
    husart0.Interrupt.idleie = Disable;
    husart0.Interrupt.lbdie = Disable;
    husart0.Interrupt.ctsie = Disable;
    husart0.Interrupt.eie = Disable;
    husart0.Modem.rts = Disable;
    husart0.Modem.cts = Disable;
    husart0.Modem.ri = Disable;
    husart0.Modem.dsr = Disable;
    husart0.Modem.dtr = Disable;
    husart0.Modem.ddis = Disable;
    husart0.Modem.dcd = Disable;
    husart0.baudrate = UART_BAUDRATE;

    HAL_USART_Init(&husart0);
}

static void Interrupts_Init(void)
{
    __HAL_PCC_EPIC_CLK_ENABLE();
    HAL_EPIC_Clear(0xFFFFFFFF);
    HAL_IRQ_EnableInterrupts();
}
