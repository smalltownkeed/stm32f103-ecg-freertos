.syntax unified
.cpu cortex-m3
.thumb
.section .isr_vector,"a",%progbits
.global g_vectors
g_vectors:
 .word _estack
 .word Reset_Handler
 .word Default_Handler
 .word Default_Handler
 .word Default_Handler
 .word Default_Handler
 .word Default_Handler
 .word 0,0,0,0
 .word SVC_Handler
 .word Default_Handler
 .word 0
 .word PendSV_Handler
 .word SysTick_Handler
 /* external IRQ0..10 */
 .rept 11
 .word Default_Handler
 .endr
 .word DMA1_Channel1_IRQHandler
 /* IRQ12..16 */
 .rept 5
 .word Default_Handler
 .endr
 .word DMA1_Channel7_IRQHandler
 /* IRQ18..37 */
 .rept 20
 .word Default_Handler
 .endr
 .word USART2_IRQHandler
 /* IRQ39..42 */
 .rept 4
 .word Default_Handler
 .endr
.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.thumb_func
Reset_Handler:
 ldr r0, =_sidata
 ldr r1, =_sdata
 ldr r2, =_edata
1:
 cmp r1,r2
 bcs 2f
 ldr r3,[r0],#4
 str r3,[r1],#4
 b 1b
2:
 ldr r1,=_sbss
 ldr r2,=_ebss
 movs r3,#0
3:
 cmp r1,r2
 bcs 4f
 str r3,[r1],#4
 b 3b
4:
 bl main
 b .
.section .text.Default_Handler,"ax",%progbits
.global Default_Handler
.thumb_func
Default_Handler:
 b .
.section .note.GNU-stack,"",%progbits

