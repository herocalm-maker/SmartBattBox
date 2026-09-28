/**
 * @file    drv_relaysw.h
 * @brief   继电器/光耦驱动接口 (Driver 层)
 *
 * 6 路开关控制 (5 继电器 + 1 光耦), 含安全断电时序.
 *
 * 一览 (引脚见 Board.h):
 *   - RELAY_CHG_IN:  充电器输入继电器
 *   - RELAY_CHG_OUT: 充电器输出继电器
 *   - RELAY_AC_CTRL: AC 直接输出继电器
 *   - RELAY_INV_OUT: 逆变器输出继电器
 *   - RELAY_INV_PRO: 逆变器预充继电器 RE5
 *   - RELAY_INV_IN:  外部直流主接触器控制
 *
 * @note 引脚定义见 Board.h; 初始化入口 drv_relay_init()
 */
#ifndef __DRV_RELAYSW_H__
#define __DRV_RELAYSW_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ==================== 继电器 ID 枚举 ==================== */

typedef enum {
   RELAY_CHG_IN  = 0,   /* PD2 充电器输入 */
   RELAY_CHG_OUT = 1,   /* PD3 充电器输出 */
   RELAY_AC_CTRL = 2,   /* PD1 AC 直接输出 */
   RELAY_INV_OUT = 3,   /* PD0 逆变器输出 */
   RELAY_INV_PRO = 4,   /* PD4 逆变器预充继电器 RE5 */
   RELAY_INV_IN  = 5,   /* 外部直流主接触器控制 */
   RELAY_CNT
} relay_id_enum;

/* ==================== 接口函数 ==================== */

/*********************************************************************
 * @fn      drv_relay_init
 * @brief   初始化继电器控制引脚 (推挽输出, 默认全部断开)
 * @return  none
********************************************************************/
extern void drv_relay_init(void);

/*********************************************************************
 * @fn      drv_relay_on
 * @brief   吸合指定继电器
 * @param   id  继电器 ID
 * @return  bool true=成功 false=ID 无效
********************************************************************/
extern bool drv_relay_on(relay_id_enum id);

/*********************************************************************
 * @fn      drv_relay_off
 * @brief   断开指定继电器
 * @param   id  继电器 ID
 * @return  bool true=成功 false=ID 无效
********************************************************************/
extern bool drv_relay_off(relay_id_enum id);

/*********************************************************************
 * @fn      drv_relay_is_on
 * @brief   查询指定继电器当前状态
 * @param   id  继电器 ID
 * @return  bool true=吸合 false=断开; ID 无效返回 false
********************************************************************/
extern bool drv_relay_is_on(relay_id_enum id);

/*********************************************************************
 * @fn      drv_relay_all_off
 * @brief   安全时序断开全部继电器
 * @note    紧急全断；正常切换的释放等待由主任务负责。
 * @return  none
********************************************************************/
extern void drv_relay_all_off(void);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_RELAYSW_H__ */
