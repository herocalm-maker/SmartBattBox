/**
 * @file    hlw8112_log.h
 * @brief   HLW8110/HLW8112 调试日志输出函数声明
 * @details 封装所有调试输出函数，通过 HLW811X_HW_Print 宏调用底层输出。
 *          关闭日志：在 hlw8112_config.h 中设置 HLW811X_LOG_ENABLE=0
 * @note    实现文件: hlw8112_log.c
 * @version V1.1
 * @date    2026-06-05 更新 HW_Print 引用
 */

#ifndef __HLW8112_LOG_H
#define __HLW8112_LOG_H

#if HLW811X_LOG_ENABLE

void HLW811X_LOG_CheckSum(unsigned int checksum);
void HLW811X_LOG_CheckSum_OK(void);
void HLW811X_LOG_CheckSum_Error(void);
void HLW811X_LOG_CalFactor_Fail(void);
void HLW811X_LOG_RegConfig_Error(void);
void HLW811X_LOG_Angle_Reg(unsigned long angle_reg);
void HLW811X_LOG_Angle_Reg_Error(void);
void HLW811X_LOG_Current_Lead(float angle);
void HLW811X_LOG_Current_Lag(float angle);

/* 测量数据输出 */
void HLW811X_LOG_MeterData_Header(void);
void HLW811X_LOG_Channel_A_Data(float voltage, float current, float power,
                                float apparent_power, float energy);
void HLW811X_LOG_Channel_B_Data(float voltage, float current,
                                float power, float energy);
void HLW811X_LOG_PF_Freq_Angle(float pf, float freq, float angle);
void HLW811X_LOG_Raw_RegData(unsigned long rmsu, unsigned long rmsia,
                             unsigned long powerpa, unsigned long energy_pa,
                             unsigned long rmsib, unsigned long powerpb,
                             unsigned long energy_pb);
void HLW811X_LOG_CalFactor(unsigned int rmsuc, unsigned int rmsiac,
                           unsigned int rmsibc, unsigned int powerpac,
                           unsigned int powerpbc, unsigned int energyac,
                           unsigned int energybc, unsigned int powersc);
void HLW811X_LOG_Config_Reg(unsigned int syscon, unsigned int emucon,
                            unsigned int emucon2);

/* #endif /* HLW811X_LOG_ENABLE */

/* #endif /* __HLW8112_LOG_H */
