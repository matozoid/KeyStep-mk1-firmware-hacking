/* Reset handler and vector table for the STM32F103 (high-density, 60 IRQs). */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);
void system_init(void);

void Reset_Handler(void);
void Default_Handler(void);

#define WEAK __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK;
void HardFault_Handler(void) WEAK;
void MemManage_Handler(void) WEAK;
void BusFault_Handler(void) WEAK;
void UsageFault_Handler(void) WEAK;
void SVC_Handler(void) WEAK;
void DebugMon_Handler(void) WEAK;
void PendSV_Handler(void) WEAK;
void SysTick_Handler(void) WEAK;
void EXTI0_IRQHandler(void) WEAK;
void EXTI3_IRQHandler(void) WEAK;
void DMA1_Channel1_IRQHandler(void) WEAK;
void USB_LP_CAN1_RX0_IRQHandler(void) WEAK;
void TIM2_IRQHandler(void) WEAK;
void TIM4_IRQHandler(void) WEAK;
void USART1_IRQHandler(void) WEAK;

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used)) const vector_t vector_table[16 + 60] = {
    [0] = (vector_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 + 6] = EXTI0_IRQHandler,
    [16 + 9] = EXTI3_IRQHandler,
    [16 + 11] = DMA1_Channel1_IRQHandler,
    [16 + 20] = USB_LP_CAN1_RX0_IRQHandler,
    [16 + 28] = TIM2_IRQHandler,
    [16 + 30] = TIM4_IRQHandler,
    [16 + 37] = USART1_IRQHandler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;)
        *dst++ = *src++;
    for (uint32_t *dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;

    system_init();
    main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}
