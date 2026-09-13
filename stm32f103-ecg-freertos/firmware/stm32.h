#ifndef ECG_STM32_H
#define ECG_STM32_H
/* STM32F103x8 register addresses/bitfields from ST RM0008. */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define RCC 0x40021000u
#define GPIOA 0x40010800u
#define GPIOB 0x40010c00u
#define GPIOC 0x40011000u
#define TIM3 0x40000400u
#define TIM4 0x40000800u
#define ADC1 0x40012400u
#define USART2 0x40004400u
#define DMA1 0x40020000u
#define DMA_CH(n) (DMA1+8u+20u*((n)-1u))
#define CR1 0x00u
#define CR2 0x04u
#define DIER 0x0cu
#define SR 0x10u
#define EGR 0x14u
#define CCMR1 0x18u
#define CCMR2 0x1cu
#define CCER 0x20u
#define CNT 0x24u
#define PSC 0x28u
#define ARR 0x2cu
#define CCR1 0x34u
#define CCR2 0x38u
#define CCR3 0x3cu
static inline uint32_t irq_lock(void) {
    uint32_t state; __asm volatile("mrs %0, primask\ncpsid i":"=r"(state)::"memory"); return state;
}
static inline void irq_unlock(uint32_t state) { __asm volatile("msr primask, %0"::"r"(state):"memory"); }
static inline void barrier(void) { __asm volatile("dmb":::"memory"); }
static inline void nvic_enable(unsigned irq, uint8_t priority) {
    ((volatile uint8_t *)0xe000e400u)[irq]=priority;
    REG(0xe000e100u+(irq/32)*4)=1u<<(irq%32);
}
#endif
