#include "buttons.h"

#include "mik32_hal_gpio.h"
#include "mik32_hal_irq.h"
#include "mik32_hal_pcc.h"

#include <stddef.h>
#include <stdint.h>

#define BUTTON_DEBOUNCE_PRESS_MS   40u
#define BUTTON_DEBOUNCE_RELEASE_MS 40u

#define BUTTON_ROCK_BIT     (1u << 0)
#define BUTTON_PAPER_BIT    (1u << 1)
#define BUTTON_SCISSORS_BIT (1u << 2)

typedef struct {
    GPIO_TypeDef *port;
    HAL_PinsTypeDef pin;
    HAL_GPIO_Line irq_line;
    HAL_GPIO_Line_Config irq_mux;
    GameMove move;
    uint32_t bit;
} ButtonConfig;

static const ButtonConfig BUTTONS[] = {
    {GPIO_1, GPIO_PIN_8, GPIO_LINE_0, GPIO_MUX_PORT1_8_LINE_0, GAME_MOVE_ROCK, BUTTON_ROCK_BIT},
    {GPIO_1, GPIO_PIN_9, GPIO_LINE_1, GPIO_MUX_PORT1_9_LINE_1, GAME_MOVE_PAPER, BUTTON_PAPER_BIT},
    {GPIO_1, GPIO_PIN_2, GPIO_LINE_2, GPIO_MUX_PORT1_2_LINE_2, GAME_MOVE_SCISSORS, BUTTON_SCISSORS_BIT},
};

static volatile uint32_t button_irq_flags;
static uint32_t press_pending_flags;
static uint32_t press_deadline_ms;
static uint32_t held_flags;
static uint32_t release_pending_flags;
static uint32_t release_deadline_ms;

static void enable_gpio_clock(GPIO_TypeDef *port);
static uint32_t take_irq_flags(void);
static bool time_reached(uint32_t now_ms, uint32_t deadline_ms);
static bool button_is_pressed(const ButtonConfig *button);
static uint32_t pressed_flags(uint32_t flags);
static void update_release(uint32_t now_ms);
static bool move_from_flags(uint32_t flags, GameMove *move);

void buttons_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    size_t index;

    __HAL_PCC_GPIO_IRQ_CLK_ENABLE();

    gpio.Mode = HAL_GPIO_MODE_GPIO_INPUT;
    gpio.Pull = HAL_GPIO_PULL_UP;
    gpio.DS = HAL_GPIO_DS_2MA;

    for (index = 0u; index < (sizeof(BUTTONS) / sizeof(BUTTONS[0])); ++index) {
        enable_gpio_clock(BUTTONS[index].port);
        gpio.Pin = BUTTONS[index].pin;
        (void)HAL_GPIO_Init(BUTTONS[index].port, &gpio);
        (void)HAL_GPIO_InitInterruptLine(BUTTONS[index].irq_mux, GPIO_INT_MODE_FALLING);
    }

    HAL_GPIO_ClearInterrupts();
    HAL_EPIC_Clear(HAL_EPIC_GPIO_IRQ_MASK);
    HAL_EPIC_MaskLevelSet(HAL_EPIC_GPIO_IRQ_MASK);
    HAL_IRQ_EnableInterrupts();
}

void buttons_handle_irq(void)
{
    size_t index;

    for (index = 0u; index < (sizeof(BUTTONS) / sizeof(BUTTONS[0])); ++index) {
        if (HAL_GPIO_LineInterruptState(BUTTONS[index].irq_line)) {
            button_irq_flags |= BUTTONS[index].bit;
        }
    }

    HAL_GPIO_ClearInterrupts();
}

void buttons_reset(void)
{
    HAL_IRQ_DisableInterrupts();
    button_irq_flags = 0u;
    HAL_IRQ_EnableInterrupts();

    press_pending_flags = 0u;
    press_deadline_ms = 0u;
    held_flags = 0u;
    release_pending_flags = 0u;
    release_deadline_ms = 0u;
}

bool buttons_poll_move(uint32_t now_ms, GameMove *move)
{
    uint32_t new_flags;
    uint32_t confirmed_flags;

    if (move == NULL) {
        return false;
    }

    update_release(now_ms);

    new_flags = take_irq_flags() & ~held_flags & ~press_pending_flags;
    if (new_flags != 0u) {
        press_pending_flags |= new_flags;
        press_deadline_ms = now_ms + BUTTON_DEBOUNCE_PRESS_MS;
    }

    if ((press_pending_flags == 0u) || !time_reached(now_ms, press_deadline_ms)) {
        return false;
    }

    confirmed_flags = pressed_flags(press_pending_flags);
    press_pending_flags = 0u;

    if (confirmed_flags == 0u) {
        return false;
    }

    held_flags |= confirmed_flags;
    release_pending_flags = 0u;

    return move_from_flags(confirmed_flags, move);
}

static void enable_gpio_clock(GPIO_TypeDef *port)
{
    if (port == GPIO_0) {
        __HAL_PCC_GPIO_0_CLK_ENABLE();
    } else if (port == GPIO_1) {
        __HAL_PCC_GPIO_1_CLK_ENABLE();
    } else if (port == GPIO_2) {
        __HAL_PCC_GPIO_2_CLK_ENABLE();
    }
}

static uint32_t take_irq_flags(void)
{
    uint32_t flags;

    if (button_irq_flags == 0u) {
        return 0u;
    }

    HAL_IRQ_DisableInterrupts();
    flags = button_irq_flags;
    button_irq_flags = 0u;
    HAL_IRQ_EnableInterrupts();

    return flags;
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms)) >= 0;
}

static bool button_is_pressed(const ButtonConfig *button)
{
    return HAL_GPIO_ReadPin(button->port, button->pin) == GPIO_PIN_LOW;
}

static uint32_t pressed_flags(uint32_t flags)
{
    uint32_t pressed = 0u;
    size_t index;

    for (index = 0u; index < (sizeof(BUTTONS) / sizeof(BUTTONS[0])); ++index) {
        if (((flags & BUTTONS[index].bit) != 0u) && button_is_pressed(&BUTTONS[index])) {
            pressed |= BUTTONS[index].bit;
        }
    }

    return pressed;
}

static void update_release(uint32_t now_ms)
{
    uint32_t released_flags;

    if (held_flags == 0u) {
        release_pending_flags = 0u;
        return;
    }

    released_flags = held_flags & ~pressed_flags(held_flags);
    if (released_flags == 0u) {
        release_pending_flags = 0u;
        return;
    }

    if (released_flags != release_pending_flags) {
        release_pending_flags = released_flags;
        release_deadline_ms = now_ms + BUTTON_DEBOUNCE_RELEASE_MS;
        return;
    }

    if (time_reached(now_ms, release_deadline_ms)) {
        held_flags &= ~released_flags;
        release_pending_flags = 0u;
    }
}

static bool move_from_flags(uint32_t flags, GameMove *move)
{
    size_t index;

    for (index = 0u; index < (sizeof(BUTTONS) / sizeof(BUTTONS[0])); ++index) {
        if ((flags & BUTTONS[index].bit) != 0u) {
            *move = BUTTONS[index].move;
            return true;
        }
    }

    return false;
}
