#include "mik32_hal_usart.h"
#include "mik32_hal_i2c.h"
#include "mik32_hal_irq.h"
#include "string.h"
#include "stdlib.h"
#include "queue.h"
#include "circular_buffer.h"

#define LED_PIN_NUM (7)
#define LED_PIN_PORT GPIO_2


static void SystemClock_Config();
static void USART_Init();
static void GPIO_Init();

USART_HandleTypeDef husart0;
ByteArrayQueue tx_queue;
ByteCircularBuffer rx_buffer;

int main()
{
    SystemClock_Config();
    USART_Init();
    GPIO_Init();

    tx_queue = Queue_Create(16);
    rx_buffer = ByteCircularBuffer_Create(16);

    for(uint8_t i = 60; i < 70; i++) {
        ByteCircularBuffer_Push(rx_buffer, i);
    }

    __HAL_PCC_EPIC_CLK_ENABLE();
    HAL_EPIC_MaskLevelSet(HAL_EPIC_UART_0_MASK); 
    HAL_IRQ_EnableInterrupts();
    HAL_USART_RXNE_EnableInterrupt(&husart0);
    HAL_USART_TXE_EnableInterrupt(&husart0);
    HAL_USART_TXC_EnableInterrupt(&husart0);
    HAL_USART_IDLE_EnableInterrupt(&husart0);

    while (1)
    {
       // LED_PIN_PORT->OUTPUT ^= (1 << LED_PIN_NUM);
        //HAL_USART_WriteByte(&husart0, ByteCircularBuffer_Pop(rx_buffer));
        HAL_USART_TXE_EnableInterrupt(&husart0);
        HAL_DelayMs(500);
    }
}

void trap_handler()
{
    LED_PIN_PORT->OUTPUT ^= (1 << LED_PIN_NUM);
    if(EPIC_CHECK_UART_0()) {
     /*   if (HAL_USART_RXNE_ReadFlag(&husart0)) {
            
            //ByteCircularBuffer_PushFromISR(rx_buffer, HAL_USART_ReadByte(&husart0));
            LED_PIN_PORT->OUTPUT ^= (1 << LED_PIN_NUM);
            HAL_USART_RXNE_ClearFlag(&husart0);
        }

        if (HAL_USART_TXE_ReadFlag(&husart0)) {
            HAL_USART_TXE_DisableInterrupt(&husart0);
            LED_PIN_PORT->OUTPUT ^= (1 << LED_PIN_NUM);
            HAL_USART_WriteByte(&husart0, (char)0x55);
            HAL_USART_TXE_ClearFlag(&husart0);
        } */
        HAL_USART_ClearFlags(&husart0);
    }

    HAL_EPIC_Clear(0xFFFFFFFF);
}

void SystemClock_Config(void)
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

void USART_Init()
{
    husart0.Instance = UART_0;
    husart0.transmitting = Enable;
    husart0.receiving = Enable;
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
    husart0.Interrupt.ctsie = Disable;
    husart0.Interrupt.eie = Disable;
    husart0.Interrupt.idleie = Disable;
    husart0.Interrupt.lbdie = Disable;
    husart0.Interrupt.peie = Disable;
    husart0.Interrupt.rxneie = Disable;
    husart0.Interrupt.tcie = Disable;
    husart0.Interrupt.txeie = Disable;
    husart0.Modem.rts = Disable; //out
    husart0.Modem.cts = Disable; //in
    husart0.Modem.dtr = Disable; //out
    husart0.Modem.dcd = Disable; //in
    husart0.Modem.dsr = Disable; //in
    husart0.Modem.ri = Disable;  //in
    husart0.Modem.ddis = Disable;//out
    husart0.baudrate = 115200;
    HAL_USART_Init(&husart0);
}

void GPIO_Init()
{
  /**< Включить  тактирование GPIO_0 */
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_0_M;

  /**< Включить  тактирование GPIO_1 */
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_1_M;

  /**< Включить  тактирование GPIO_2 */
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_2_M;

  /**< Включить  тактирование схемы формирования прерываний GPIO */
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_IRQ_M;

  // первая функция (порт общего назначения);
  PAD_CONFIG->PORT_0_CFG |= 0 << (LED_PIN_NUM * 2);

  // нагрузочная способность 2 мА;
  PAD_CONFIG->PORT_0_DS |= 0 << (LED_PIN_NUM * 2);

  // резисторы подтяжки отключены
  PAD_CONFIG->PORT_0_PUPD |= 0 << (LED_PIN_NUM * 2);

  // Установка направления выводов как выход.
  GPIO_2->DIRECTION_OUT = 1 << LED_PIN_NUM;

}
