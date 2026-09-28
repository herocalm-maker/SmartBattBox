/**
 * @file    hlw8112_log.c
 * @brief   HLW8112/HLW8110 日志输出模块 (跨平台版本)
 * @details 封装所有调试输出，通过 HLW811X_HW_Print 调用。
 *          HLW811X_HW_Print 定义在 hlw8112_hw.h 中，各平台自行实现底层输出。
 * @note    关闭日志输出：在 hlw8112_config.h 中设置 HLW811X_LOG_ENABLE=0
 * @version V3.1 更新 HLW811X_HW_Print 引用
 */

#include "hlw8112.h"
#include "hlw8112_log.h"

/*============================================================
 * 日志输出函数
 * HLW811X_HW_Print 宏由 hlw8112_hw.h 提供，无需在此文件中定义
 *============================================================*/

void HLW811X_LOG_CheckSum(unsigned int checksum)
{
    HLW811X_HW_Print("CheckSum:%x\n ", checksum);
}

void HLW811X_LOG_CheckSum_OK(void)
{
    HLW811X_HW_Print("CheckSum OK\r\n ");
}

void HLW811X_LOG_CheckSum_Error(void)
{
    HLW811X_HW_Print("CheckSum Error\r\n ");
}

void HLW811X_LOG_CalFactor_Fail(void)
{
    HLW811X_HW_Print("CalFactor Fail, use default\r\n ");
}

void HLW811X_LOG_RegConfig_Error(void)
{
    HLW811X_HW_Print("SYSCON/EMUCON/EMUCON2 Error, reinit\r\n");
}

void HLW811X_LOG_Angle_Reg(unsigned long angle_reg)
{
    HLW811X_HW_Print("Angle Reg:%ld\n ", angle_reg);
}

void HLW811X_LOG_Angle_Reg_Error(void)
{
    HLW811X_HW_Print("Angle Reg Error\r\n");
}

void HLW811X_LOG_Current_Lead(float angle)
{
    HLW811X_HW_Print("Current Lead:%f\n ", angle);
}

void HLW811X_LOG_Current_Lag(float angle)
{
    HLW811X_HW_Print("Current Lag:%f\n ", angle);
}

void HLW811X_LOG_MeterData_Header(void)
{
    HLW811X_HW_Print("==============================================\r\n");
    HLW811X_HW_Print("HLW8110/HLW8112 AC Measure, UART Mode\r\n");
    HLW811X_HW_Print("==============================================\r\n");
    HLW811X_HW_Print("\r\n");
}

void HLW811X_LOG_Channel_A_Data(float voltage, float current, float power, float apparent_power, float energy)
{
    HLW811X_HW_Print(" V = %.2f V\r\n ", voltage);
    HLW811X_HW_Print(" I_A = %.3f A\r\n", current);
    HLW811X_HW_Print(" P_A = %.2f W\r\n", power);
    HLW811X_HW_Print(" P_SC = %.2f W\r\n ", apparent_power);
    HLW811X_HW_Print(" E_A = %f KWH\r\n ", energy);
    HLW811X_HW_Print("\r\n");
}

void HLW811X_LOG_Channel_B_Data(float voltage, float current, float power, float energy)
{
    HLW811X_HW_Print(" V = %.2f\r\n ", voltage);
    HLW811X_HW_Print(" I_B = %.3f\r\n ", current);
    HLW811X_HW_Print(" P_B = %.2f\r\n ", power);
    HLW811X_HW_Print(" E_B = %f KWH\r\n ", energy);
    HLW811X_HW_Print("\r\n");
}

void HLW811X_LOG_PF_Freq_Angle(float pf, float freq, float angle)
{
    HLW811X_HW_Print(" PF = %f \r\n ", pf);
    HLW811X_HW_Print(" Freq = %f Hz \r\n ", freq);
    HLW811X_HW_Print(" Angle = %f \r\n  ", angle);
    HLW811X_HW_Print("\r\n");
}

void HLW811X_LOG_Raw_RegData(unsigned long rmsu, unsigned long rmsia, unsigned long powerpa, unsigned long energy_pa,
                         unsigned long rmsib, unsigned long powerpb, unsigned long energy_pb)
{
    HLW811X_HW_Print(" RMSU = %lx \r\n ", rmsu);
    HLW811X_HW_Print(" RMSIA = %lx \r\n", rmsia);
    HLW811X_HW_Print(" POWERPA = %lx \r\n ", powerpa);
    HLW811X_HW_Print(" ENERGY_PA = %lx \r\n ", energy_pa);
    HLW811X_HW_Print("\r\n");
    HLW811X_HW_Print(" RMSIB = %lx \r\n ", rmsib);
    HLW811X_HW_Print(" POWERPB = %lx \r\n", powerpb);
    HLW811X_HW_Print(" ENERGY_PB = %lx \r\n ", energy_pb);
    HLW811X_HW_Print("\r\n");
}

void HLW811X_LOG_CalFactor(unsigned int rmsuc, unsigned int rmsiac, unsigned int rmsibc,
                       unsigned int powerpac, unsigned int powerpbc,
                       unsigned int energyac, unsigned int energybc, unsigned int powersc)
{
    HLW811X_HW_Print(" RMSUC = %x \r\n ", rmsuc);
    HLW811X_HW_Print(" RMSIAC = %x \r\n ", rmsiac);
    HLW811X_HW_Print(" RMSIBC = %x \r\n ", rmsibc);
    HLW811X_HW_Print(" POWERPAC = %x \r\n ", powerpac);
    HLW811X_HW_Print(" POWERPBC = %x \r\n ", powerpbc);
    HLW811X_HW_Print(" ENERGYAC = %x \r\n", energyac);
    HLW811X_HW_Print(" ENERGYBC = %x \r\n ", energybc);
    HLW811X_HW_Print(" POWERSC = %x \r\n ", powersc);
    HLW811X_HW_Print("\r\n");
}

void HLW811X_LOG_Config_Reg(unsigned int syscon, unsigned int emucon, unsigned int emucon2)
{
    HLW811X_HW_Print(" SYSCON = %x \r\n ", syscon);
    HLW811X_HW_Print(" EMUCON = %x\r\n", emucon);
    HLW811X_HW_Print(" EMUCON2 = %x\r\n", emucon2);
    HLW811X_HW_Print("\r\n");
    HLW811X_HW_Print("\r\n");
}
