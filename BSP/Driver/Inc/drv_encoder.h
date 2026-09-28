/**
 * @file    drv_encoder.h
 * @brief   旋转编码器驱动 (Driver 层)
 *
 * 功能:
 *   - EC11 编码器 A/B 相检测
 *   - 累积旋转增量 (正转+1, 反转-1)
 *   - 20ms 周期轮询采样
 *
 * 引脚: PC12 (A相) / PC11 (B相)
 */
#ifndef __DRV_ENCODER_H__
#define __DRV_ENCODER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 初始化编码器 GPIO */
extern void drv_encoder_init(void);

/* 反初始化 (释放 GPIO) */
extern void drv_encoder_deinit(void);

/* 轮询扫描 (20ms 周期调用, 累积增量) */
extern void drv_encoder_scan(void);

/* 读取并清除累积增量 */
extern int8_t drv_encoder_read(void);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_ENCODER_H__ */
