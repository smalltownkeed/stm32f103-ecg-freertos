#include "board.h"
#include "stm32.h"
#include "history.h"
#include "waveform.h"

#define HALF_WORDS (ADC_SCANS_PER_POINT * 3u)
#define RX_SIZE 256u

volatile uint32_t adc_faults;
volatile uint32_t uart_errors;
volatile uint32_t adc_generation;
volatile uint32_t adc_event_cycles;
volatile unsigned adc_ready_half;
static volatile uint16_t adc_buffer[HALF_WORDS * 2];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static uint8_t rx_bytes[RX_SIZE];
static volatile uint8_t tx_busy;

static void clock_init(void)
{
    REG(RCC) |= 1u << 16;
    uint32_t timeout = 2000000;
    while (!(REG(RCC) & (1u << 17)) && --timeout) {}
    if (!timeout) {
        for (;;) {} /* 8 MHz HSE required. Inspect PC here if clock startup fails. */
    }
    REG(0x40022000u) = 0x12;
    REG(RCC + 4) = (7u << 18) | (1u << 16) | (2u << 14) | (4u << 8);
    REG(RCC) |= 1u << 24;
    while (!(REG(RCC) & (1u << 25))) {}
    REG(RCC + 4) |= 2;
    while ((REG(RCC + 4) & 12u) != 8u) {}
    REG(0xe000ed08u) = 0x08000000u;
    REG(0xe000ed0cu) = 0x05fa0300u; /* four preemption bits, no subpriority */
    REG(0xe000edfcu) |= 1u << 24;
    REG(0xe0001004u) = 0;
    REG(0xe0001000u) |= 1u; /* DWT cycle counter: latency measurements */
}

uint32_t board_cycles(void) { return REG(0xe0001004u); }
void board_led_toggle(void) { REG(GPIOC + 0x0c) ^= 1u << 13; }

void board_set_wave(unsigned index)
{
    REG(TIM4 + CR1) |= 2;
    REG(TIM4 + CCR1) = ecg_wave[index][0];
    REG(TIM4 + CCR2) = ecg_wave[index][1];
    REG(TIM4 + CCR3) = ecg_wave[index][2];
    REG(TIM4 + CR1) &= ~2u;
}

void board_stop_sampling(void)
{
    REG(TIM3 + CR1) = 0;
    REG(ADC1 + 8) &= ~(1u << 20);
    REG(DMA_CH(1)) = 0;
    adc_faults++;
}

void DMA1_Channel1_IRQHandler(void)
{
    uint32_t flags = REG(DMA1) & 15;
    REG(DMA1 + 4) = 15;
    if ((flags & 8) || (flags & 6) == 6) {
        board_stop_sampling();
    } else if (flags & 6) {
        adc_ready_half = (flags & 4) ? 0 : 1;
        adc_event_cycles = board_cycles();
        adc_generation++;
    } else {
        return;
    }
    app_adc_interrupt();
}

const volatile uint16_t *board_adc_half(unsigned half)
{
    return adc_buffer + half * HALF_WORDS;
}

int board_adc_valid(unsigned half, uint32_t generation)
{
    uint32_t remaining = REG(DMA_CH(1) + 4);
    if (generation != adc_generation || (REG(DMA1) & 14) || adc_faults) return 0;
    return half == 0 ? remaining > 0 && remaining <= HALF_WORDS : remaining > HALF_WORDS;
}

void USART2_IRQHandler(void)
{
    uint32_t status = REG(USART2);
    if (status & 0x2f) {
        uint8_t byte = (uint8_t)REG(USART2 + 4);
        if (status & 15) { uart_errors++; return; }
        uint8_t next = (uint8_t)(rx_head + 1);
        if (next == rx_tail) { uart_errors++; return; }
        rx_bytes[rx_head] = byte;
        barrier();
        rx_head = next;
    }
    /* Priority 4: do not call any FreeRTOS API from this interrupt. */
}

int board_uart_read(uint8_t *byte)
{
    if (rx_head == rx_tail) return 0;
    *byte = rx_bytes[rx_tail];
    barrier();
    rx_tail++;
    return 1;
}

void DMA1_Channel7_IRQHandler(void)
{
    if (REG(DMA1) & (8u << 24)) uart_errors++;
    REG(DMA_CH(7)) = 0;
    REG(DMA1 + 4) = 15u << 24;
    tx_busy = 0;
}

int board_uart_busy(void) { return tx_busy; }

void board_uart_send(const uint8_t *bytes, size_t size)
{
    REG(DMA_CH(7)) = 0;
    REG(DMA1 + 4) = 15u << 24;
    REG(DMA_CH(7) + 4) = (uint32_t)size;
    REG(DMA_CH(7) + 8) = USART2 + 4;
    REG(DMA_CH(7) + 12) = (uint32_t)(uintptr_t)bytes;
    tx_busy = 1;
    barrier();
    REG(DMA_CH(7)) = (1u << 12) | (1u << 7) | (1u << 4) | 11u;
}

void board_init(void)
{
    clock_init();
    REG(RCC + 0x14) |= 1;
    REG(RCC + 0x18) |= (1u << 9) | (1u << 4) | (1u << 3) | (1u << 2) | 1;
    REG(RCC + 0x1c) |= (1u << 17) | (1u << 2) | (1u << 1);
    REG(GPIOA) = (REG(GPIOA) & ~0x000fffffu) | 0x00004b00u;
    REG(GPIOB) = (REG(GPIOB) & ~0xff000000u) | 0xbb000000u;
    REG(GPIOB + 4) = (REG(GPIOB + 4) & ~15u) | 11;
    REG(GPIOC + 4) = (REG(GPIOC + 4) & ~(15u << 20)) | (2u << 20);
    REG(GPIOC + 0x10) = 1u << 13;

    REG(USART2 + 8) = 0x138;
    REG(USART2 + 0x10) = 0;
    REG(USART2 + 0x14) = (1u << 7) | 1;
    REG(USART2 + 0x0c) = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2);
    nvic_enable(38, 0x40);
    nvic_enable(17, 0x60);

    REG(TIM4 + PSC) = 0;
    REG(TIM4 + ARR) = 255;
    REG(TIM4 + CCMR1) = 0x6868;
    REG(TIM4 + CCMR2) = 0x68;
    REG(TIM4 + CCER) = 0x111;
    REG(TIM4 + CR1) = 1u << 7;
    board_set_wave(0);
    REG(TIM4 + EGR) = 1;

    REG(DMA_CH(1)) = 0;
    REG(DMA1 + 4) = 15;
    REG(DMA_CH(1) + 4) = HALF_WORDS * 2;
    REG(DMA_CH(1) + 8) = ADC1 + 0x4c;
    REG(DMA_CH(1) + 12) = (uint32_t)(uintptr_t)adc_buffer;
    REG(DMA_CH(1)) = (3u << 12) | (1u << 10) | (1u << 8) | (1u << 7) | (1u << 5) | 14;
    nvic_enable(11, 0x50);
    REG(ADC1 + 4) = 1u << 8;
    REG(ADC1 + 0x10) = (1u << 0) | (1u << 3) | (1u << 12);
    REG(ADC1 + 0x2c) = 2u << 20;
    REG(ADC1 + 0x30) = 0;
    REG(ADC1 + 0x34) = (4u << 10) | (1u << 5);
    REG(ADC1 + 8) = 1;
    for (volatile unsigned i = 0; i < 10000; i++) {}
    REG(ADC1 + 8) |= 1u << 3;
    while (REG(ADC1 + 8) & (1u << 3)) {}
    REG(ADC1 + 8) |= 1u << 2;
    while (REG(ADC1 + 8) & (1u << 2)) {}
    REG(ADC1 + 8) = (4u << 17) | (1u << 8) | 1;
    REG(TIM3 + PSC) = 0;
    REG(TIM3 + ARR) = 1124;
    REG(TIM3 + CR2) = 0;
    REG(TIM3 + EGR) = 1;
    REG(TIM3 + SR) = 0;
    REG(TIM3 + CR2) = 2u << 4;
    /* TIM3 UG above cannot reach the ADC while EXTTRIG is disabled. */
}

void board_start_sampling(void)
{
    REG(DMA_CH(1)) |= 1;
    REG(ADC1 + 8) |= 1u << 20;
    REG(TIM4 + CR1) |= 1;
    REG(TIM3 + CR1) = 1;
}
