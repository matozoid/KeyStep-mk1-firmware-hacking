/* Minimal STM32F103 register definitions (written from the RM0008 reference manual).
 * Only the peripherals this firmware uses. */
#ifndef STM32F103_H
#define STM32F103_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- Cortex-M3 core ---- */
typedef struct {
    volatile uint32_t CTRL, LOAD, VAL, CALIB;
} SysTick_t;
#define SYSTICK ((SysTick_t *)0xE000E010u)

#define SCB_VTOR  REG32(0xE000ED08u)
#define SCB_AIRCR REG32(0xE000ED0Cu)
#define NVIC_ISER ((volatile uint32_t *)0xE000E100u)
#define NVIC_ICER ((volatile uint32_t *)0xE000E180u)
#define NVIC_IPR  ((volatile uint8_t *)0xE000E400u)

static inline void nvic_enable(unsigned irq) { NVIC_ISER[irq >> 5] = 1u << (irq & 31); }
static inline void nvic_disable(unsigned irq) { NVIC_ICER[irq >> 5] = 1u << (irq & 31); }
static inline void nvic_priority(unsigned irq, uint8_t prio) { NVIC_IPR[irq] = (uint8_t)(prio << 4); }

static inline void irq_disable(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_enable(void) { __asm volatile("cpsie i" ::: "memory"); }
static inline uint32_t irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask)::"memory");
    return primask;
}
static inline void irq_restore(uint32_t primask) { __asm volatile("msr primask, %0" ::"r"(primask) : "memory"); }

/* IRQ numbers used */
enum {
    IRQ_EXTI0 = 6,
    IRQ_EXTI3 = 9,
    IRQ_DMA1_CH1 = 11,
    IRQ_USB_LP_CAN_RX0 = 20,
    IRQ_TIM2 = 28,
    IRQ_TIM4 = 30,
    IRQ_I2C1_EV = 31,
    IRQ_I2C2_EV = 33,
    IRQ_USART1 = 37,
};

/* ---- RCC ---- */
typedef struct {
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, APB1ENR, BDCR, CSR;
} RCC_t;
#define RCC ((RCC_t *)0x40021000u)

#define RCC_CR_HSION   (1u << 0)
#define RCC_CR_HSIRDY  (1u << 1)
#define RCC_CR_HSEON   (1u << 16)
#define RCC_CR_HSERDY  (1u << 17)
#define RCC_CR_PLLON   (1u << 24)
#define RCC_CR_PLLRDY  (1u << 25)

#define RCC_AHB_DMA1   (1u << 0)

#define RCC_APB2_AFIO  (1u << 0)
#define RCC_APB2_IOPA  (1u << 2)
#define RCC_APB2_IOPB  (1u << 3)
#define RCC_APB2_IOPC  (1u << 4)
#define RCC_APB2_IOPD  (1u << 5)
#define RCC_APB2_IOPE  (1u << 6)
#define RCC_APB2_ADC1  (1u << 9)
#define RCC_APB2_USART1 (1u << 14)

#define RCC_APB1_TIM2  (1u << 0)
#define RCC_APB1_TIM3  (1u << 1)
#define RCC_APB1_TIM4  (1u << 2)
#define RCC_APB1_SPI2  (1u << 14)
#define RCC_APB1_I2C1  (1u << 21)
#define RCC_APB1_I2C2  (1u << 22)
#define RCC_APB1_USB   (1u << 23)
#define RCC_APB1_PWR   (1u << 28)
#define RCC_APB1_DAC   (1u << 29)

/* ---- FLASH ---- */
typedef struct {
    volatile uint32_t ACR, KEYR, OPTKEYR, SR, CR, AR, RESERVED, OBR, WRPR;
} FLASH_t;
#define FLASH ((FLASH_t *)0x40022000u)
#define FLASH_SR_BSY   (1u << 0)
#define FLASH_SR_PGERR (1u << 2)
#define FLASH_SR_WRPRTERR (1u << 4)
#define FLASH_SR_EOP   (1u << 5)
#define FLASH_CR_PG    (1u << 0)
#define FLASH_CR_PER   (1u << 1)
#define FLASH_CR_STRT  (1u << 6)
#define FLASH_CR_LOCK  (1u << 7)
#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

/* ---- GPIO / AFIO ---- */
typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_t;
#define GPIOA ((GPIO_t *)0x40010800u)
#define GPIOB ((GPIO_t *)0x40010C00u)
#define GPIOC ((GPIO_t *)0x40011000u)
#define GPIOD ((GPIO_t *)0x40011400u)
#define GPIOE ((GPIO_t *)0x40011800u)

typedef struct {
    volatile uint32_t EVCR, MAPR, EXTICR[4], RESERVED, MAPR2;
} AFIO_t;
#define AFIO ((AFIO_t *)0x40010000u)

typedef struct {
    volatile uint32_t IMR, EMR, RTSR, FTSR, SWIER, PR;
} EXTI_t;
#define EXTI ((EXTI_t *)0x40010400u)

/* 4-bit pin configurations (CNF:MODE) */
#define GPIO_ANALOG        0x0u
#define GPIO_IN_FLOATING   0x4u
#define GPIO_IN_PULL       0x8u /* pull-up if ODR bit set, else pull-down */
#define GPIO_OUT_PP        0x3u /* 50 MHz */
#define GPIO_OUT_OD        0x7u
#define GPIO_AF_PP         0xBu
#define GPIO_AF_OD         0xFu

/* ---- Timers (general purpose) ---- */
typedef struct {
    volatile uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RCR, CCR1, CCR2,
        CCR3, CCR4, BDTR, DCR, DMAR;
} TIM_t;
#define TIM2 ((TIM_t *)0x40000000u)
#define TIM3 ((TIM_t *)0x40000400u)
#define TIM4 ((TIM_t *)0x40000800u)

/* ---- DAC ---- */
typedef struct {
    volatile uint32_t CR, SWTRIGR, DHR12R1, DHR12L1, DHR8R1, DHR12R2, DHR12L2, DHR8R2, DHR12RD, DHR12LD,
        DHR8RD, DOR1, DOR2;
} DAC_t;
#define DAC ((DAC_t *)0x40007400u)

/* ---- USART ---- */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_t;
#define USART1 ((USART_t *)0x40013800u)
#define USART_SR_ORE  (1u << 3)
#define USART_SR_RXNE (1u << 5)
#define USART_SR_TC   (1u << 6)
#define USART_SR_TXE  (1u << 7)
#define USART_CR1_RE  (1u << 2)
#define USART_CR1_TE  (1u << 3)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_CR1_TXEIE (1u << 7)
#define USART_CR1_UE  (1u << 13)

/* ---- SPI ---- */
typedef struct {
    volatile uint32_t CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR, I2SCFGR, I2SPR;
} SPI_t;
#define SPI2 ((SPI_t *)0x40003800u)
#define SPI_SR_RXNE (1u << 0)
#define SPI_SR_TXE (1u << 1)
#define SPI_SR_BSY (1u << 7)

/* ---- I2C ---- */
typedef struct {
    volatile uint32_t CR1, CR2, OAR1, OAR2, DR, SR1, SR2, CCR, TRISE;
} I2C_t;
#define I2C1 ((I2C_t *)0x40005400u)
#define I2C2 ((I2C_t *)0x40005800u)

/* ---- ADC / DMA ---- */
typedef struct {
    volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR[4], HTR, LTR, SQR1, SQR2, SQR3, JSQR, JDR[4], DR;
} ADC_t;
#define ADC1 ((ADC_t *)0x40012400u)

typedef struct {
    volatile uint32_t CCR, CNDTR, CPAR, CMAR, RESERVED;
} DMA_Channel_t;
typedef struct {
    volatile uint32_t ISR, IFCR;
    DMA_Channel_t CH[7];
} DMA_t;
#define DMA1 ((DMA_t *)0x40020000u)

/* ---- IWDG ---- */
typedef struct {
    volatile uint32_t KR, PR, RLR, SR;
} IWDG_t;
#define IWDG ((IWDG_t *)0x40003000u)

/* ---- USB FS device ---- */
#define USB_BASE     0x40005C00u
#define USB_PMA_BASE 0x40006000u
#define USB_EPR(n)   REG32(USB_BASE + 4u * (n))
#define USB_CNTR     REG32(USB_BASE + 0x40u)
#define USB_ISTR     REG32(USB_BASE + 0x44u)
#define USB_FNR      REG32(USB_BASE + 0x48u)
#define USB_DADDR    REG32(USB_BASE + 0x4Cu)
#define USB_BTABLE   REG32(USB_BASE + 0x50u)

#endif
