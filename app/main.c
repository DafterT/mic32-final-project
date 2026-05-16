#include "buttons.h"
#include "game.h"
#include "mik32_hal.h"
#include "mik32_hal_i2c.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_scr1_timer.h"
#include "mik32_hal_spi.h"
#include "mik32_hal_usart.h"
#include "lcd_driver.h"
#include "mfrc522.h"
#include "rfid_config.h"
#include "xprintf.h"

#include <stdbool.h>
#include <stdint.h>

#define UART_BAUDRATE 115200u
#define MOVE_COUNT    3u

I2C_HandleTypeDef hi2c;

static SPI_HandleTypeDef hspi0;
static MFRC522 mfrc522;

typedef struct {
    GameUser user;
    GameUserId card_id;
    bool user_active;
    bool menu_prompt_printed;
    uint32_t next_rfid_poll_ms;
} AppContext;

static bool app_poll_rfid(AppContext *context, uint32_t now);
static void app_handle_card(AppContext *context, const GameUserId *card_id);
static bool app_activate_user(AppContext *context, const GameUserId *card_id);
static bool app_save_active_user(AppContext *context);
static void app_enter_menu(AppContext *context);
static void app_enter_game(AppContext *context);
static void app_handle_move(AppContext *context, GameMove player_move);
static void print_round_result(GameMove player_move, const GameRound *round);
static void print_user_stats(const GameUser *user);
static void SystemClock_Config(void);
static void USART0_Init(void);
static void I2C1_Init(void);
static void SPI0_Init(void);
static bool read_rfid_card_once(GameUserId *id);
static void copy_rfid_uid_to_user_id(const Uid *uid, GameUserId *id);
static bool same_user_id(const GameUserId *left, const GameUserId *right);
static void print_help(void);
static void print_init_status(GameStatus status, const GameUser *user);
static void print_save_status(GameStatus status, const GameUser *user);
static void print_user_id(const GameUserId *id);
static const char *move_name(GameMove move);
static const char *result_name(GameRoundResult result);
static const char *status_name(GameStatus status);
static bool status_is_fatal(GameStatus status);
static GameStatus save_user(GameUser *user);

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
    AppContext context = {0};

    SystemClock_Config();
    HAL_Init();
    HAL_Time_SCR1TIM_Init();
    USART0_Init();
    I2C1_Init();
    SPI0_Init();
    buttons_init();

    lcd_init();
    lcd_clear();
    lcd_send_string("RFID button game", 0, 0);
    lcd_send_string("Apply RFID card", 1, 0);

    MFRC522_Init(&mfrc522, &hspi0, RFID_CS_PORT, RFID_CS_PIN, RFID_RST_PORT, RFID_RST_PIN);
    PCD_Init(&mfrc522);

    xprintf("\r\nRFID button game\r\n");

    while (1) {
        GameMove player_move;
        uint32_t now = HAL_Millis();

        if (!context.user_active && !context.menu_prompt_printed) {
            xprintf("\r\nmenu: apply RFID card\r\n");
            context.menu_prompt_printed = true;
        }

        if (app_poll_rfid(&context, now)) {
            continue;
        }

        if (!context.user_active) {
            HAL_ProgramDelayMs(1u);
            continue;
        }

        if (buttons_poll_move(now, &player_move)) {
            app_handle_move(&context, player_move);
            continue;
        }
    }

    return 0;
}

static bool app_poll_rfid(AppContext *context, uint32_t now)
{
    if ((context == NULL) || (now < context->next_rfid_poll_ms)) {
        return false;
    }

    context->next_rfid_poll_ms = now + RFID_POLL_DELAY_MS;

    if (!read_rfid_card_once(&context->card_id)) {
        return false;
    }

    xprintf("\r\ncard uid=");
    print_user_id(&context->card_id);
    xprintf("\r\n");

    app_handle_card(context, &context->card_id);
    return true;
}

static void app_handle_card(AppContext *context, const GameUserId *card_id)
{
    if ((context == NULL) || (card_id == NULL)) {
        return;
    }

    if (!context->user_active) {
        (void)app_activate_user(context, card_id);
        return;
    }

    if (same_user_id(card_id, &context->user.id)) {
        if (app_save_active_user(context)) {
            app_enter_menu(context);
            xprintf("returned to menu\r\n");
        }
        return;
    }

    if (!app_save_active_user(context)) {
        return;
    }

    if (!app_activate_user(context, card_id)) {
        app_enter_menu(context);
    }
}

static bool app_activate_user(AppContext *context, const GameUserId *card_id)
{
    GameStatus status;

    if ((context == NULL) || (card_id == NULL)) {
        return false;
    }

    status = game_init_user(card_id, &context->user);
    print_init_status(status, &context->user);

    if (status_is_fatal(status)) {
        return false;
    }

    app_enter_game(context);
    print_help();
    return true;
}

static bool app_save_active_user(AppContext *context)
{
    GameStatus status;

    if (context == NULL) {
        return false;
    }

    status = save_user(&context->user);
    return !status_is_fatal(status);
}

static void app_enter_menu(AppContext *context)
{
    if (context == NULL) {
        return;
    }

    context->user_active = false;
    context->menu_prompt_printed = false;
    buttons_reset();
}

static void app_enter_game(AppContext *context)
{
    if (context == NULL) {
        return;
    }

    context->user_active = true;
    context->menu_prompt_printed = false;
    buttons_reset();
}

static void app_handle_move(AppContext *context, GameMove player_move)
{
    GameRound round;

    if (context == NULL) {
        return;
    }

    round = game_stage(&context->user, player_move);
    print_round_result(player_move, &round);
    print_user_stats(&context->user);
}

static void print_round_result(GameMove player_move, const GameRound *round)
{
    if (round == NULL) {
        return;
    }

    xprintf("you=%s mcu=%s result=%s\r\n",
            move_name(player_move),
            move_name(round->device_move),
            result_name(round->result));
}

static void print_user_stats(const GameUser *user)
{
    if (user == NULL) {
        return;
    }

    xprintf("stats W/D/L: %lu/%lu/%lu rounds=%lu sessions=%lu dirty=%u\r\n",
            (unsigned long)user->stats.wins,
            (unsigned long)user->stats.draws,
            (unsigned long)user->stats.losses,
            (unsigned long)user->stats.rounds,
            (unsigned long)user->sessions_played,
            user->dirty ? 1u : 0u);
}

void trap_handler(void)
{
    if (EPIC_CHECK_GPIO_IRQ()) {
        buttons_handle_irq();
    }

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
    USART_HandleTypeDef husart0 = {0};

    husart0.Instance = UART_0;
    husart0.transmitting = Enable;
    husart0.receiving = Disable;
    husart0.xck_mode = XCK_Mode3;
    husart0.baudrate = UART_BAUDRATE;

    (void)HAL_USART_Init(&husart0);
}

static void I2C1_Init(void)
{
    hi2c = (I2C_HandleTypeDef){0};
    hi2c.Instance = I2C_1;
    hi2c.Init.Mode = HAL_I2C_MODE_MASTER;
    hi2c.Init.DigitalFilter = I2C_DIGITALFILTER_OFF;
    hi2c.Init.AnalogFilter = I2C_ANALOGFILTER_DISABLE;
    hi2c.Init.AutoEnd = I2C_AUTOEND_ENABLE;
    hi2c.Clock.PRESC = 5;
    hi2c.Clock.SCLDEL = 15;
    hi2c.Clock.SDADEL = 15;
    hi2c.Clock.SCLH = 15;
    hi2c.Clock.SCLL = 15;

    if (HAL_I2C_Init(&hi2c) != HAL_OK) {
        xprintf("I2C_Init_Error\r\n");
    }
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

static bool read_rfid_card_once(GameUserId *id)
{
    if (id == NULL) {
        return false;
    }

    if (!PICC_IsNewCardPresent(&mfrc522)) {
        return false;
    }

    if (!PICC_ReadCardSerial(&mfrc522)) {
        HAL_ProgramDelayMs(RFID_POLL_DELAY_MS);
        return false;
    }

    copy_rfid_uid_to_user_id(&mfrc522.uid, id);
    (void)PICC_HaltA(&mfrc522);
    HAL_ProgramDelayMs(RFID_POLL_DELAY_MS);

    return id->length > 0u;
}

static void copy_rfid_uid_to_user_id(const Uid *uid, GameUserId *id)
{
    uint8_t index;

    if (id == NULL) {
        return;
    }

    id->length = 0u;
    for (index = 0u; index < GAME_USER_ID_MAX_BYTES; ++index) {
        id->bytes[index] = 0u;
    }

    if ((uid == NULL) || (uid->size == 0u) || (uid->size > GAME_USER_ID_MAX_BYTES)) {
        return;
    }

    id->length = uid->size;
    for (index = 0u; index < id->length; ++index) {
        id->bytes[index] = uid->uidByte[index];
    }
}

static bool same_user_id(const GameUserId *left, const GameUserId *right)
{
    uint8_t index;

    if ((left == NULL) || (right == NULL) || (left->length != right->length)) {
        return false;
    }

    for (index = 0u; index < left->length; ++index) {
        if (left->bytes[index] != right->bytes[index]) {
            return false;
        }
    }

    return true;
}

static void print_help(void)
{
    xprintf("\r\nRock Paper Scissors\r\n");
    xprintf("same RFID card = save and menu\r\n");
    xprintf("another RFID card = save and switch user\r\n");
    xprintf("buttons GPIO1_8/GPIO1_9/GPIO1_2 = rock/paper/scissors\r\n");
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

    if (id == NULL) {
        return;
    }

    for (index = 0u; index < id->length; ++index) {
        if (index != 0u) {
            xprintf(" ");
        }
        xprintf("%02X", id->bytes[index]);
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
