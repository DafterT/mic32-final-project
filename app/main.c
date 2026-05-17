#include "app_controller.h"
#include "buttons.h"
#include "game.h"
#include "game_lcd_ui.h"
#include "game_storage.h"
#include "mik32_hal.h"
#include "mik32_hal_i2c.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_scr1_timer.h"
#include "mik32_hal_spi.h"
#include "mik32_hal_usart.h"
#include "lcd_driver.h"
#include "mfrc522.h"
#include "rfid_config.h"
#include "uid_hash.h"
#include "xprintf.h"

#include <stdbool.h>
#include <stdint.h>

#define UART_BAUDRATE 115200u

#define APP_SPLASH_MS     4000u
#define APP_WELCOME_MS    2000u
#define APP_CHANT_STEP_MS 400u
#define APP_DUEL_MS       2000u
#define APP_RESULT_MS     2000u

I2C_HandleTypeDef hi2c;

static SPI_HandleTypeDef hspi0;
static MFRC522 mfrc522;

static void app_log_event(AppLogEvent event, const AppLogData *data);
static void app_poll_inputs(AppController *app, uint32_t now_ms, uint32_t *next_rfid_poll_ms);
static bool app_poll_rfid(AppController *app, uint32_t now_ms, uint32_t *next_rfid_poll_ms);
static bool app_poll_buttons(AppController *app, uint32_t now_ms);
static void app_drain_unlocked_inputs(AppController *app,
                                      AppControllerState unlocked_from,
                                      uint32_t now_ms,
                                      uint32_t *next_rfid_poll_ms);
static void print_round_result(GameMove player_move, const GameRound *round);
static void print_init_status(GameStatus status, const GameUser *user);
static void print_save_status(GameStatus status, const GameUser *user);
static void SystemClock_Config(void);
static void USART0_Init(void);
static void I2C1_Init(void);
static void SPI0_Init(void);
static bool read_rfid_card_once(GameUserId *id);
static void copy_rfid_uid_to_user_id(const Uid *uid, GameUserId *id);
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms);
static void print_user_id(const GameUserId *id);

static const AppControllerTiming APP_TIMING = {
    .splash_ms = APP_SPLASH_MS,
    .welcome_ms = APP_WELCOME_MS,
    .chant_step_ms = APP_CHANT_STEP_MS,
    .duel_ms = APP_DUEL_MS,
    .result_ms = APP_RESULT_MS,
};

static const AppControllerPorts APP_PORTS = {
    .list_users = game_storage_list_users,
    .init_user = game_init_user,
    .stage = game_stage,
    .save_user = game_save_user,
    .show_splash = game_lcd_show_splash,
    .show_menu_empty = game_lcd_show_menu_empty,
    .show_menu_user = game_lcd_show_menu_user,
    .show_welcome = game_lcd_show_welcome,
    .show_wait_move = game_lcd_show_wait_move,
    .show_chant = game_lcd_show_chant,
    .show_duel = game_lcd_show_duel,
    .show_result = game_lcd_show_result,
    .log = app_log_event,
};

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
    AppController app = {0};
    uint32_t next_rfid_poll_ms = 0u;

    SystemClock_Config();
    HAL_Init();
    HAL_Time_SCR1TIM_Init();
    USART0_Init();
    I2C1_Init();
    SPI0_Init();
    buttons_init();

    lcd_init();

    MFRC522_Init(&mfrc522, &hspi0, RFID_CS_PORT, RFID_CS_PIN, RFID_RST_PORT, RFID_RST_PIN);
    PCD_Init(&mfrc522);

    app_controller_init(&app, &APP_PORTS, &APP_TIMING, HAL_Millis());
    buttons_reset();
    while (1) {
        AppControllerState unlocked_from;
        uint32_t now = HAL_Millis();

        if (app_controller_tick(&app, now, &unlocked_from)) {
            app_drain_unlocked_inputs(&app, unlocked_from, now, &next_rfid_poll_ms);
        }
        app_poll_inputs(&app, now, &next_rfid_poll_ms);
    }

    return 0;
}

static void app_poll_inputs(AppController *app, uint32_t now_ms, uint32_t *next_rfid_poll_ms)
{
    if (app == NULL) {
        return;
    }

    if (app_poll_rfid(app, now_ms, next_rfid_poll_ms)) {
        return;
    }
    (void)app_poll_buttons(app, now_ms);
}

static bool app_poll_rfid(AppController *app, uint32_t now_ms, uint32_t *next_rfid_poll_ms)
{
    GameUserId card_id = {0};

    if ((app == NULL) || (next_rfid_poll_ms == NULL) || !app_controller_accepts_rfid(app)) {
        return false;
    }
    if (!time_reached(now_ms, *next_rfid_poll_ms)) {
        return false;
    }

    *next_rfid_poll_ms = now_ms + RFID_POLL_DELAY_MS;
    if (!read_rfid_card_once(&card_id)) {
        return false;
    }

    app_controller_handle_card(app, &card_id, now_ms);
    return true;
}

static bool app_poll_buttons(AppController *app, uint32_t now_ms)
{
    GameMove move;

    if (app == NULL) {
        return false;
    }
    if (!buttons_poll_move(now_ms, &move)) {
        return false;
    }

    app_controller_handle_button(app, move, now_ms);
    return true;
}

static void app_drain_unlocked_inputs(AppController *app,
                                      AppControllerState unlocked_from,
                                      uint32_t now_ms,
                                      uint32_t *next_rfid_poll_ms)
{
    GameUserId ignored_id = {0};

    if ((app == NULL) || (next_rfid_poll_ms == NULL)) {
        return;
    }

    buttons_reset();
    if (read_rfid_card_once(&ignored_id)) {
        app_controller_handle_ignored_card(app, unlocked_from, &ignored_id);
        *next_rfid_poll_ms = now_ms + RFID_POLL_DELAY_MS;
    }
    buttons_reset();
}

static void app_log_event(AppLogEvent event, const AppLogData *data)
{
    switch (event) {
    case APP_LOG_GAME_START:
        xprintf("\r\ngame start\r\n");
        break;
    case APP_LOG_MENU_LIST_STATUS:
        if (data != NULL) {
            xprintf("menu list status=%s\r\n", app_controller_status_name(data->status));
        }
        break;
    case APP_LOG_CARD_ACCEPTED:
        if ((data != NULL) && (data->card_id != NULL)) {
            xprintf("card accepted hash=0x%04X uid=", (unsigned int)uid_hash16(data->card_id));
            print_user_id(data->card_id);
            xprintf("\r\n");
        }
        break;
    case APP_LOG_CARD_IGNORED:
        if ((data != NULL) && (data->card_id != NULL)) {
            xprintf("card ignored state=%s hash=0x%04X uid=",
                    app_controller_state_name(data->state),
                    (unsigned int)uid_hash16(data->card_id));
            print_user_id(data->card_id);
            xprintf("\r\n");
        }
        break;
    case APP_LOG_BUTTON_ACCEPTED_ACTION:
        if (data != NULL) {
            xprintf("button accepted action=%s\r\n", app_controller_menu_action_name(data->action));
        }
        break;
    case APP_LOG_BUTTON_ACCEPTED_MOVE:
        if (data != NULL) {
            xprintf("button accepted move=%s\r\n", app_controller_move_name(data->move));
        }
        break;
    case APP_LOG_BUTTON_IGNORED:
        if (data != NULL) {
            xprintf("button ignored state=%s move=%s\r\n",
                    app_controller_state_name(data->state),
                    app_controller_move_name(data->move));
        }
        break;
    case APP_LOG_ROUND_RESULT:
        if (data != NULL) {
            print_round_result(data->move, &data->round);
        }
        break;
    case APP_LOG_USER_INIT:
        if (data != NULL) {
            print_init_status(data->status, data->user);
        }
        break;
    case APP_LOG_SAVE_STATUS:
        if (data != NULL) {
            print_save_status(data->status, data->user);
        }
        break;
    case APP_LOG_RETURNED_MENU_SAME_CARD:
        xprintf("user returned menu reason=same_card\r\n");
        break;
    default:
        break;
    }
}

static void print_round_result(GameMove player_move, const GameRound *round)
{
    if (round == NULL) {
        return;
    }

    xprintf("round you=%s bot=%s result=%s\r\n",
            app_controller_move_name(player_move),
            app_controller_move_name(round->device_move),
            app_controller_result_name(round->result));
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
        return false;
    }

    copy_rfid_uid_to_user_id(&mfrc522.uid, id);
    (void)PICC_HaltA(&mfrc522);

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

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms)) >= 0;
}

static void print_init_status(GameStatus status, const GameUser *user)
{
    if (user == NULL) {
        return;
    }

    xprintf("user init status=%s hash=0x%04X uid=",
            app_controller_status_name(status),
            (unsigned int)uid_hash16(&user->id));
    print_user_id(&user->id);
    xprintf(" loaded=%u sessions=%lu rounds=%lu\r\n",
            user->loaded_from_storage ? 1u : 0u,
            (unsigned long)user->sessions_played,
            (unsigned long)user->stats.rounds);
}

static void print_save_status(GameStatus status, const GameUser *user)
{
    if (user == NULL) {
        return;
    }

    if ((status == GAME_STATUS_CRC_ERROR) && !user->dirty) {
        xprintf("save status=ok warning=crc_reused hash=0x%04X uid=", (unsigned int)uid_hash16(&user->id));
    } else if ((status == GAME_STATUS_STORAGE_FULL) && !user->dirty) {
        xprintf("save status=ok warning=evicted_old_user hash=0x%04X uid=", (unsigned int)uid_hash16(&user->id));
    } else {
        xprintf("save status=%s hash=0x%04X uid=", app_controller_status_name(status), (unsigned int)uid_hash16(&user->id));
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
