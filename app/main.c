#include "mik32_hal_usart.h"
#include "mik32_hal_i2c.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_dma.h"
#include "string.h"
#include "stdlib.h"
#include "queue.h"
#include "circular_buffer.h"
#include "app_types.h"
#include "crc8_calc.h"

#define LED_PIN_NUM             (7)
#define LED_PIN_PORT            (GPIO_2)
#define TOGGLE_ONBOARD_LED      (LED_PIN_PORT->OUTPUT ^= (1 << LED_PIN_NUM))
#define BYTES_EXPECTED_TO_RECIEVE   16

static void SystemClock_Config();
static void USART_Init();
static void GPIO_Init();
static void DMA_Init(void);

static void configure_interrupts();
static void configure_mem_to_mem_dma(DMA_InitTypeDef*, DMA_ChannelHandleTypeDef*);
static void configure_mem_to_uart_dma(DMA_InitTypeDef*, DMA_ChannelHandleTypeDef*);
static void move_incoming_data_to_src_mem(ByteCircularBuffer, ByteArray*, bool*);
static void move_data_from_src_to_dst_mem(DMA_ChannelHandleTypeDef*, ByteArray*, ByteArray*, bool*);
static void transfer_data_from_dst_mem(DMA_ChannelHandleTypeDef*, USART_HandleTypeDef*, ByteArray*);

USART_HandleTypeDef husart0;
DMA_InitTypeDef hdma;
DMA_ChannelHandleTypeDef hdma_ch_mem_to_mem;
DMA_ChannelHandleTypeDef hdma_ch_mem_to_uart;

static ByteArrayQueue tx_queue;
static ByteCircularBuffer rx_buffer;

static bool data_processed = false;
static ByteArray src_mem = { .length = BYTES_EXPECTED_TO_RECIEVE };
static ByteArray dst_mem = { .length = MAX_BYTE_BUFFER_LENGTH };
static volatile ByteArray ba;

int main()
{
    SystemClock_Config();
    GPIO_Init();
    DMA_Init();
    USART_Init();

    tx_queue = Queue_Create(16);
    rx_buffer = ByteCircularBuffer_Create(16);

    configure_interrupts();
    configure_mem_to_mem_dma(&hdma, &hdma_ch_mem_to_mem);
    configure_mem_to_uart_dma(&hdma, &hdma_ch_mem_to_uart);

    while (1)
    {  
        move_incoming_data_to_src_mem(rx_buffer, &src_mem, &data_processed);

        if(data_processed) {
            move_data_from_src_to_dst_mem(&hdma_ch_mem_to_mem, &src_mem, &dst_mem, &data_processed);
            CRC8_CalculateChecksumAndAppendTo(&dst_mem);
            transfer_data_from_dst_mem(&hdma_ch_mem_to_uart, &husart0, &dst_mem);
        }       
    }
}

void trap_handler()
{
    if(EPIC_CHECK_UART_0()) {
        if (HAL_USART_RXNE_ReadFlag(&husart0)) {
            HAL_USART_RXNE_ClearFlag(&husart0);
            ByteCircularBuffer_PushFromISR(rx_buffer, HAL_USART_ReadByte(&husart0));
        }
     
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

void DMA_Init(void)
{
    hdma.Instance = DMA_CONFIG;
    hdma.CurrentValue = DMA_CURRENT_VALUE_ENABLE;
    HAL_DMA_Init(&hdma);
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

    husart0.dma_tx_request = Enable;
    husart0.dma_rx_request = Disable;

    HAL_USART_Init(&husart0);
}

void configure_interrupts() {
    __HAL_PCC_EPIC_CLK_ENABLE();
    HAL_EPIC_MaskLevelSet(HAL_EPIC_UART_0_MASK);
    HAL_USART_RXNE_EnableInterrupt(&husart0);
    HAL_IRQ_EnableInterrupts();
}

void configure_mem_to_mem_dma(DMA_InitTypeDef* hdma, DMA_ChannelHandleTypeDef* ch) {
    ch->dma = hdma;

    /* Настройки канала */
    ch->ChannelInit.Channel = DMA_CHANNEL_1;
    ch->ChannelInit.Priority = DMA_CHANNEL_PRIORITY_VERY_HIGH;

    ch->ChannelInit.ReadMode = DMA_CHANNEL_MODE_MEMORY;
    ch->ChannelInit.ReadInc = DMA_CHANNEL_INC_ENABLE;
    ch->ChannelInit.ReadSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно read_size */
    ch->ChannelInit.ReadBurstSize = 0;                /* read_burst_size должно быть кратно read_size */
    ch->ChannelInit.ReadRequest = 0;  //DMA_CHANNEL_USART_0_REQUEST;
    ch->ChannelInit.ReadAck = DMA_CHANNEL_ACK_DISABLE;

    ch->ChannelInit.WriteMode = DMA_CHANNEL_MODE_MEMORY;
    ch->ChannelInit.WriteInc = DMA_CHANNEL_INC_ENABLE;
    ch->ChannelInit.WriteSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно write_size */
    ch->ChannelInit.WriteBurstSize = 0;                /* write_burst_size должно быть кратно read_size */
    ch->ChannelInit.WriteRequest = 0; //DMA_CHANNEL_USART_0_REQUEST;
    ch->ChannelInit.WriteAck = DMA_CHANNEL_ACK_ENABLE;
}

void configure_mem_to_uart_dma(DMA_InitTypeDef* hdma, DMA_ChannelHandleTypeDef* ch) {
    ch->dma = hdma;

    /* Настройки канала */
    ch->ChannelInit.Channel = DMA_CHANNEL_0;
    ch->ChannelInit.Priority = DMA_CHANNEL_PRIORITY_VERY_HIGH;

    ch->ChannelInit.ReadMode = DMA_CHANNEL_MODE_MEMORY;
    ch->ChannelInit.ReadInc = DMA_CHANNEL_INC_ENABLE;
    ch->ChannelInit.ReadSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно read_size */
    ch->ChannelInit.ReadBurstSize = 0;                /* read_burst_size должно быть кратно read_size */
    ch->ChannelInit.ReadRequest = DMA_CHANNEL_USART_0_REQUEST;
    ch->ChannelInit.ReadAck = DMA_CHANNEL_ACK_DISABLE;

    ch->ChannelInit.WriteMode = DMA_CHANNEL_MODE_PERIPHERY;
    ch->ChannelInit.WriteInc = DMA_CHANNEL_INC_DISABLE;
    ch->ChannelInit.WriteSize = DMA_CHANNEL_SIZE_BYTE; /* data_len должно быть кратно write_size */
    ch->ChannelInit.WriteBurstSize = 0;                /* write_burst_size должно быть кратно read_size */
    ch->ChannelInit.WriteRequest = DMA_CHANNEL_USART_0_REQUEST;
    ch->ChannelInit.WriteAck = DMA_CHANNEL_ACK_ENABLE;
}

void move_incoming_data_to_src_mem(ByteCircularBuffer buffer, ByteArray* mem_block, bool* finished) {
    static uint32_t bytes_processed = 0;
    
    while( (bytes_processed < mem_block->length) && !ByteCircularBuffer_IsEmpty(buffer) ) {
        mem_block->byteArray[bytes_processed++] = ByteCircularBuffer_Pop(buffer);        
    }

    if(bytes_processed == mem_block->length) {
        *finished = true;
        bytes_processed = 0;
    }
}

void move_data_from_src_to_dst_mem(DMA_ChannelHandleTypeDef* hdma_ch, ByteArray* src, ByteArray* dst, bool* finished) {
    *finished = false;
    dst->length = src->length;
    HAL_DMA_Start(hdma_ch, src->byteArray, dst->byteArray, (src->length - 1));
}

void transfer_data_from_dst_mem(DMA_ChannelHandleTypeDef* hdma_ch, USART_HandleTypeDef* huart, ByteArray* dst_mem) {
   HAL_DMA_Start(hdma_ch, dst_mem->byteArray, (void*)&huart->Instance->TXDATA, dst_mem->length - 1);
}