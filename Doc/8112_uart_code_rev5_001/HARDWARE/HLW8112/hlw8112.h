/**
 * @file    hlw8112.h
 * @brief   HLW8110/HLW8112 驱动聚合入口
 * @details 用户只需包含此文件即可使用全部功能。
 *
 * @version V4.2 全局变量方案（C51 兼容）
 * @date    2026-06-05
 */

#ifndef __HLW8112_H
#define __HLW8112_H

#include "hlw8112_config.h"
#include "hlw8112_hw.h"
#include "hlw8112_port.h"
/* #include "hlw8112_data.h"       /* extern 全局变量 + 枚举定义 */
#include "hlw8112_regs.h"
#include "hlw8112_calib.h"
#include "hlw8112_calc.h"
/* #include "hlw8112_protocol.h"   /* 协议函数 + 缓冲区全局变量 */
/* #include "hlw8112_reader.h"     /* 状态机 API */
#include "hlw8112_log.h"

/* #endif /* __HLW8112_H */
