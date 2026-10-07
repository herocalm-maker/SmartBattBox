/**
 * @file    Board.h
 * @brief   板级硬件配置总表 — 单一事实来源 (Single Source of Truth)
 *
 * 本文件集中定义移动电池逆变器控制板的所有硬件配置参数,
 * 对应原理图: Doc/移动电池逆变器电路原理图_20260706.pdf (6 页)
 *
 * 架构层级: BSP (Board Support Package)
 *   所有 Driver/Module/Service/App 层应通过本文件获取硬件常量,
 *   禁止在各层散落硬编码引脚号/地址/参数.
 *
 * MCU: STM32F407VGT6 (Cortex-M4F, 100-pin LQFP, 1MB Flash, 192KB RAM)
 *
 * 主控电路连接框架 (原理图 PAGE 1):
 *
 *   ┌──────────┐   UART1  PA9/PA10     ┌──────────┐
 *   │ PC/DEBUG │◄──────────────────────►│          │
 *   └──────────┘                        │          │   I2C1   PB6/PB7    ┌──────────┐
 *   ┌──────────┐   UART6  PC6/PC7      │          │◄───────────────────►│ MAX17263 │
 *   │ 蓝牙PB-03F│◄──────────────────────►│          │   ALRT   PB8        │ 电量计U19│
 *   └──────────┘                        │          │                      └──────────┘
 *   ┌──────────┐   UART4  PA0/PA1      │          │   SPI2  PB13/14/15   ┌──────────┐
 *   │ 串口显示屏│◄──────────────────────►│  STM32   │◄───────────────────►│ W25Q128  │
 *   └──────────┘                        │  F407    │   CS/ WP/ RST        │ Flash U1 │
 *   ┌──────────┐   UART2  PD5/PD6      │   VGT6   │   PB10/PB11/PB12    └──────────┘
 *   │HLW8110 U8│◄──────────────────────►│          │
 *   │输入计量  │                        │          │   ADC1: PA3/PA4/PA5/PA6
 *   └──────────┘                        │          │   (充电电压/充温/池温/逆温)
 *   ┌──────────┐   UART3  PD8/PD9      │          │
 *   │HLW8110 U9│◄──────────────────────►│          │   GPIO: 5路继电器+接触器
 *   │输出计量  │                        │          │         风扇×2/蜂鸣器/LED×3
 *   └──────────┘                        │          │         按键×4/编码器/充电唤醒
 *                                       └──────────┘
 *
 * 电源框架 (原理图 PAGE 6):
 *   磷酸铁锂 8S 25.2V ──► TPS5430 ──► +12V ──► TPS5430 ──► +5V
 *                         (U13)              (U16)
 *   +5V ──► HT7533-1 ──► 3V3U (MCU)     +5V ──► HT7533-1 ──► 3V3A (模拟)
 *           (U14)                              (U15)
 *   +5V ──► HT7533-1 ──► 3V3D (蓝牙)     +5V ──► HT7533-1 ──► 3V3G (电量计)
 *           (U12)                              (U20)
 */
#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"       /* STM32 HAL (GPIOA, GPIO_PIN_x 等基础类型) */
#include <stdint.h>
#include <stdbool.h>

//******************************************************************************************************
//  1. GPIO 引脚分配总表
//
//  每个引脚定义两件套宏 (Board.h 为引脚唯一事实源, 业务代码禁止直接引用 main.h 标签):
//    BOARD_xxx_PORT   端口 (GPIO_TypeDef*)
//    BOARD_xxx_PIN    引脚号 (uint16_t, 可多脚 OR)
//  调用形如: bsp_gpio_write(BOARD_PWR_EN_PORT, BOARD_PWR_EN_PIN, GPIO_PIN_SET)
//******************************************************************************************************/

/* ===== 强电控制输出 (继电器/接触器, 经 S8050 三极管驱动) ===== */
/*  网络标号        | 引脚 | 驱动器件        | 功能                 | 原理图位置 */
#define BOARD_CHG_IN_PORT      GPIOD       /* PD2  RE1 G2RL-2-DC12 充电器输入控制 */
#define BOARD_CHG_IN_PIN       GPIO_PIN_2
#define BOARD_CHG_OUT_PORT     GPIOD       /* PD3  RE4 G2RL-2-DC12 充电器输出控制 */
#define BOARD_CHG_OUT_PIN       GPIO_PIN_3
#define BOARD_AC_CTRL_PORT     GPIOD       /* PD1  RE2 G2RL-2-DC12 AC直接输出控制 */
#define BOARD_AC_CTRL_PIN      GPIO_PIN_1
#define BOARD_INV_OUT_PORT     GPIOD       /* PD0  RE3 G2RL-2-DC12 逆变器输出控制 */
#define BOARD_INV_OUT_PIN      GPIO_PIN_0
#define BOARD_INV_PRO_PORT     GPIOD       /* PD4  RE5 G2RL-2-DC12 逆变器输入继电器 */
#define BOARD_INV_PRO_PIN      GPIO_PIN_4
#define BOARD_INV_IN_PORT      GPIOD       /* PD7  U21 PC817C 外部直流主接触器控制 (0706: PE14->PD7) */
#define BOARD_INV_IN_PIN       GPIO_PIN_7
/* ===== 电源与外设控制 ===== */
#define BOARD_PWR_EN_PORT      GPIOB       /* PB4  主电源使能 (DC-DC/LDO 通断, 自锁) */
#define BOARD_PWR_EN_PIN       GPIO_PIN_4
#define BOARD_BLE_WKUP_PORT    GPIOA       /* PA2  蓝牙模组唤醒 */
#define BOARD_BLE_WKUP_PIN     GPIO_PIN_2
#define BOARD_FAN1_CTRL_PORT   GPIOD       /* PD10 散热风扇1 (Q18 YJL03N06A, 0706新增) */
#define BOARD_FAN1_CTRL_PIN    GPIO_PIN_10
#define BOARD_FAN2_CTRL_PORT   GPIOD       /* PD11 散热风扇2 (Q17 YJL03N06A, 0706新增) */
#define BOARD_FAN2_CTRL_PIN    GPIO_PIN_11
#define BOARD_BUZZER_PORT      GPIOD       /* PD13 蜂鸣器 (Q7 S8050, 0706: PE13->PD13) */
#define BOARD_BUZZER_PIN       GPIO_PIN_13
#define BOARD_ALARM_LED_PORT   GPIOD       /* PD14 报警灯 (Q19 S8050 RED, 0706新增) */
#define BOARD_ALARM_LED_PIN    GPIO_PIN_14
#define BOARD_LED1_PORT        GPIOC       /* PC4  信号指示 LED1 (GREEN) */
#define BOARD_LED1_PIN         GPIO_PIN_4
#define BOARD_LED2_PORT        GPIOC       /* PC5  信号指示 LED2 (BLUE) */
#define BOARD_LED2_PIN         GPIO_PIN_5
/* ===== W25Q128 SPI Flash 控制 (SPI2 + GPIO) ===== */
#define BOARD_FLASH_CS_PORT    GPIOB       /* PB10 W25Q128 CS# (低有效) */
#define BOARD_FLASH_CS_PIN     GPIO_PIN_10
#define BOARD_FLASH_WP_PORT    GPIOB       /* PB11 W25Q128 WP# (低写保护) */
#define BOARD_FLASH_WP_PIN     GPIO_PIN_11
#define BOARD_FLASH_RST_PORT   GPIOB       /* PB12 W25Q128 RESET# */
#define BOARD_FLASH_RST_PIN    GPIO_PIN_12
#define BOARD_SPI2_SCK_PORT    GPIOB       /* PB13 SPI2 时钟 */
#define BOARD_SPI2_SCK_PIN     GPIO_PIN_13

#define BOARD_SPI2_MISO_PIN    GPIO_PIN_14   /* PB14 SPI2 MISO */
#define BOARD_SPI2_MOSI_PIN    GPIO_PIN_15   /* PB15 SPI2 MOSI */

/* ===== 输入信号 ===== */
#define BOARD_ONOFF_KEY_PORT   GPIOB       /* PB3  开关机按键 (低有效) */
#define BOARD_ONOFF_KEY_PIN    GPIO_PIN_3
#define BOARD_ACIN_KEY_PORT    GPIOB       /* PB0  AC输入选择按键 (低有效) */
#define BOARD_ACIN_KEY_PIN     GPIO_PIN_0
#define BOARD_ACOUT_KEY_PORT   GPIOB       /* PB1  AC输出选择按键 (低有效) */
#define BOARD_ACOUT_KEY_PIN    GPIO_PIN_1
#define BOARD_K1_PORT          GPIOC       /* PC0  BYPASS/CHARGE 切换按键 (低有效) */
#define BOARD_K1_PIN           GPIO_PIN_0
#define BOARD_CODER1_PORT      GPIOC       /* PC12 编码器 A 相 */
#define BOARD_CODER1_PIN       GPIO_PIN_12
#define BOARD_CODER2_PORT      GPIOC       /* PC11 编码器 B 相 */
#define BOARD_CODER2_PIN       GPIO_PIN_11
#define BOARD_CHG_WKUP_PORT    GPIOB       /* PB5  充电唤醒 (高有效) */
#define BOARD_CHG_WKUP_PIN     GPIO_PIN_5
#define BOARD_GUAGE_ALRT_PORT  GPIOB       /* PB8  MAX17263 ALRT (低有效) */
#define BOARD_GUAGE_ALRT_PIN   GPIO_PIN_8
/* ===== ADC 采样引脚 ===== */
#define BOARD_CHG_AD_BACKUP_PORT  GPIOC    /* PC1 ADC1_IN11 充电模拟备份 (* 见分析说明) */
#define BOARD_CHG_AD_BACKUP_PIN   GPIO_PIN_1

#define BOARD_CHG_VOL_AD_PORT  GPIOA       /* PA3 ADC1_IN3  充电电压 */
#define BOARD_CHG_VOL_AD_PIN   GPIO_PIN_3

#define BOARD_CHG_TMP_AD_PORT  GPIOA       /* PA4 ADC1_IN4  充电器温度 NTC */
#define BOARD_CHG_TMP_AD_PIN   GPIO_PIN_4

#define BOARD_BAT_TMP_AD_PORT  GPIOA       /* PA5 ADC1_IN5  电池温度 NTC */
#define BOARD_BAT_TMP_AD_PIN   GPIO_PIN_5

#define BOARD_INV_TMP_AD_PORT  GPIOA       /* PA6 ADC1_IN6  逆变器温度 NTC */
#define BOARD_INV_TMP_AD_PIN   GPIO_PIN_6

/* ===== UART 引脚 ===== */
#define BOARD_UART1_GPIO_PORT  GPIOA       //UART1
#define BOARD_UART1_TX_PIN     GPIO_PIN_9
#define BOARD_UART1_RX_PIN     GPIO_PIN_10

#define BOARD_UART2_GPIO_PORT  GPIOD       // UART2
#define BOARD_UART2_TX_PIN     GPIO_PIN_5
#define BOARD_UART2_RX_PIN     GPIO_PIN_6

#define BOARD_UART3_GPIO_PORT  GPIOD       // UART3
#define BOARD_UART3_TX_PIN     GPIO_PIN_8
#define BOARD_UART3_RX_PIN     GPIO_PIN_9

#define BOARD_UART4_GPIO_PORT  GPIOA       // UART4
#define BOARD_UART4_TX_PIN     GPIO_PIN_0
#define BOARD_UART4_RX_PIN     GPIO_PIN_1

#define BOARD_UART6_GPIO_PORT  GPIOC       // UART6
#define BOARD_UART6_TX_PIN     GPIO_PIN_6
#define BOARD_UART6_RX_PIN     GPIO_PIN_7

/* ===== I2C 引脚 ===== */
#define BOARD_I2C1_SCL_PORT    GPIOB       /* PB6  I2C1 SCL -> MAX17263 */
#define BOARD_I2C1_SCL_PIN     GPIO_PIN_6

#define BOARD_I2C1_SDA_PORT    GPIOB       /* PB7  I2C1 SDA -> MAX17263 */
#define BOARD_I2C1_SDA_PIN     GPIO_PIN_7

//******************************************************************************************************
//  2. UART 配置
//******************************************************************************************************/
#define BOARD_UART_BAUD_DEBUG     115200u
#define BOARD_UART_BAUD_DISPLAY   115200u
#define BOARD_UART_BAUD_BLE       115200u

/* DMA 通道分配 (RX/TX 均启用 DMA NORMAL):
 *   USART1 → DMA2_Stream2/Ch4 (RX), DMA2_Stream7/Ch4 (TX)
 *   USART2 → DMA1_Stream5/Ch4 (RX), DMA1_Stream6/Ch4 (TX)
 *   USART3 → DMA1_Stream1/Ch4 (RX), DMA1_Stream3/Ch4 (TX)
 *   UART4  → DMA1_Stream2/Ch4 (RX), DMA1_Stream4/Ch4 (TX)
 *   USART6 → DMA2_Stream1/Ch5 (RX), DMA2_Stream6/Ch5 (TX)
 */

//******************************************************************************************************
//  3. SPI 配置
//******************************************************************************************************/
#define BOARD_SPI_BAUDRATE_PRESCALER  SPI_BAUDRATEPRESCALER_4   /* APB1 42MHz / 4 = 10.5MHz (W25Q128 max 104MHz) */
#define BOARD_SPI_DATASIZE            SPI_DATASIZE_8BIT
#define BOARD_SPI_CPOL                SPI_POLARITY_LOW   /* Mode 0 (CPOL=0, CPHA=0) */
#define BOARD_SPI_CPHA                SPI_PHASE_1EDGE

//******************************************************************************************************
//  4. ADC 通道映射 (ADC1, 12bit, DMA 循环)
//******************************************************************************************************/

/* Rank | 通道      | ADC_IN  | 引脚 | 功能           | 采样时间 */
/*  1   | CH_11     | IN11    | PC1  | 充电模拟备份 *  | 480 cycles (* 参考正点原子例程, 最稳定) */
/*  2   | CH_3      | IN3     | PA3  | 充电电压        | 480 cycles */
/*  3   | CH_4      | IN4     | PA4  | 充电器温度 NTC  | 480 cycles */
/*  4   | CH_5      | IN5     | PA5  | 电池温度 NTC    | 480 cycles */
/*  5   | CH_6      | IN6     | PA6  | 逆变器温度 NTC  | 480 cycles */

#define BOARD_ADC_VREF_MV         3300u    /* ADC 参考电压 = 3V3A */
#define BOARD_ADC_RESOLUTION      4095u    /* 12bit 最大值 (0..4095, 用于电压换算 raw×VREF/4095) */
#define BOARD_ADC_SAMPLETIME      ADC_SAMPLETIME_480CYCLES  /* 参考正点原子19-3例程: 480周期最稳定 */
#define BOARD_ADC_CHANNEL_CNT     5u       /* DMA 通道数 (含 PC1) */

//******************************************************************************************************
//  5. 定时器配置
//******************************************************************************************************/
/* TIM4: 50ms 中断, Ft=84MHz, Tout=((ARR+1)*(PSC+1))/Ft
 *   PSC=4199 → 分频后 20kHz, ARR=999 → 1000/20000 = 0.05s */
#define BOARD_TIM4_PSC          4199u    /* 预分频: 84MHz / 4200 = 20kHz */
#define BOARD_TIM4_ARR          999u     /* 重装值:  20kHz / 1000 = 20Hz (50ms) */

//******************************************************************************************************
//  6. IC 器件参数
//******************************************************************************************************/
/* ===== W25Q128 SPI Flash (U1, SPI2) ===== */
#define BOARD_W25Q128_SIZE_BYTES          (16u * 1024u * 1024u)  /* 16MB = 128Mbit */
#define BOARD_W25Q128_SECTOR_SIZE         4096u
#define BOARD_W25Q128_PAGE_SIZE           256u
#define BOARD_W25Q128_EXPECTED_ID         0xEF4018u   /* JEDEC ID: EF=WinBond, 40=type, 18=128Mbit */

/* ===== IWDG 独立看门狗 =====
 *   LSI ~32kHz, Prescaler=64 → 500Hz 计数, Reload=4095 → 超时 ~8.19s
 *   MainTask 10ms 周期喂狗, 余量充足.
 *   注: IWDG 一旦启动不可关闭 (硬件特性), 故无 deinit.
 *
 *   BOARD_WDG_ENABLE=0: 调试阶段关闭看门狗 (bsp_wdg_init 变为空操作,
 *                       bsp_wdg_refresh 也空操作). 调通后再置 1 启用. */
#define BOARD_WDG_ENABLE                1u   /* 启用 IWDG, 由 monitor_tick 集中喂狗 */

//******************************************************************************************************
//  7. NTC 温度传感器参数 (PA4/PA5/PA6 三路, BAV99 保护)
//******************************************************************************************************/

#define BOARD_NTC_PULLUP_OHM       10000u          /* 上拉电阻 10K (3V3A 供电) */
#define BOARD_NTC_R0_OHM           10000u          /* 25°C 标称阻值 10K */
#define BOARD_NTC_T0_K             298.15f         /* 25°C = 298.15K */
#define BOARD_NTC_BETA             3950.0f         /* B 值 3950 */

//******************************************************************************************************
//  8. 保护阈值 (温度/风扇联动)
//******************************************************************************************************/

#define BOARD_PROT_INV_TMP_WARN_TH    600     /* 逆变器 60℃ 预警 (0.1℃) */
#define BOARD_PROT_INV_TMP_FAULT_TH   750     /* 逆变器 75℃ 故障 */
#define BOARD_PROT_BAT_TMP_WARN_TH    450     /* 电池 45℃ 预警 */
#define BOARD_PROT_BAT_TMP_FAULT_TH   550     /* 电池 55℃ 故障 */
#define BOARD_PROT_CHG_TMP_WARN_TH    600     /* 充电器 60℃ 预警 */
#define BOARD_PROT_CHG_TMP_FAULT_TH   750     /* 充电器 75℃ 故障 */
#define BOARD_PROT_FAN_ON_TH          350     /* 任一温度 > 35℃ 开风扇 */
#define BOARD_PROT_FAN_OFF_TH         300     /* 全部温度 < 30℃ 关风扇 (迟滞) */

//******************************************************************************************************
//  8.1 输出过载保护 (TextAlarm OVERLOAD 显示)
//******************************************************************************************************/

/* 额定功率: 按 ZEMCT131 电流互感器(5A 级)×220V≈1100W 满量程取整 1000W, 待硬件确认 */
#define BOARD_PWR_RATED_W        1000u   /* 系统额定输出功率 W */
/* 过载阈值 = 额定 × 1.1 (功能清单 E016: P_out > 额定功率 × 1.1 判系统过载) */
#define BOARD_PWR_OVERLOAD_W     (BOARD_PWR_RATED_W * 11 / 10)

//******************************************************************************************************
//  9. 按键/蜂鸣器时序参数
//******************************************************************************************************/

#define BOARD_KEY_DEBOUNCE_TICKS       3u      /* 去抖 3×20ms=60ms (V3 按键重构时补齐扫描周期/长按阈值) */
#define BOARD_BUZ_TICK_SHORT           1u      /* 短滴 50ms */
#define BOARD_BUZ_TICK_LONG            10u     /* 长滴 500ms */
#define BOARD_BUZ_TICK_ALARM           4u      /* 报警 200ms on/off */

//******************************************************************************************************
//  分析说明 (0706 原理图 vs 当前代码)
//******************************************************************************************************
//
// [问题 1] PC1_CHG_ADIN11 (ADC1_IN11) — ⚠ 待确认
//   原理图: 0706 版 PAGE 2 的 MCU 引脚表中 PC1 标注为 "ETH_MDC|ADC123_IN11",
//           未连接到任何模拟信号; PA3 已承担"充电电压采样"功能。
//   代码:   main.h:77 定义了 PC1_CHG_ADIN11, adc.c:79 将其配置为 ADC Rank 1。
//   判断:   PC1 在 0706 原理图中无对应网络, 疑为旧版残留。
//           建议确认硬件是否实际走线; 若未走线应从 ADC 序列中移除,
//           BSP_ADC_CH_CNT 改为 4, 可节省 1 个 DMA 通道开销。
//
// [问题 2] HLW8110 UART 波特率 — ✓ 已修复
//   原理图: U8/U9 为 HLW8110 (非 HLW8112), UART 通讯。
//   原代码: usart.c 中 UART2/UART3 均配置为 115200 bps。
//   问题:   HLW8110/HLW8112 仅支持 9600/19200/38400 bps (SYSCON 寄存器控制),
//           不支持 115200。hlw8110_setup() 会写 SYSCON 配置 IC (默认不写, 保持 9600),
//           IC 使用默认 9600 bps, 与 UART 115200 不匹配 → 通信必定失败。
//           (参考代码 Doc/8112_uart_code_rev5_001/hlw8112_config.h:120 确认 HLW811X_UART_BAUD=9600)
//   修复:   drv_hlw8110.c 初始化波特率硬编码 9600 (匹配 IC 默认值)。
//           若需更高波特率, 应在 hlw8110_setup() 中写 SYSCON 寄存器切换 IC 波特率,
//           并同步修改 UART 波特率。
//
// [确认项] 0706 版引脚变更 — ✓ 已正确反映
//   PD7_INV_IN:    0706 由 PE14 改至 PD7 (main.h:107, bsp_io.h:49) ✓
//   PD13_Buzzer:   0706 由 PE13 改至 PD13 (main.h:116, bsp_io.h:89) ✓
//   PD14_ALARM_LED:0706 新增 (main.h:119, bsp_io.h:93) ✓
//   PD10_FAN1_CTRL:0706 新增 (main.h:110, bsp_io.h:63) ✓
//   PD11_FAN2_CTRL:0706 新增 (main.h:113, bsp_io.h:66) ✓
//
// [确认项] UART/I2C/SPI 外设分配 — ✓ 与原理图框架图一致
//   UART1=PC/DEBUG, UART2=输入计量, UART3=输出计量,
//   UART4=显示屏, UART6=蓝牙, I2C1=MAX17263, SPI2=W25Q128 ✓
//
// [确认项] MAX17263 I2C 地址 — ✓ 正确
//   7bit 地址 0x36 → 8bit 写 0x6C / 读 0x6D (drv_max17263.h, MAX1726x 家族固定地址) ✓
//
// [确认项] 电流采样电阻 — ✓ 正确
//   原理图 R114 = 1mR 1% 2512, 代码 MAX17263_RSENSE_MOHM=1 (drv_max17263.h) ✓
//

#ifdef __cplusplus
}
#endif
#endif /* __BOARD_H */
