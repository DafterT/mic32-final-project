#include "game.h"
#include "mik32_hal.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_usart.h"
#include "xprintf.h"

#include <stdbool.h>
#include <stdint.h>

#define UART_BAUDRATE 115200u
#define MOVE_COUNT    3u

static void system_clock_config(void);
static void uart0_init(USART_HandleTypeDef *uart);
static void print_help(void);
static void print_init_status(GameStatus status, const GameUser *user);
static void print_save_status(GameStatus status, const GameUser *user);
static char read_command(USART_HandleTypeDef *uart);
static bool parse_move(char command, GameMove *move);
static const char *move_name(GameMove move);
static const char *result_name(GameRoundResult result);
static const char *status_name(GameStatus status);
static bool status_is_fatal(GameStatus status);
static GameStatus save_user(GameUser *user);

int main(void)
{
    static const GameUserId uart_user_id = {
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

        if (!parse_move(command, &player_move)) {
            xprintf("bad input. use r/p/s, 0/1/2, h, w, n\r\n");
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
}

void trap_handler(void)
{
    HAL_EPIC_Clear(0xFFFFFFFF);
}

static void system_clock_config(void)
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

static void uart0_init(USART_HandleTypeDef *uart)
{
    *uart = (USART_HandleTypeDef){0};
    uart->Instance = UART_0;
    uart->transmitting = Enable;
    uart->receiving = Enable;
    uart->xck_mode = XCK_Mode3;
    uart->baudrate = UART_BAUDRATE;

    HAL_USART_Init(uart);
}

static void print_help(void)
{
    xprintf("\r\nRock Paper Scissors over UART\r\n");
    xprintf("UID = UART demo user\r\n");
    xprintf("r/0 = rock, p/1 = paper, s/2 = scissors\r\n");
    xprintf("w = save snapshot, n = save and reload user\r\n");
    xprintf("h/? = help\r\n");
}

static void print_init_status(GameStatus status, const GameUser *user)
{
    xprintf("\r\ninit status=%s loaded=%u sessions=%lu rounds=%lu\r\n",
            status_name(status),
            user->loaded_from_storage ? 1u : 0u,
            (unsigned long)user->sessions_played,
            (unsigned long)user->stats.rounds);
}

static void print_save_status(GameStatus status, const GameUser *user)
{
    xprintf("save status=%s sessions=%lu dirty=%u\r\n",
            status_name(status),
            (unsigned long)user->sessions_played,
            user->dirty ? 1u : 0u);
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
