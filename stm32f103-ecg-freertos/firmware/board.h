#ifndef ECG_BOARD_H
#define ECG_BOARD_H
#include <stddef.h>
#include <stdint.h>
extern volatile uint32_t adc_faults;
extern volatile uint32_t uart_errors;
extern volatile uint32_t adc_generation;
extern volatile uint32_t adc_event_cycles;
extern volatile unsigned adc_ready_half;
void board_init(void);
void board_start_sampling(void);
void board_stop_sampling(void);
void board_set_wave(unsigned index);
const volatile uint16_t *board_adc_half(unsigned half);
int board_adc_valid(unsigned half, uint32_t generation);
int board_uart_read(uint8_t *byte);
int board_uart_busy(void);
void board_uart_send(const uint8_t *bytes, size_t size);
void board_led_toggle(void);
uint32_t board_cycles(void);
void app_adc_interrupt(void);
#endif
