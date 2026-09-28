#ifndef __LED_H
#define __LED_H	 
#include "sys.h"


//LED端口定义
#define LED0 PBout(5)	// DS0
#define LED1 PEout(5)	// DS1	

//STM32F103RCT6
#define LED2 PAout(8)	// PA8	

#define UART_SPI_SELECT		PDin(11)		// SPI = 1, UART = 0

void LED_Init(void);	//初始化		 				    
#endif

















