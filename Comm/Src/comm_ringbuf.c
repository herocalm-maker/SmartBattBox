/**
 * @file    comm_ringbuf.c
 * @brief   环形缓冲区 (Comm 层)
 *
 * 实现说明:
 *   - 经典 FIFO head/tail 模型, 容量 = size - 1
 *   - 单生产者/单消费者免锁: 写侧只更新 tail, 读侧只更新 head
 */
#include "comm_ringbuf.h"
#include <stddef.h>             /* NULL */

//******************************************************************************************************
//  函数实现
//******************************************************************************************************/

/*********************************************************************
 * @fn      comm_ringbuf_init
 * @brief   初始化环形缓冲区, 绑定底层存储并清空读写指针.
 * @param   rb    队列指针
 * @param   buf   底层存储数组 (调用方分配, 生存期需覆盖队列使用期)
 * @param   size  存储数组大小 (有效容量 = size - 1)
 ********************************************************************/
void comm_ringbuf_init(comm_ringbuf_t *rb, uint8_t *buf, uint16_t size)
{
    if (rb == NULL) 
      return;
    rb->buf  = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

/*********************************************************************
 * @fn      comm_ringbuf_reset
 * @brief   清空队列 (读写指针归零), 不改动底层存储绑定.
 * @param   rb    队列指针
 ********************************************************************/
void comm_ringbuf_reset(comm_ringbuf_t *rb)
{
    if (rb == NULL) 
      return;
    rb->head = 0;
    rb->tail = 0;
}

/*********************************************************************
 * @fn      comm_ringbuf_push
 * @brief   向队列写入数据 (单生产者/单消费者场景, 不关中断).
 * @note    生产者与消费者各占一侧时天然并发安全 (含 ISR→任务):
 *          写侧只更新 tail, 读侧只更新 head, 且均为单点更新.
 * @param   rb    队列指针
 * @param   data  数据指针
 * @param   len   要写入的字节数
 * @return  uint16_t 实际写入字节数 (队列满时可能小于 len)
 ********************************************************************/
uint16_t comm_ringbuf_push(comm_ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    if (rb == NULL || rb->buf == NULL || len == 0)
      return 0;

    uint16_t free = rb->size - 1u - comm_ringbuf_size(rb);
    if (len > free)
      len = free;

    uint16_t tail = rb->tail;
    for (uint16_t i = 0; i < len; i++)
    {
        rb->buf[tail] = data[i];
        tail = (uint16_t)((tail + 1u) % rb->size);
    }
    rb->tail = tail;     /* 单点更新, 消费者读到的 tail 始终一致 */
    return len;
}

/*********************************************************************
 * @fn      comm_ringbuf_pop
 * @brief   从队列读取并移除数据 (消费者在任务上下文调用).
 * @param   rb    队列指针
 * @param   out   输出缓冲区 (NULL=仅丢弃数据不移出)
 * @param   len   要读取的字节数
 * @return  uint16_t 实际读取字节数 (队列空时为 0)
 ********************************************************************/
uint16_t comm_ringbuf_pop(comm_ringbuf_t *rb, uint8_t *out, uint16_t len)
{
    if (rb == NULL || rb->buf == NULL || len == 0)
      return 0;

    uint16_t avail = comm_ringbuf_size(rb);
    if (len > avail)
      len = avail;

    uint16_t head = rb->head;
    for (uint16_t i = 0; i < len; i++)
    {
        if (out)
          out[i] = rb->buf[head];
        head = (uint16_t)((head + 1u) % rb->size);
    }
    rb->head = head;
    return len;
}

/*********************************************************************
 * @fn      comm_ringbuf_peek
 * @brief   只读查看队头数据, 不移动读指针 (协议帧解析用).
 * @note    典型用法: peek 整帧头/帧长后再 pop, 避免半帧被消费.
 * @param   rb    队列指针
 * @param   out   输出缓冲区
 * @param   len   要查看的字节数
 * @return  uint16_t 实际拷贝字节数 (队列数据不足或参数非法时为 0)
 ********************************************************************/
uint16_t comm_ringbuf_peek(const comm_ringbuf_t *rb, uint8_t *out, uint16_t len)
{
    if (rb == NULL || rb->buf == NULL || out == NULL || len == 0) 
      return 0;

    uint16_t avail = comm_ringbuf_size(rb);
    if (len > avail) 
      len = avail;

    uint16_t head = rb->head;
    for (uint16_t i = 0; i < len; i++)
    {
        out[i] = rb->buf[head];
        head = (uint16_t)((head + 1u) % rb->size);
    }
    return len;
}

/*********************************************************************
 * @fn      comm_ringbuf_size
 * @brief   查询队列中当前驻留的数据字节数.
 * @param   rb    队列指针
 * @return  uint16_t 已写入且未被读走的字节数
 ********************************************************************/
uint16_t comm_ringbuf_size(const comm_ringbuf_t *rb)
{
    if (rb == NULL) 
      return 0;
    uint16_t tail = rb->tail;
    uint16_t head = rb->head;
    return (uint16_t)((tail - head + rb->size) % rb->size);
}

/*********************************************************************
 * @fn      comm_ringbuf_free
 * @brief   查询队列剩余可写字节数.
 * @param   rb    队列指针
 * @return  uint16_t 剩余空间 (有效容量 size-1 再减去已占用)
 ********************************************************************/
uint16_t comm_ringbuf_free(const comm_ringbuf_t *rb)
{
    if (rb == NULL) 
      return 0;
    return (uint16_t)(rb->size - 1u - comm_ringbuf_size(rb));
}

/*********************************************************************
 * @fn      comm_ringbuf_is_empty
 * @brief   判断队列是否为空.
 * @param   rb    队列指针
 * @return  true=空 (或 rb 为 NULL)  false=有数据
 ********************************************************************/
bool comm_ringbuf_is_empty(const comm_ringbuf_t *rb)
{
    if (rb == NULL)
      return true;
    return (rb->head == rb->tail);
}

/*********************************************************************
 * @fn      comm_ringbuf_is_full
 * @brief   判断队列是否已满 (还需再写入 1 字节即满).
 * @param   rb    队列指针
 * @return  true=满  false=未满 (或 rb 为 NULL)
 ********************************************************************/
bool comm_ringbuf_is_full(const comm_ringbuf_t *rb)
{
    if (rb == NULL) 
      return false;
    uint16_t next_tail = (uint16_t)((rb->tail + 1u) % rb->size);
    return (next_tail == rb->head);
}
