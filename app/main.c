#include "mik32_hal_usart.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_dma.h"
#include "string.h"
#include "circular_buffer.h"
#include "app_types.h"
#include "crc8_calc.h"

#define LED_PIN_NUM             (7)
#define LED_PIN_PORT            (GPIO_2)

#define RX_BUFFER_CAPACITY      MAX_BYTE_BUFFER_LENGTH

#define PROTOCOL_HEADER_0       0xC0
#define PROTOCOL_HEADER_1       0xFF
#define PROTOCOL_HEADER_2       0xEE

#define MAX_FRAME_TAIL_LENGTH   0xFF
#define MAX_PAYLOAD_LENGTH      (MAX_FRAME_TAIL_LENGTH - 1)

typedef enum {
    ParserState_WaitHeader0 = 0,
    ParserState_WaitHeader1,
    ParserState_WaitHeader2,
    ParserState_ReadLength,
    ParserState_ReadPayload,
    ParserState_ReadCrc,
} ParserState;

typedef struct {
    ParserState state;
    uint8_t frame_tail_length;
    uint8_t payload_length;
    uint8_t payload_received;
    uint8_t payload_buffer[MAX_PAYLOAD_LENGTH];
} ProtocolParser;

static void SystemClock_Config(void);
static void USART_Init(void);
static void GPIO_Init(void);
static void DMA_Init(void);

static void configure_interrupts(void);
static void configure_mem_to_uart_dma(DMA_InitTypeDef* hdma, DMA_ChannelHandleTypeDef* ch);

static void drain_rx_buffer(void);
static void parser_reset(void);
static void parser_consume_byte(uint8_t byte);
static void handle_completed_frame(uint8_t received_crc);

static void service_tx_dma(void);
static void start_uart_tx_dma(ByteArray* tx_buffer);
static bool enqueue_payload_for_tx(const uint8_t* payload, uint8_t payload_length);

USART_HandleTypeDef husart0;
DMA_InitTypeDef hdma;
DMA_ChannelHandleTypeDef hdma_ch_mem_to_uart;

static ByteCircularBuffer rx_buffer;
static ProtocolParser parser;

static ByteArray tx_dma_buffer;
static ByteArray tx_pending_buffer;

static bool tx_dma_active = false;

static volatile uint32_t rx_overflow_count = 0;
static volatile uint32_t bad_crc_count = 0;
static volatile uint32_t bad_length_count = 0;
static volatile uint32_t valid_packet_count = 0;
static volatile uint32_t tx_overflow_count = 0;

int main()
{
    SystemClock_Config();
    GPIO_Init();
    DMA_Init();
    USART_Init();

    rx_buffer = ByteCircularBuffer_Create(RX_BUFFER_CAPACITY);
    parser_reset();

    configure_interrupts();
    configure_mem_to_uart_dma(&hdma, &hdma_ch_mem_to_uart);

    while (1)
    {
        service_tx_dma();
        drain_rx_buffer();
    }
}

void trap_handler()
{
    if (EPIC_CHECK_UART_0()) {
        if (HAL_USART_RXNE_ReadFlag(&husart0)) {
            if (!ByteCircularBuffer_PushFromISR(rx_buffer, HAL_USART_ReadByte(&husart0))) {
                rx_overflow_count++;
            }
        }
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

static void GPIO_Init(void)
{
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_0_M;
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_1_M;
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_2_M;
  PM->CLK_APB_P_SET |= PM_CLOCK_APB_P_GPIO_IRQ_M;

  PAD_CONFIG->PORT_0_CFG |= 0 << (LED_PIN_NUM * 2);
  PAD_CONFIG->PORT_0_DS |= 0 << (LED_PIN_NUM * 2);
  PAD_CONFIG->PORT_0_PUPD |= 0 << (LED_PIN_NUM * 2);

  LED_PIN_PORT->DIRECTION_OUT = 1 << LED_PIN_NUM;
}

static void DMA_Init(void)
{
    hdma.Instance = DMA_CONFIG;
    hdma.CurrentValue = DMA_CURRENT_VALUE_ENABLE;
    HAL_DMA_Init(&hdma);
}

static void USART_Init(void)
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

static void configure_interrupts(void)
{
    __HAL_PCC_EPIC_CLK_ENABLE();
    HAL_EPIC_MaskLevelSet(HAL_EPIC_UART_0_MASK);
    HAL_USART_RXNE_EnableInterrupt(&husart0);
    HAL_IRQ_EnableInterrupts();
}

static void configure_mem_to_uart_dma(DMA_InitTypeDef* hdma, DMA_ChannelHandleTypeDef* ch)
{
    ch->dma = hdma;

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

static void drain_rx_buffer(void)
{
    while (!ByteCircularBuffer_IsEmpty(rx_buffer)) {
        parser_consume_byte(ByteCircularBuffer_Pop(rx_buffer));
        service_tx_dma();
    }
}

static void parser_reset(void)
{
    parser.state = ParserState_WaitHeader0;
    parser.frame_tail_length = 0;
    parser.payload_length = 0;
    parser.payload_received = 0;
}

static void parser_consume_byte(uint8_t byte)
{
    switch (parser.state) {
        case ParserState_WaitHeader0:
            if (byte == PROTOCOL_HEADER_0) {
                parser.state = ParserState_WaitHeader1;
            }
            break;

        case ParserState_WaitHeader1:
            if (byte == PROTOCOL_HEADER_1) {
                parser.state = ParserState_WaitHeader2;
            } else if (byte == PROTOCOL_HEADER_0) {
                parser.state = ParserState_WaitHeader1;
            } else {
                parser.state = ParserState_WaitHeader0;
            }
            break;

        case ParserState_WaitHeader2:
            if (byte == PROTOCOL_HEADER_2) {
                parser.state = ParserState_ReadLength;
            } else if (byte == PROTOCOL_HEADER_0) {
                parser.state = ParserState_WaitHeader1;
            } else {
                parser.state = ParserState_WaitHeader0;
            }
            break;

        case ParserState_ReadLength:
            parser.frame_tail_length = byte;

            if (parser.frame_tail_length == 0) {
                bad_length_count++;
                parser_reset();
                break;
            }

            /* Protocol length includes the trailing CRC byte. */
            parser.payload_length = parser.frame_tail_length - 1;
            parser.payload_received = 0;

            if (parser.payload_length == 0) {
                parser.state = ParserState_ReadCrc;
            } else {
                parser.state = ParserState_ReadPayload;
            }
            break;

        case ParserState_ReadPayload:
            parser.payload_buffer[parser.payload_received++] = byte;

            if (parser.payload_received == parser.payload_length) {
                parser.state = ParserState_ReadCrc;
            }
            break;

        case ParserState_ReadCrc:
            handle_completed_frame(byte);
            parser_reset();
            break;
    }
}

static void handle_completed_frame(uint8_t received_crc)
{
    uint8_t actual_crc = CRC8_CalculateChecksumOf(parser.payload_buffer, parser.payload_length);

    if (actual_crc != received_crc) {
        bad_crc_count++;
        return;
    }

    valid_packet_count++;
    (void)enqueue_payload_for_tx(parser.payload_buffer, parser.payload_length);
}

static void service_tx_dma(void)
{
    if (tx_dma_active && HAL_DMA_GetChannelReadyStatus(&hdma_ch_mem_to_uart)) {
        tx_dma_active = false;
        tx_dma_buffer.length = 0;
    }

    if (tx_dma_active || tx_pending_buffer.length == 0) {
        return;
    }

    memcpy(tx_dma_buffer.byteArray, tx_pending_buffer.byteArray, tx_pending_buffer.length);
    tx_dma_buffer.length = tx_pending_buffer.length;
    tx_pending_buffer.length = 0;

    start_uart_tx_dma(&tx_dma_buffer);
    tx_dma_active = true;
}

static void start_uart_tx_dma(ByteArray* tx_buffer)
{
    if (tx_buffer->length == 0) {
        return;
    }

    HAL_DMA_Start(
        &hdma_ch_mem_to_uart,
        tx_buffer->byteArray,
        (void*)&husart0.Instance->TXDATA,
        tx_buffer->length - 1
    );
}

static bool enqueue_payload_for_tx(const uint8_t* payload, uint8_t payload_length)
{
    uint16_t pending_length;

    if (payload_length == 0) {
        return true;
    }

    service_tx_dma();

    if (!tx_dma_active && tx_pending_buffer.length == 0) {
        memcpy(tx_dma_buffer.byteArray, payload, payload_length);
        tx_dma_buffer.length = payload_length;

        start_uart_tx_dma(&tx_dma_buffer);
        tx_dma_active = true;
        return true;
    }

    pending_length = tx_pending_buffer.length;
    
    if ((pending_length + payload_length) > MAX_BYTE_BUFFER_LENGTH) {
        tx_overflow_count++;
        return false;
    }

    memcpy(&tx_pending_buffer.byteArray[tx_pending_buffer.length], payload, payload_length);
    tx_pending_buffer.length += payload_length;

    return true;
}
