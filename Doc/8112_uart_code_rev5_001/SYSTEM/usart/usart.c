#include "usart.h"	  
////////////////////////////////////////////////////////////////////////////////// 	 
//如果使用ucos,则包括下面的头文件即可.
#if SYSTEM_SUPPORT_OS
#include "includes.h"					//ucos 使用	  
#endif
//V1.3修改说明 
//支持适应不同频率下的串口波特率设置.
//加入了对printf的支持
//增加了串口接收命令功能.
//修正了printf第一个字符丢失的bug
//V1.4修改说明
//1,修改串口初始化IO的bug
//2,修改了USART_RX_STA,使得串口最大接收字节数为2的14次方
//3,增加了USART_REC_LEN,用于定义串口最大允许接收的字节数(不大于2的14次方)
//4,修改了EN_USART1_RX的使能方式
//V1.5修改说明
//1,增加了对UCOSII的支持
//V1.6修改说明 20150109
//uart_init函数去掉了开启PE中断
//V1.7修改说明 20150322
//修改OS_CRITICAL_METHOD宏判断为：SYSTEM_SUPPORT_OS
////////////////////////////////////////////////////////////////////////////////// 	  
 
//////////////////////////////////////////////////////////////////
//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 
	/* Whatever you require here. If the only file you are using is */ 
	/* standard output using printf() for debugging, no file handling */ 
	/* is required. */ 
}; 
/* FILE is typedef’ d in stdio.h. */ 
FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
_sys_exit(int x) 
{ 
	x = x; 
} 

/* UART1 发送环形缓冲区 - 非阻塞 printf */
#define UART1_TX_BUF_SIZE  1024
static char           uart1_tx_buf[UART1_TX_BUF_SIZE];
/* static volatile uint16_t uart1_tx_head = 0;   /* 写入指针（主循环写入） */
/* static volatile uint16_t uart1_tx_tail = 0;   /* 读取指针（中断读取） */

//重定向fputc函数 → 非阻塞写入环形缓冲区
int fputc(int ch, FILE *f)
{      
    uint16_t next = (uart1_tx_head + 1) % UART1_TX_BUF_SIZE;
    if (next != uart1_tx_tail)              // 缓冲区未满
    {
        uart1_tx_buf[uart1_tx_head] = (char)ch;
        uart1_tx_head = next;
        USART1->CR1 |= (1 << 7);            // 使能 TXE 中断，启动发送
    }
/*     /* 缓冲区满 → 丢弃该字符，绝不阻塞主循环 */
    return ch;
}
#endif 
//end
//////////////////////////////////////////////////////////////////

#if EN_USART1_RX   //如果使能了接收
//串口1中断服务程序
//注意,读取USARTx->SR能避免莫名其妙的错误   	
u8 USART_RX_BUF[USART_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//接收状态
//bit15，	接收完成标志
//bit14，	接收到0x0d
//bit13~0，	接收到的有效字节数目
u16 USART_RX_STA=0;       //接收状态标记	  




  
void USART1_IRQHandler(void)
{
	u8 res;	
#if SYSTEM_SUPPORT_OS 		//如果SYSTEM_SUPPORT_OS为真，则需要支持OS.
	OSIntEnter();    
#endif
	if(USART1->SR&(1<<5))	//接收到数据
	{	 
		res=USART1->DR; 
		if((USART_RX_STA&0x8000)==0)//接收未完成
		{
			if(USART_RX_STA&0x4000)//接收到了0x0d
			{
				if(res!=0x0a)USART_RX_STA=0;//接收错误,重新开始
				else USART_RX_STA|=0x8000;	//接收完成了 
			}else //还没收到0X0D
			{	
				if(res==0x0d)USART_RX_STA|=0x4000;
				else
				{
					USART_RX_BUF[USART_RX_STA&0X3FFF]=res;
					USART_RX_STA++;
					if(USART_RX_STA>(USART_REC_LEN-1))USART_RX_STA=0;//接收数据错误,重新开始接收	  
				}		 
			}
		}  		 	   
	}
/* 	/* 新增：TXE 发送中断处理 - 从环形缓冲区取数据发往 UART1 */
	if ((USART1->SR & (1 << 7)) && (USART1->CR1 & (1 << 7)))
	{
		if (uart1_tx_head != uart1_tx_tail)          // 还有数据待发送
		{
			USART1->DR = uart1_tx_buf[uart1_tx_tail];
			uart1_tx_tail = (uart1_tx_tail + 1) % UART1_TX_BUF_SIZE;
		}
		else
		{
			USART1->CR1 &= ~(1 << 7);                // 全部发完，关闭 TXE 中断
		}
	}
#if SYSTEM_SUPPORT_OS 	//如果SYSTEM_SUPPORT_OS为真，则需要支持OS.
	OSIntExit();  											 
#endif
}
#endif										 
//初始化IO 串口1
//pclk2:PCLK2时钟频率(Mhz)
//bound:波特率 
void uart_init(u32 pclk2,u32 bound)
{  	 
	float temp;
	u16 mantissa;
	u16 fraction;	   
	temp=(float)(pclk2*1000000)/(bound*16);//得到USARTDIV
	mantissa=temp;				 //得到整数部分
	fraction=(temp-mantissa)*16; //得到小数部分	 
    mantissa<<=4;
	mantissa+=fraction; 
	RCC->APB2ENR|=1<<2;   //使能PORTA口时钟  
	RCC->APB2ENR|=1<<14;  //使能串口时钟 
	GPIOA->CRH&=0XFFFFF00F;//IO状态设置
	GPIOA->CRH|=0X000008B0;//IO状态设置 
	RCC->APB2RSTR|=1<<14;   //复位串口1
	RCC->APB2RSTR&=~(1<<14);//停止复位	   	   
	//波特率设置
 	USART1->BRR=mantissa; // 波特率设置	 
	USART1->CR1|=0X200C;  //1位停止,无校验位.
#if EN_USART1_RX		  //如果使能了接收
	//使能接收中断 
	USART1->CR1|=1<<5;    //接收缓冲区非空中断使能	    	
	MY_NVIC_Init(3,3,USART1_IRQn,2);//组2，最低优先级 
#endif
}


//初始化IO 串口2
//pclk2:PCLK2时钟频率(Mhz)
//bound:波特率,设置1位停止位,偶校验
void uart2_init(u32 pclk2,u32 bound)
{  	 
	float temp;
	u16 mantissa;
	u16 fraction;	                                                                      
	temp=(float)(pclk2*1000000)/(bound*16);//得到USARTDIV
	mantissa=temp;				 //得到整数部分
	fraction=(temp-mantissa)*16; //得到小数部分	 
    mantissa<<=4;
	mantissa+=fraction; 
	RCC->APB2ENR|=1<<2;   //使能PORTA口时钟 
	
	RCC->APB1ENR|=1<<17;  //使能串口时钟,uart2
	
	GPIOA->CRL&=0XFFFF00FF;//IO状态设置,设置PA3、PA2
	GPIOA->CRL|=0X00008B00;//IO状态设置，PA3-浮空输入，PA2-推挽输出
	
//	PA2 = 0;
	PA3 = 1;
	
	RCC->APB1RSTR|=1<<17;   //复位串口2
	RCC->APB1RSTR&=~(1<<17);//停止复位	
	
	//波特率设置
 	USART2->BRR=mantissa; // 波特率设置	 
//	USART2->CR1|=0X24AC;  //1位停止,无校验位，允许中断,允许TXE发送中断,和RNXE接收中断
//	USART2->CR1|=0X202C;  //1位停止,无校验位，允许中断,禁止TXE发送中断,和RNXE接收中断
	USART2->CR1|=0X342C;  //1位停止,偶校验位，允许中断,禁止TXE发送中断,和RNXE接收中断
//	USART2->CR2|=0x0400;
//	USART2->CR3|=0x08;	//半双工
	
#if EN_USART2_RX		  //如果使能了接收
	//使能接收中断 
	USART2->CR1|=1<<5;    //接收缓冲区非空中断使能	    	
	MY_NVIC_Init(3,3,USART2_IRQn,2);//组1，最低优先级 
#endif
}



