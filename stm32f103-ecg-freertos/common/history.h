#ifndef ECG_HISTORY_H
#define ECG_HISTORY_H
#include <stdint.h>
#define HISTORY_SAMPLES 1600u
#define ADC_SCANS_PER_POINT 256u
typedef struct {
    uint16_t samples[HISTORY_SAMPLES][3];
    uint32_t next;
} history_t;
uint32_t history_oldest(const history_t *history);
void history_append(history_t *history, const uint16_t values[3]);
int history_get(const history_t *history, uint32_t sequence, uint16_t values[3]);
void adc_average(const volatile uint16_t *input, uint16_t output[3]);
#endif
