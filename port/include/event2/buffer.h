// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent compatibility layer (event2/buffer.h).
 */

#ifndef MINI_EVENT2_BUFFER_H
#define MINI_EVENT2_BUFFER_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct evbuffer;

struct evbuffer_iovec {
    void  *iov_base;
    size_t iov_len;
};

struct evbuffer_ptr {
    ssize_t pos;    /* -1 when not found */
};

struct evbuffer *evbuffer_new(void);
void evbuffer_free(struct evbuffer *);

int    evbuffer_add(struct evbuffer *, const void *data, size_t len);
int    evbuffer_add_buffer(struct evbuffer *dst, struct evbuffer *src);
int    evbuffer_prepend(struct evbuffer *, const void *data, size_t len);

size_t evbuffer_get_length(const struct evbuffer *);

int    evbuffer_remove(struct evbuffer *, void *data, size_t len);
size_t evbuffer_remove_buffer(struct evbuffer *src, struct evbuffer *dst,
                              size_t len);
int    evbuffer_drain(struct evbuffer *, size_t len);

/* Zero-copy view of the first chain. With our contiguous implementation a
 * single iovec always exposes the whole buffer. Returns number of iovecs
 * filled (<= n_vec), 0 if buffer empty / len < 0. */
int    evbuffer_peek(struct evbuffer *, ssize_t len,
                     const struct evbuffer_ptr *start_at,
                     struct evbuffer_iovec *vec, int n_vec);

struct evbuffer_ptr evbuffer_search(struct evbuffer *, const char *what,
                                    size_t len, const struct evbuffer_ptr *start);

/* Write the whole buffer to fd (blocking send loop). Returns bytes written
 * or -1 on hard error. */
int    evbuffer_write(struct evbuffer *, int fd);

unsigned char *evbuffer_pullup(struct evbuffer *, ssize_t size);

#ifdef __cplusplus
}
#endif

#endif /* MINI_EVENT2_BUFFER_H */
