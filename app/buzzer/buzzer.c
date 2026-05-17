#include "buzzer.h"

#include "mik32_hal_gpio.h"
#include "mik32_hal_timer32.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BUZZER_TIMER_INPUT_HZ 32000000u
#define BUZZER_INITIAL_TOP    61185u

#define NOTE_F5  698u
#define NOTE_C7  2093u
#define NOTE_C5  523u
#define NOTE_D5  587u
#define NOTE_E5  659u
#define NOTE_F4  349u
#define NOTE_G4  392u
#define NOTE_A4  440u
#define NOTE_AS4 466u
#define NOTE_B4  494u
#define NOTE_G5  784u
#define NOTE_A5  880u
#define NOTE_C6  1047u
#define NOTE_D6  1175u
#define NOTE_E6  1319u
#define NOTE_G6  1568u

#define NOTE_MS(denominator)       (1000u / (denominator))
#define NOTE_STEP_MS(denominator)  ((NOTE_MS(denominator) * 13u) / 10u)
#define MELODY_NOTE(freq, denom)   {freq, NOTE_MS(denom), NOTE_STEP_MS(denom)}
#define MELODY_REST(denom)         {0u, 0u, NOTE_MS(denom)}

typedef struct {
    uint16_t frequency_hz;
    uint16_t active_ms;
    uint16_t step_ms;
} BuzzerNote;

typedef struct {
    const BuzzerNote *notes;
    uint8_t note_count;
} BuzzerMelody;

static void buzzer_start_current_note(uint32_t now_ms);
static void buzzer_set_frequency(uint16_t frequency_hz);
static void buzzer_silence(void);
static const BuzzerMelody *buzzer_melody(AppSound sound);

static TIMER32_HandleTypeDef buzzer_timer;
static TIMER32_CHANNEL_HandleTypeDef buzzer_channel;
static const BuzzerNote *active_notes;
static uint32_t step_start_ms;
static uint8_t active_note_count;
static uint8_t active_note_index;
static bool initialized;
static bool playing;
static bool tone_enabled;

static const BuzzerNote intro_melody[] = {
    MELODY_NOTE(NOTE_C5, 8),
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_NOTE(NOTE_A5, 8),
    MELODY_REST(16),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_C5, 8),
    MELODY_NOTE(NOTE_D5, 8),
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_REST(16),
    MELODY_NOTE(NOTE_A5, 8),
    MELODY_NOTE(NOTE_C6, 8),
    MELODY_NOTE(NOTE_D6, 8),
    MELODY_NOTE(NOTE_E6, 8),
    MELODY_NOTE(NOTE_C6, 4),
};

static const BuzzerNote menu_back_melody[] = {
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_NOTE(NOTE_C5, 4),
};

static const BuzzerNote chant_melody[] = {
    MELODY_NOTE(NOTE_C5, 16),
    MELODY_NOTE(NOTE_D5, 16),
    MELODY_NOTE(NOTE_C5, 16),
    MELODY_NOTE(NOTE_D5, 16),

    MELODY_NOTE(NOTE_E5, 16),
    MELODY_NOTE(NOTE_F5, 16),
    MELODY_NOTE(NOTE_E5, 16),
    MELODY_NOTE(NOTE_F5, 16),

    MELODY_REST(32),

    MELODY_NOTE(NOTE_G5, 16),
    MELODY_NOTE(NOTE_A5, 16),
    MELODY_NOTE(NOTE_G5, 16),
    MELODY_NOTE(NOTE_A5, 16),

    MELODY_REST(32),

    MELODY_NOTE(NOTE_C6, 8),
    MELODY_NOTE(NOTE_D6, 8),
    MELODY_NOTE(NOTE_G5, 4)
};

static const BuzzerNote lose_melody[] = {
    MELODY_NOTE(NOTE_C5, 8),
    MELODY_NOTE(NOTE_B4, 8),
    MELODY_NOTE(NOTE_AS4, 8),
    MELODY_NOTE(NOTE_A4, 4),
    MELODY_REST(16),
    MELODY_NOTE(NOTE_G4, 8),
    MELODY_NOTE(NOTE_F4, 4),
};

static const BuzzerNote draw_melody[] = {
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_REST(16),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_REST(16),
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_NOTE(NOTE_E5, 4)
};

static const BuzzerNote win_melody[] = {
    MELODY_NOTE(NOTE_E5, 8),
    MELODY_NOTE(NOTE_G5, 8),
    MELODY_NOTE(NOTE_C6, 8),
    MELODY_NOTE(NOTE_D6, 8),
    MELODY_NOTE(NOTE_G6, 4),

    MELODY_REST(16),

    MELODY_NOTE(NOTE_E6, 8),
    MELODY_NOTE(NOTE_G6, 8),
    MELODY_NOTE(NOTE_C7, 4)
};

static const BuzzerMelody intro = {intro_melody, sizeof(intro_melody) / sizeof(intro_melody[0])};
static const BuzzerMelody menu_back = {menu_back_melody, sizeof(menu_back_melody) / sizeof(menu_back_melody[0])};
static const BuzzerMelody chant = {chant_melody, sizeof(chant_melody) / sizeof(chant_melody[0])};
static const BuzzerMelody lose = {lose_melody, sizeof(lose_melody) / sizeof(lose_melody[0])};
static const BuzzerMelody draw = {draw_melody, sizeof(draw_melody) / sizeof(draw_melody[0])};
static const BuzzerMelody win = {win_melody, sizeof(win_melody) / sizeof(win_melody[0])};

void buzzer_init(void)
{
    __HAL_PCC_GPIO_1_CLK_ENABLE();

    buzzer_timer.Instance = TIMER32_2;
    buzzer_timer.Clock.Source = TIMER32_SOURCE_PRESCALER;
    buzzer_timer.Clock.Prescaler = 0u;
    buzzer_timer.CountMode = TIMER32_COUNTMODE_FORWARD;
    buzzer_timer.Top = BUZZER_INITIAL_TOP;
    buzzer_timer.State = TIMER32_STATE_DISABLE;
    buzzer_timer.InterruptMask = 0u;
    (void)HAL_Timer32_Init(&buzzer_timer);

    buzzer_channel.TimerInstance = TIMER32_2;
    buzzer_channel.ChannelIndex = TIMER32_CHANNEL_1;
    buzzer_channel.PWM_Invert = TIMER32_CHANNEL_NON_INVERTED_PWM;
    buzzer_channel.Mode = TIMER32_CHANNEL_MODE_PWM;
    buzzer_channel.CaptureEdge = TIMER32_CHANNEL_CAPTUREEDGE_RISING;
    buzzer_channel.OCR = BUZZER_INITIAL_TOP / 2u;
    buzzer_channel.Noise = TIMER32_CHANNEL_FILTER_OFF;
    (void)HAL_Timer32_Channel_Init(&buzzer_channel);

    HAL_Timer32_Start(&buzzer_timer);
    initialized = true;
    buzzer_stop();
}

void buzzer_play(AppSound sound, uint32_t now_ms)
{
    const BuzzerMelody *melody;

    if (!initialized) {
        return;
    }

    melody = buzzer_melody(sound);
    if ((melody == NULL) || (melody->notes == NULL) || (melody->note_count == 0u)) {
        buzzer_stop();
        return;
    }

    active_notes = melody->notes;
    active_note_count = melody->note_count;
    active_note_index = 0u;
    playing = true;
    buzzer_start_current_note(now_ms);
}

void buzzer_update(uint32_t now_ms)
{
    const BuzzerNote *note;
    uint32_t elapsed_ms;

    if (!initialized || !playing || (active_notes == NULL)) {
        return;
    }

    note = &active_notes[active_note_index];
    elapsed_ms = now_ms - step_start_ms;
    if (elapsed_ms >= note->step_ms) {
        ++active_note_index;
        if (active_note_index >= active_note_count) {
            buzzer_stop();
            return;
        }
        buzzer_start_current_note(now_ms);
        return;
    }

    if (tone_enabled && (elapsed_ms >= note->active_ms)) {
        buzzer_silence();
    }
}

void buzzer_stop(void)
{
    active_notes = NULL;
    active_note_count = 0u;
    active_note_index = 0u;
    playing = false;
    buzzer_silence();
}

static void buzzer_start_current_note(uint32_t now_ms)
{
    const BuzzerNote *note = &active_notes[active_note_index];

    step_start_ms = now_ms;
    if ((note->frequency_hz == 0u) || (note->active_ms == 0u)) {
        buzzer_silence();
        return;
    }

    buzzer_set_frequency(note->frequency_hz);
}

static void buzzer_set_frequency(uint16_t frequency_hz)
{
    uint32_t top;

    if (!initialized || (frequency_hz == 0u)) {
        buzzer_silence();
        return;
    }

    top = BUZZER_TIMER_INPUT_HZ / frequency_hz;
    if (top < 2u) {
        top = 2u;
    }

    HAL_Timer32_Top_Set(&buzzer_timer, top);
    HAL_Timer32_Channel_OCR_Set(&buzzer_channel, top / 2u);
    HAL_Timer32_Value_Clear(&buzzer_timer);
    (void)HAL_Timer32_Channel_Enable(&buzzer_channel);
    tone_enabled = true;
}

static void buzzer_silence(void)
{
    if (initialized) {
        (void)HAL_Timer32_Channel_Disable(&buzzer_channel);
    }
    tone_enabled = false;
}

static const BuzzerMelody *buzzer_melody(AppSound sound)
{
    switch (sound) {
    case APP_SOUND_INTRO:
        return &intro;
    case APP_SOUND_MENU_BACK:
        return &menu_back;
    case APP_SOUND_CHANT:
        return &chant;
    case APP_SOUND_LOSE:
        return &lose;
    case APP_SOUND_DRAW:
        return &draw;
    case APP_SOUND_WIN:
        return &win;
    default:
        return NULL;
    }
}
