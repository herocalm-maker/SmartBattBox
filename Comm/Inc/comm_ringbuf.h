/**
 * @file    comm_ringbuf.h
 * @brief   环形缓冲区 (Comm 层)
 *
 * 设计约定:
 *   - 单生产者/单消费者免锁: 写侧只更新 tail, 读侧只更新 head
 *   - 容量约定: 实际可用容量 = size - 1 (FIFO 经典约定)
 *
 * 典型用法:
 *   static uint8_t s_buf[256];
 *   static comm_ringbuf_t s_rb;
 *   comm_ringbuf_init(&s_rb, s_buf, sizeof(s_buf));
 *   comm_ringbuf_push(&s_rb, data, n);
 *   ...
 *   uint8_t tmp[16];
 *   uint16_t n = comm_ringbuf_pop(&s_rb, tmp, sizeof(tmp));
 */
#ifndef __COMM_RINGBUF_H__
#define __COMM_RINGBUF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>


typedef struct {
    uint8_t         *buf;       /* 缓冲区指针 (用户提供) */
    uint16_t         size;      /* 缓冲区总大小 (字节) */
    volatile uint16_t head;     /* 模块内部实现 */
    volatile uint16_t tail;     /* 模块内部实现 */
} comm_ringbuf_t;


/* 初始化 */
extern void comm_ringbuf_init(comm_ringbuf_t *rb, uint8_t *buf, uint16_t size);

/* 复位 */
extern void comm_ringbuf_reset(comm_ringbuf_t *rb);

/* 向队列写入数据 (单生产者一侧, 不关中断). 返回实际写入字节数, 队列满时可能小于 len */
extern uint16_t comm_ringbuf_push(comm_ringbuf_t *rb, const uint8_t *data, uint16_t len);

/* 从队列读取并移除数据 (单消费者一侧, 不关中断). 返回实际读取字节数 */
extern uint16_t comm_ringbuf_pop(comm_ringbuf_t *rb, uint8_t *out, uint16_t len);

/* ringbuf peek */
extern uint16_t comm_ringbuf_peek(const comm_ringbuf_t *rb, uint8_t *out, uint16_t len);

/* 查询队列当前数据字节数 */
extern uint16_t comm_ringbuf_size(const comm_ringbuf_t *rb);

/* 查询队列剩余可用空间 (字节, 实际可用 = size - 1 - 当前数据) */
extern uint16_t comm_ringbuf_free(const comm_ringbuf_t *rb);

/* 判断是否empty */
extern bool comm_ringbuf_is_empty(const comm_ringbuf_t *rb);

/* 判断是否full */
extern bool comm_ringbuf_is_full(const comm_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif
#endif /* __COMM_RINGBUF_H__ */
