#include "led.h"


//初始化PB5和PE5为输出口.并使能这两个口的时钟		    
//LED IO初始化
void LED_Init(void)
{
	RCC->APB2ENR|=1<<3;    //使能PORTB时钟	   	 
	RCC->APB2ENR|=1<<6;    //使能PORTE时钟	
	   	 
	GPIOB->CRL&=0XFF0FFFFF; 
	GPIOB->CRL|=0X00300000;//PB.5 推挽输出   	 
    GPIOB->ODR|=1<<5;      //PB.5 输出高
											  
	GPIOE->CRL&=0XFF0FFFFF;
	GPIOE->CRL|=0X00300000;//PE.5推挽输出
	GPIOE->ODR|=1<<5;      //PE.5输出高 
	
	
	GPIOD->CRL&=0XFFF00FFF;  //IO状态设置,设置PD11、PD12
	GPIOD->CRL|=0X00088000;  //IO状态设置，PD11、PD12-输入	 
	GPIOD->ODR|=1<<11;      //PD11输出高 
	GPIOD->ODR|=1<<12;      //PD12输出高 
	
	
	RCC->APB2ENR|=1<<2;    //使能PORTA时钟	
	
	GPIOA->CRH&=0XFFFFFFF0;  //IO状态设置,设置PA8
	GPIOA->CRH|=0X00000003;  //IO状态设置，PA8输出
	
	GPIOA->ODR|=1<<8;      //PA.5 输出高
}






