//-----------------------------------------------------------------------------
// Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See LICENSE.txt for the text of the license.
//-----------------------------------------------------------------------------
// WebSerial UART implementation for Emscripten/WASM
// Uses SPSC ring buffers in shared memory.
//-----------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__

#include "comms.h"
#include "uart.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_RB_CAPACITY 65536u

typedef struct {
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    _Atomic uint32_t initialized;
    uint8_t data[UART_RB_CAPACITY];
} uart_ringbuf_t;

static uart_ringbuf_t g_uart_rx;
static uart_ringbuf_t g_uart_tx;
static uart_ringbuf_t g_uart_stdin;
static uint32_t timeout_value = 30;

static size_t rb_read(uart_ringbuf_t *rb, uint8_t *dst, size_t maxlen) {
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint32_t available = head - tail;

    if (available == 0) {
        return 0;
    }
    if (maxlen > available) {
        maxlen = available;
    }

    uint32_t tail_idx = tail % UART_RB_CAPACITY;
    size_t first = UART_RB_CAPACITY - tail_idx;
    if (first > maxlen) {
        first = maxlen;
    }

    memcpy(dst, &rb->data[tail_idx], first);

    size_t remaining = maxlen - first;
    if (remaining > 0) {
        memcpy(dst + first, &rb->data[0], remaining);
    }

    atomic_store_explicit(&rb->tail, tail + (uint32_t)maxlen, memory_order_release);
    return maxlen;
}

static size_t rb_write(uart_ringbuf_t *rb, const uint8_t *src, size_t len) {
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    uint32_t used = head - tail;
    uint32_t free_space = UART_RB_CAPACITY - used;

    if (free_space == 0) {
        return 0;
    }
    if (len > free_space) {
        len = free_space;
    }

    uint32_t head_idx = head % UART_RB_CAPACITY;
    size_t first = UART_RB_CAPACITY - head_idx;
    if (first > len) {
        first = len;
    }

    memcpy(&rb->data[head_idx], src, first);

    size_t remaining = len - first;
    if (remaining > 0) {
        memcpy(&rb->data[0], src + first, remaining);
    }

    atomic_store_explicit(&rb->head, head + (uint32_t)len, memory_order_release);
    return len;
}

int uart_read_stdin(void);

uintptr_t pm3_uart_rx_head_ptr(void);
uintptr_t pm3_uart_rx_tail_ptr(void);
uintptr_t pm3_uart_rx_buf_ptr(void);
uintptr_t pm3_uart_rx_initialized_ptr(void);
uintptr_t pm3_uart_tx_head_ptr(void);
uintptr_t pm3_uart_tx_tail_ptr(void);
uintptr_t pm3_uart_tx_buf_ptr(void);
uint32_t pm3_uart_rb_capacity(void);
uintptr_t pm3_uart_stdin_head_ptr(void);
uintptr_t pm3_uart_stdin_tail_ptr(void);
uintptr_t pm3_uart_stdin_buf_ptr(void);

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_rx_head_ptr(void) {
    return (uintptr_t)&g_uart_rx.head;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_rx_tail_ptr(void) {
    return (uintptr_t)&g_uart_rx.tail;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_rx_buf_ptr(void) {
    return (uintptr_t)&g_uart_rx.data[0];
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_rx_initialized_ptr(void) {
    return (uintptr_t)&g_uart_rx.initialized;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_tx_head_ptr(void) {
    return (uintptr_t)&g_uart_tx.head;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_tx_tail_ptr(void) {
    return (uintptr_t)&g_uart_tx.tail;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_tx_buf_ptr(void) {
    return (uintptr_t)&g_uart_tx.data[0];
}

EMSCRIPTEN_KEEPALIVE uint32_t pm3_uart_rb_capacity(void) {
    return UART_RB_CAPACITY;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_stdin_head_ptr(void) {
    return (uintptr_t)&g_uart_stdin.head;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_stdin_tail_ptr(void) {
    return (uintptr_t)&g_uart_stdin.tail;
}

EMSCRIPTEN_KEEPALIVE uintptr_t pm3_uart_stdin_buf_ptr(void) {
    return (uintptr_t)&g_uart_stdin.data[0];
}

int uart_read_stdin(void) {
    uint8_t c = 0;
    return rb_read(&g_uart_stdin, &c, 1) > 0 ? (int)c : 0;
}

int uart_reconfigure_timeouts(uint32_t value) {
    timeout_value = value;
    return PM3_SUCCESS;
}

uint32_t uart_get_timeouts(void) {
    return timeout_value;
}

serial_port uart_open(const char *pcPortName, uint32_t speed, bool silent) {
    (void)pcPortName;
    (void)speed;

    g_conn.send_via_local_ip = false;
    g_conn.send_via_ip = PM3_NONE;
    g_conn.uart_speed = speed;

    atomic_store(&g_uart_rx.head, 0);
    atomic_store(&g_uart_rx.tail, 0);
    atomic_store(&g_uart_tx.head, 0);
    atomic_store(&g_uart_tx.tail, 0);
    atomic_store(&g_uart_stdin.head, 0);
    atomic_store(&g_uart_stdin.tail, 0);

    atomic_store(&g_uart_rx.initialized, 1);
    atomic_store(&g_uart_tx.initialized, 1);
    atomic_store(&g_uart_stdin.initialized, 1);

    if (!silent) {
        printf("WebSerial ring buffers initialized\n");
    }

    return NULL;
}

void uart_close(const serial_port sp) {
    (void)sp;
}

int uart_receive(const serial_port sp, uint8_t *pbtRx, uint32_t pszMaxRxLen, uint32_t *pszRxLen) {
    (void)sp;
    *pszRxLen = 0;

    if (pszMaxRxLen == 0) {
        return PM3_SUCCESS;
    }

    uint32_t total_read = 0;
    double start_time = emscripten_get_now();
    int spin_count = 0;

    while (total_read < pszMaxRxLen) {
        size_t n = rb_read(&g_uart_rx, pbtRx + total_read, pszMaxRxLen - total_read);
        if (n > 0) {
            total_read += (uint32_t)n;
            start_time = emscripten_get_now();
            spin_count = 0;
            continue;
        }

        if (emscripten_get_now() - start_time > 500.0) {
            break;
        }

        if (spin_count < 1000) {
            spin_count++;
        } else {
            emscripten_thread_sleep(1);
        }
    }

    *pszRxLen = total_read;
    return PM3_SUCCESS;
}

int uart_send(const serial_port sp, const uint8_t *pbtTx, const uint32_t len) {
    (void)sp;
    return rb_write(&g_uart_tx, pbtTx, len) == len ? PM3_SUCCESS : PM3_EIO;
}

bool uart_set_speed(serial_port sp, const uint32_t uiPortSpeed) {
    (void)sp;
    (void)uiPortSpeed;
    return true;
}

uint32_t uart_get_speed(const serial_port sp) {
    (void)sp;
    return 115200;
}

bool uart_bind(void *socket, const char *bindAddrStr, const char *bindPortStr, bool isBindingIPv6) {
    (void)socket;
    (void)bindAddrStr;
    (void)bindPortStr;
    (void)isBindingIPv6;
    return false;
}

int uart_parse_address_port(char *addrPortStr, const char **addrStr, const char **portStr, bool *isIPv6) {
    (void)addrPortStr;
    (void)addrStr;
    (void)portStr;
    (void)isIPv6;
    return PM3_ENOTIMPL;
}

#endif
