/**
 * @file    mod_key.h
 * @brief   按键管理模块 (Module 层)
 *
 * 按下确认制按键处理方案:
 *   - 四态状态机: IDLE → CONFIRM → PRESSED → LONG (20ms 轮询)
 *   - 短按: 去抖确认后立即触发, 不等释放
 *   - 长按: 按住达到阈值触发一次, 不连发
 *   - 支持组合键 (多键同时按下, 同一手势内已报按键去重)
 *   - 使用位标记区分长短按 (KEY_LONG_MARK = 0x80)
 *   - 编码器独立封装 (drv_encoder)
 */
#ifndef __MOD_KEY_H__
#define __MOD_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*--------------------------------------------------------------------
 * 按键索引
 *--------------------------------------------------------------------*/
#define KEY_IDX_ONOFF   0
#define KEY_IDX_ACIN    1
#define KEY_IDX_ACOUT   2
#define KEY_IDX_K1      3
#define KEY_IDX_CNT     4

/*--------------------------------------------------------------------
 * 按键值 (位掩码, 参考气泵项目 KEY_VALUE_*)
 *--------------------------------------------------------------------*/
#define KEY_VALUE_ONOFF   (1u << KEY_IDX_ONOFF)  /* 0x01 */
#define KEY_VALUE_ACIN    (1u << KEY_IDX_ACIN)   /* 0x02 */
#define KEY_VALUE_ACOUT   (1u << KEY_IDX_ACOUT)  /* 0x04 */
#define KEY_VALUE_K1      (1u << KEY_IDX_K1)     /* 0x08 */

/* 长按标记 (参考气泵项目 KEY_LONG_MARK) */
#define KEY_LONG_MARK     0x80u

/* 组合键值 (参考气泵项目 KEY_VALUE_LONG_UP_AND_DOWN) */
#define KEY_VALUE_ACIN_ACOUT  (KEY_VALUE_ACIN | KEY_VALUE_ACOUT)  /* 0x06 */

/* 长按事件值：业务代码直接使用命名宏，避免重复拼接长按标记。 */
#define KEY_LONG_VALUE_ONOFF      (KEY_LONG_MARK | KEY_VALUE_ONOFF)
#define KEY_LONG_VALUE_ACIN       (KEY_LONG_MARK | KEY_VALUE_ACIN)
#define KEY_LONG_VALUE_ACOUT      (KEY_LONG_MARK | KEY_VALUE_ACOUT)
#define KEY_LONG_VALUE_K1         (KEY_LONG_MARK | KEY_VALUE_K1)
#define KEY_LONG_VALUE_ACIN_ACOUT (KEY_LONG_MARK | KEY_VALUE_ACIN_ACOUT)


/*--------------------------------------------------------------------
 * 回调类型 (统一入口, 参数为按键码)
 * 按键码 = 按键值 (KEY_VALUE_*) | 长按标记 (KEY_LONG_MARK)
 *--------------------------------------------------------------------*/
typedef void (*mod_key_callback_t)(uint8_t key_code);

/*--------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------*/
extern bool mod_key_init(void);
extern void mod_key_register_callback(mod_key_callback_t cb);
extern void mod_key_poll_handler(void);

/**
 * @brief 查询某个按键当前是否处于按下状态 (原始电平, 不去抖)。
 * @param idx KEY_IDX_ONOFF / KEY_IDX_ACIN / KEY_IDX_ACOUT / KEY_IDX_K1
 * @return true: 按下 (低电平); false: 松开或参数非法。
 * @note  供关机流程使用: 长按触发关机后需确认按键已松开再断电,
 *        否则按键仍会把电源自锁重新拉通, 表现为"关机后立即重启"。
 */
extern bool mod_key_is_down(uint8_t idx);

#ifdef __cplusplus
}
#endif
#endif /* __MOD_KEY_H__ */
