#include "timer.h"
#include "led.h"
/* #include "hlw8112_reader.h"     /* HLW811X_Reader_TimerIRQ() */

unsigned char B_ReadData_HLW8110;

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & 0X0001)
    {
        static uint16_t led_tick = 0;
        static uint8_t  read_tick = 0;

        led_tick++;
        if (led_tick >= 750) led_tick = 0;
        LED2 = (led_tick < 500) ? 0 : 1;

        read_tick++;
        if (read_tick >= 50) {
            read_tick = 0;
            B_ReadData_HLW8110 = 1;
        }

/*         HLW811X_Reader_TimerIRQ();  /* 无参数，操作全局变量 */
    }
    TIM3->SR &= ~(1 << 0);
}

void TIM3_Int_Init(u16 arr,u16 psc)
{
    RCC->APB1ENR|=1<<1;
    TIM3->ARR=arr;
    TIM3->PSC=psc;
    TIM3->DIER|=1<<0;
    TIM3->CR1|=0x01;
    MY_NVIC_Init(1,3,TIM3_IRQn,2);
}
