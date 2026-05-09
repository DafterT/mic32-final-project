#include "game.h"
#include "mik32_hal.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_scr1_timer.h"
#include "mik32_hal_spi.h"
#include "mik32_hal_usart.h"
#include "mfrc522.h"
#include "rfid_config.h"
#include "xprintf.h"

#include <stdbool.h>
#include <stdint.h>

#define UART_BAUDRATE 115200u
#define MOVE_COUNT    3u

USART_HandleTypeDef husart0;
SPI_HandleTypeDef hspi0;
MFRC522 mfrc522;

static void SystemClock_Config(void);
static void USART0_Init(void);
static void SPI0_Init(void);
static void RFID_MonitorLoop(void);
static void print_rfid_uid(const Uid *uid);
#if 0
/* Старые UART game helpers оставлены для последующего объединения с RFID UID. */
static void print_help(void);
static void print_init_status(GameStatus status, const GameUser *user);
static void print_save_status(GameStatus status, const GameUser *user);
static void print_user_id(const GameUserId *id);
static char read_command(USART_HandleTypeDef *uart);
static bool parse_move(char command, GameMove *move);
static const char *move_name(GameMove move);
static const char *result_name(GameRoundResult result);
static const char *status_name(GameStatus status);
static bool status_is_fatal(GameStatus status);
static GameStatus save_user(GameUser *user);
#endif

uint32_t HAL_Micros(void)
{
    return HAL_Time_SCR1TIM_Micros();
}

uint32_t HAL_Millis(void)
{
    return HAL_Time_SCR1TIM_Millis();
}

void HAL_DelayUs(uint32_t time_us)
{
    HAL_Time_SCR1TIM_DelayUs(time_us);
}

void HAL_DelayMs(uint32_t time_ms)
{
    HAL_Time_SCR1TIM_DelayMs(time_ms);
}

int main(void)
{
    SystemClock_Config();
    HAL_Init();
    HAL_Time_SCR1TIM_Init();
    USART0_Init();
    SPI0_Init();

    MFRC522_Init(&mfrc522, &hspi0, RFID_CS_PORT, RFID_CS_PIN, RFID_RST_PORT, RFID_RST_PIN);
    PCD_Init(&mfrc522);

    xprintf("\r\nRFID monitor over UART\r\n");
    xprintf("Поднесите RFID метку к считывателю...\r\n");

    RFID_MonitorLoop();

#if 0
    /* Старый UART game loop оставлен для последующего объединения с RFID UID. */
    static GameUserId uart_user_id = {
        .bytes = { 'U', 'A', 'R', 'T' },
        .length = 4u
    };
    USART_HandleTypeDef uart0;
    GameUser user = {0};
    GameStatus status;

    system_clock_config();
    HAL_Init();
    uart0_init(&uart0);

    status = game_init_user(&uart_user_id, &user);
    print_init_status(status, &user);
    if (status_is_fatal(status)) {
        while (1) {
        }
    }

    print_help();

    while (1) {
        char command;
        GameMove player_move;
        GameRound round;

        xprintf("\r\nmove> ");
        command = read_command(&uart0);
        xprintf("%c\r\n", command);

        if ((command == 'h') || (command == 'H') || (command == '?')) {
            print_help();
            continue;
        }

        if ((command == 'w') || (command == 'W')) {
            (void)save_user(&user);
            continue;
        }

        if ((command == 'n') || (command == 'N')) {
            status = save_user(&user);
            if (status_is_fatal(status)) {
                continue;
            }

            status = game_init_user(&uart_user_id, &user);
            print_init_status(status, &user);
            continue;
        }

        if ((command == 'u') || (command == 'U')) {
            char id_suffix;

            status = save_user(&user);
            if (status_is_fatal(status)) {
                continue;
            }

            xprintf("uid char> ");
            id_suffix = read_command(&uart0);
            xprintf("%c\r\n", id_suffix);

            uart_user_id.bytes[uart_user_id.length - 1u] = (uint8_t)id_suffix;
            status = game_init_user(&uart_user_id, &user);
            print_init_status(status, &user);
            continue;
        }

        if (!parse_move(command, &player_move)) {
            xprintf("bad input. use r/p/s, 0/1/2, h, w, n, u\r\n");
            continue;
        }

        round = game_stage(&user, player_move);

        xprintf("you=%s mcu=%s result=%s\r\n",
                move_name(player_move),
                move_name(round.device_move),
                result_name(round.result));
        xprintf("stats W/D/L: %lu/%lu/%lu rounds=%lu sessions=%lu dirty=%u\r\n",
                (unsigned long)user.stats.wins,
                (unsigned long)user.stats.draws,
                (unsigned long)user.stats.losses,
                (unsigned long)user.stats.rounds,
                (unsigned long)user.sessions_played,
                user.dirty ? 1u : 0u);
    }
#endif

    return 0;
}

void trap_handler(void)
{
    HAL_EPIC_Clear(0xFFFFFFFF);
}

static void SystemClock_Config(void)
{
    PCC_InitTypeDef pcc = {0};

    pcc.OscillatorEnable = PCC_OSCILLATORTYPE_ALL;
    pcc.FreqMon.OscillatorSystem = PCC_OSCILLATORTYPE_OSC32M;
    pcc.FreqMon.ForceOscSys = PCC_FORCE_OSC_SYS_UNFIXED;
    pcc.FreqMon.Force32KClk = PCC_FREQ_MONITOR_SOURCE_OSC32K;
    pcc.AHBDivider = 0;
    pcc.APBMDivider = 0;
    pcc.APBPDivider = 0;
    pcc.HSI32MCalibrationValue = 128;
    pcc.LSI32KCalibrationValue = 8;
    pcc.RTCClockSelection = PCC_RTC_CLOCK_SOURCE_AUTO;
    pcc.RTCClockCPUSelection = PCC_CPU_RTC_CLOCK_SOURCE_OSC32K;

    HAL_PCC_Config(&pcc);
}

static void USART0_Init(void)
{
    husart0 = (USART_HandleTypeDef){0};
    husart0.Instance = UART_0;
    husart0.transmitting = Enable;
    husart0.receiving = Enable;
    husart0.xck_mode = XCK_Mode3;
    husart0.baudrate = UART_BAUDRATE;

    HAL_USART_Init(&husart0);
}

static void SPI0_Init(void)
{
    hspi0 = (SPI_HandleTypeDef){0};
    hspi0.Instance = RFID_SPI_INSTANCE;
    hspi0.Init.SPI_Mode = HAL_SPI_MODE_MASTER;
    hspi0.Init.CLKPhase = RFID_SPI_PHASE;
    hspi0.Init.CLKPolarity = RFID_SPI_POLARITY;
    hspi0.Init.ThresholdTX = 1;
    hspi0.Init.BaudRateDiv = RFID_SPI_BAUDRATE_DIV;
    hspi0.Init.Decoder = SPI_DECODER_NONE;
    hspi0.Init.ManualCS = SPI_MANUALCS_ON;
    hspi0.Init.ChipSelect = SPI_CS_NONE;

    if (HAL_SPI_Init(&hspi0) != HAL_OK) {
        xprintf("SPI_Init_Error\r\n");
    }
}

static void RFID_MonitorLoop(void)
{
    while (1) {
        if (!PICC_IsNewCardPresent(&mfrc522)) {
            HAL_ProgramDelayMs(RFID_POLL_DELAY_MS);
            continue;
        }

        if (!PICC_ReadCardSerial(&mfrc522)) {
            HAL_ProgramDelayMs(RFID_POLL_DELAY_MS);
            continue;
        }

        xprintf("RFID с UID ");
        print_rfid_uid(&mfrc522.uid);
        xprintf(" поднесли\r\n");

        (void)PICC_HaltA(&mfrc522);
        HAL_ProgramDelayMs(RFID_POLL_DELAY_MS);
    }
}

static void print_rfid_uid(const Uid *uid)
{
    byte index;

    for (index = 0; index < uid->size; ++index) {
        if (index != 0) {
            xprintf(" ");
        }
        xprintf("%02X", uid->uidByte[index]);
    }
}

#if 0
/* Старые UART game helpers оставлены для последующего объединения с RFID UID. */
static void print_help(void)
{
    xprintf("\r\nRock Paper Scissors over UART\r\n");
    xprintf("UID = UART demo user\r\n");
    xprintf("r/0 = rock, p/1 = paper, s/2 = scissors\r\n");
    xprintf("w = save snapshot, n = save and reload user\r\n");
    xprintf("uX = save and switch UID to UARX\r\n");
    xprintf("h/? = help\r\n");
}

static void print_init_status(GameStatus status, const GameUser *user)
{
    xprintf("\r\ninit status=%s uid=", status_name(status));
    print_user_id(&user->id);
    xprintf(" loaded=%u sessions=%lu rounds=%lu\r\n",
            user->loaded_from_storage ? 1u : 0u,
            (unsigned long)user->sessions_played,
            (unsigned long)user->stats.rounds);
}

static void print_save_status(GameStatus status, const GameUser *user)
{
    if ((status == GAME_STATUS_CRC_ERROR) && !user->dirty) {
        xprintf("save status=ok warning=crc_reused uid=");
    } else if ((status == GAME_STATUS_STORAGE_FULL) && !user->dirty) {
        xprintf("save status=ok warning=evicted_old_user uid=");
    } else {
        xprintf("save status=%s uid=", status_name(status));
    }
    print_user_id(&user->id);
    xprintf(" sessions=%lu dirty=%u\r\n",
            (unsigned long)user->sessions_played,
            user->dirty ? 1u : 0u);
}

static void print_user_id(const GameUserId *id)
{
    uint8_t index;

    for (index = 0u; index < id->length; ++index) {
        xprintf("%c", id->bytes[index]);
    }
}

static char read_command(USART_HandleTypeDef *uart)
{
    char command;

    do {
        while (!HAL_USART_RXNE_ReadFlag(uart)) {
        }

        command = HAL_USART_ReadByte(uart);
        HAL_USART_RXNE_ClearFlag(uart);
    } while ((command == '\r') || (command == '\n') || (command == ' '));

    return command;
}

static bool parse_move(char command, GameMove *move)
{
    switch (command) {
    case 'r':
    case 'R':
    case '0':
        *move = GAME_MOVE_ROCK;
        return true;
    case 'p':
    case 'P':
    case '1':
        *move = GAME_MOVE_PAPER;
        return true;
    case 's':
    case 'S':
    case '2':
        *move = GAME_MOVE_SCISSORS;
        return true;
    default:
        return false;
    }
}

static const char *move_name(GameMove move)
{
    static const char *const names[MOVE_COUNT] = {
        "rock",
        "paper",
        "scissors"
    };

    if ((uint8_t)move >= MOVE_COUNT) {
        return "unknown";
    }

    return names[(uint8_t)move];
}

static const char *result_name(GameRoundResult result)
{
    static const char *const names[MOVE_COUNT] = {
        "loss",
        "draw",
        "win"
    };

    if ((uint8_t)result >= MOVE_COUNT) {
        return "unknown";
    }

    return names[(uint8_t)result];
}

static const char *status_name(GameStatus status)
{
    switch (status) {
    case GAME_STATUS_OK:
        return "ok";
    case GAME_STATUS_NOT_FOUND:
        return "not_found";
    case GAME_STATUS_INVALID_ARG:
        return "invalid_arg";
    case GAME_STATUS_STORAGE_FULL:
        return "storage_full";
    case GAME_STATUS_STORAGE_ERROR:
        return "storage_error";
    case GAME_STATUS_CRC_ERROR:
        return "crc_error";
    default:
        return "unknown";
    }
}

static bool status_is_fatal(GameStatus status)
{
    return (status == GAME_STATUS_INVALID_ARG) || (status == GAME_STATUS_STORAGE_ERROR);
}

static GameStatus save_user(GameUser *user)
{
    GameStatus status = game_save_user(user);

    print_save_status(status, user);
    return status;
}
#endif
