/**
 * @file    hlw8112_reader.h
 * @brief   HLW8112/HLW8110 非阻塞数据读取状态机接口
 * @details 全局变量方案：状态变量由 extern 全局变量承载（定义在 hlw8112_reader.c），
 *          函数无 ctx 参数，通过全局变量直接操作。
 *
 * 使用流程 (见 test.c):
 *   1. HLW811X_Reader_InitChip();            // 主循环频繁调用
 *      while (!HLW811X_Reader_IsInitDone()) {}
 *   2. HLW811X_Reader_Start();               // 启动一次测量
 *      HLW811X_Reader_Run();
 *      if (HLW811X_Reader_IsReady()) {
 *          // 读取 F_AC_V, F_AC_I_A 等全局变量
 *      }
 *   3. 定时器中断 (2ms): HLW811X_Reader_TimerIRQ();
 *
 * @version V2.1 全局变量方案（C51 兼容）
 * @date    2026-06-05
 */

#ifndef __HLW8112_READER_H
#define __HLW8112_READER_H

/* #include "hlw8112_data.h"       /* 枚举定义 + extern 全局变量声明 */
/* #include "hlw8112_regs.h"       /* 寄存器地址宏 */

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================
 * 函数声明（无 ctx 参数，操作全局变量）
 *============================================================*/

void HLW811X_Reader_Reset(void);
void HLW811X_Reader_Start(void);
void HLW811X_Reader_Run(void);
u8   HLW811X_Reader_IsReady(void);
void HLW811X_Reader_TimerIRQ(void);
void HLW811X_Reader_InitChip(void);
u8   HLW811X_Reader_IsInitDone(void);
u32  HLW811X_Reader_GetErrors(void);
void HLW811X_Reader_PrintErrors(void);

#ifdef __cplusplus
}
#endif

/* #endif /* __HLW8112_READER_H */
