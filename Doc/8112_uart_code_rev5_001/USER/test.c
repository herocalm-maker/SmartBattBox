/**
 * @file    test.c
 * @brief   HLW8110/8112 驱动演示（全局变量方案）
 * @details 全部通过 extern 全局变量调用，无 ctx 参数。
 *
 * @version V5.1 全局变量方案
 * @date    2026-06-05
 */

#include "sys.h"
#include "usart.h"
#include "delay.h"
#include "led.h"
#include "beep.h"
#include "key.h"
#include "timer.h"
#include "test.h"
#include "hlw8112.h"

int main(void)
{
    delay_init(72);
    Stm32_Clock_Init(9);
    uart_init(36,460800);
    LED_Init();
    TIM3_Int_Init(19,7199);

#if HLW811X_UART_BAUD == 9600
    uart2_init(18,9600);
#elif HLW811X_UART_BAUD == 19200
    uart2_init(18,19200);
#elif HLW811X_UART_BAUD == 38400
    uart2_init(18,38400);
#else
    uart2_init(18,9600);
#endif

    delay_ms(200);
    delay_ms(200);

    HLW811X_HW_Init();
    HLW811X_HW_SetEN(0);
    HLW811X_HW_SetSCLK(1);
    HLW811X_HW_SetCS(0);

    delay_ms(200);
    delay_ms(200);

    while(1)
    {
        switch (g_hlw811x_chip_state)
        {
            case HLW811X_CHIP_STATE_INIT:
                HLW811X_Reader_InitChip();
                if (HLW811X_Reader_IsInitDone())
                {
                    HLW811X_Reader_Reset();
#if HLW811X_LOG_ENABLE
                    HLW811X_HW_Print("[HLW811X] Init OK, switch to WORK\r\n");
#endif
                    g_hlw811x_chip_state = HLW811X_CHIP_STATE_WORK;
                }
                break;

            case HLW811X_CHIP_STATE_WORK:
                HLW811X_Reader_Start();
                HLW811X_Reader_Run();

                if (HLW811X_Reader_IsReady())
                {
#if HLW811X_LOG_ENABLE
                    HLW811X_HW_Print("V=%.2f IA=%.3f PA=%.2f EA=%.6f "
                                     "IB=%.3f PB=%.2f EB=%.6f "
                                     "P_SC=%.2f PF=%.3f "
                                     "Freq=%.1f Ang=%.1f SYS=0x%02X\r\n",
                        F_AC_V,
                        F_AC_I_A, F_AC_P_A, F_AC_E_A,
                        F_AC_I_B, F_AC_P_B, F_AC_E_B,
                        F_AC_P_SC,
                        F_AC_PF,
                        F_AC_LINE_Freq, F_Angle,
                        g_hlw811x_reader_sysstat);
#endif
                    HLW811X_Reader_PrintErrors();

                    if (g_hlw811x_reader_rst_det)
                    {
                        g_hlw811x_reader_rst_det = 0;
#if HLW811X_LOG_ENABLE
                        HLW811X_HW_Print("[HLW811X] Chip Reset detected!\r\n");
#endif
                        g_hlw811x_chip_state = HLW811X_CHIP_STATE_RESET;
                    }
                }
                {
                    static unsigned long last_err = 0;
                    if (HLW811X_Reader_GetErrors() != last_err)
                    {
                        last_err = HLW811X_Reader_GetErrors();
                        HLW811X_Reader_PrintErrors();
                    }
                }
                break;

            case HLW811X_CHIP_STATE_RESET:
                g_hlw811x_reader_init_done = 0;
                g_hlw811x_reader_init_st = HLW811X_READER_INIT_IDLE;
                g_hlw811x_chip_state = HLW811X_CHIP_STATE_INIT;
                break;
        }
    }
}
