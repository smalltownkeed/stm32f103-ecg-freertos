#include "history.h"

uint32_t history_oldest(const history_t *history)
{
    return history->next > HISTORY_SAMPLES ? history->next - HISTORY_SAMPLES : 0;
}

void history_append(history_t *history, const uint16_t values[3])
{
    unsigned slot = history->next % HISTORY_SAMPLES;
    for (unsigned channel = 0; channel < 3; channel++) {
        history->samples[slot][channel] = values[channel];
    }
    history->next++;
}

int history_get(const history_t *history, uint32_t sequence, uint16_t values[3])
{
    if (sequence < history_oldest(history)) return -1;
    if (sequence >= history->next) return 0;
    unsigned slot = sequence % HISTORY_SAMPLES;
    for (unsigned channel = 0; channel < 3; channel++) {
        values[channel] = history->samples[slot][channel];
    }
    return 1;
}

void adc_average(const volatile uint16_t *input, uint16_t output[3])
{
    uint32_t sum[3] = {0};
    for (unsigned i = 0; i < ADC_SCANS_PER_POINT; i++) {
        for (unsigned channel = 0; channel < 3; channel++) {
            sum[channel] += input[i * 3 + channel];
        }
    }
    for (unsigned channel = 0; channel < 3; channel++) {
        output[channel] = (uint16_t)((sum[channel] + 128) / 256);
    }
}
