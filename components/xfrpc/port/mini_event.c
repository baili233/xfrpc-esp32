// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: minimal libevent implementation over lwip sockets.
 *
 * Event model: single-task, select()-driven event loop.
 *  - event_base_dispatch() runs the loop on the calling task (the xfrpc task)
 *  - bufferevents are polled with select(); callbacks are dispatched inline
 *    (equivalent to libevent's default deferred callbacks in a single thread)
 *  - timers are kept in a plain list; the next deadline bounds the select
 *    timeout (capped at 100 ms so stop requests from other tasks are seen)
 *  - DNS resolution is blocking lwip getaddrinfo (accepted MVP tradeoff)
 *
 * Only the API surface used by xfrpc is implemented. See port/include/event2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/dns.h"
#include "event2/util.h"

#define MINI_SELECT_MAX_WAIT_MS 100   /* stay responsive to cross-task stops */
#define MINI_RECV_CHUNK         4096
#define MINI_RECV_MAX_PER_LOOP  (64 * 1024)
#define MINI_EVBUF_INIT_CAP    4096

/* ------------------------------------------------------------------ */
/* time helpers (monotonic)                                            */
/* ------------------------------------------------------------------ */

static int64_t mini_now_us(void)
{
    return esp_timer_get_time();
}

static int64_t tv_to_us(const struct timeval *tv)
{
    if (!tv)
        return -1;
    return (int64_t)tv->tv_sec * 1000000 + tv->tv_usec;
}

/* ------------------------------------------------------------------ */
/* evbuffer                                                            */
/* ------------------------------------------------------------------ */

struct evbuffer {
    uint8_t *data;
    size_t   len;
    size_t   cap;
};

static int evbuffer_reserve(struct evbuffer *buf, size_t need)
{
    if (buf->len + need <= buf->cap)
        return 0;
    size_t ncap = buf->cap ? buf->cap : MINI_EVBUF_INIT_CAP;
    while (ncap < buf->len + need)
        ncap *= 2;
    uint8_t *nd = realloc(buf->data, ncap);
    if (!nd)
        return -1;
    buf->data = nd;
    buf->cap = ncap;
    return 0;
}

struct evbuffer *evbuffer_new(void)
{
    return calloc(1, sizeof(struct evbuffer));
}

void evbuffer_free(struct evbuffer *buf)
{
    if (!buf)
        return;
    free(buf->data);
    free(buf);
}

int evbuffer_add(struct evbuffer *buf, const void *data, size_t len)
{
    if (!buf || (!data && len > 0))
        return -1;
    if (evbuffer_reserve(buf, len) < 0)
        return -1;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 0;
}

int evbuffer_add_buffer(struct evbuffer *dst, struct evbuffer *src)
{
    if (!dst || !src || dst == src)
        return -1;
    if (src->len == 0)
        return 0;
    if (evbuffer_add(dst, src->data, src->len) < 0)
        return -1;
    src->len = 0;
    return 0;
}

int evbuffer_prepend(struct evbuffer *buf, const void *data, size_t len)
{
    if (!buf || (!data && len > 0))
        return -1;
    if (len == 0)
        return 0;
    if (evbuffer_reserve(buf, len) < 0)
        return -1;
    memmove(buf->data + len, buf->data, buf->len);
    memcpy(buf->data, data, len);
    buf->len += len;
    return 0;
}

size_t evbuffer_get_length(const struct evbuffer *buf)
{
    return buf ? buf->len : 0;
}

int evbuffer_remove(struct evbuffer *buf, void *data, size_t len)
{
    if (!buf || !data)
        return -1;
    if (len > buf->len)
        len = buf->len;
    memcpy(data, buf->data, len);
    memmove(buf->data, buf->data + len, buf->len - len);
    buf->len -= len;
    return (int)len;
}

size_t evbuffer_remove_buffer(struct evbuffer *src, struct evbuffer *dst, size_t len)
{
    if (!src || !dst)
        return 0;
    if (len > src->len)
        len = src->len;
    if (len == 0)
        return 0;
    if (evbuffer_add(dst, src->data, len) < 0)
        return 0;
    memmove(src->data, src->data + len, src->len - len);
    src->len -= len;
    return len;
}

int evbuffer_drain(struct evbuffer *buf, size_t len)
{
    if (!buf)
        return -1;
    if (len > buf->len)
        len = buf->len;
    memmove(buf->data, buf->data + len, buf->len - len);
    buf->len -= len;
    return 0;
}

int evbuffer_peek(struct evbuffer *buf, ssize_t len,
                  const struct evbuffer_ptr *start_at,
                  struct evbuffer_iovec *vec, int n_vec)
{
    (void)start_at;
    if (!buf || !vec || n_vec < 1 || len < 0)
        return 0;
    if (len == 0 || buf->len == 0)
        return 0;
    vec[0].iov_base = buf->data;
    vec[0].iov_len = buf->len;
    return 1;
}

struct evbuffer_ptr evbuffer_search(struct evbuffer *buf, const char *what,
                                    size_t len, const struct evbuffer_ptr *start)
{
    struct evbuffer_ptr ptr = { .pos = -1 };
    if (!buf || !what || len == 0)
        return ptr;
    size_t off = start ? (size_t)start->pos : 0;
    if (buf->len >= len && off <= buf->len - len) {
        for (size_t i = off; i + len <= buf->len; i++) {
            if (memcmp(buf->data + i, what, len) == 0) {
                ptr.pos = (ssize_t)i;
                break;
            }
        }
    }
    return ptr;
}

int evbuffer_write(struct evbuffer *buf, int fd)
{
    if (!buf || buf->len == 0)
        return 0;
    int total = 0;
    while (buf->len > 0) {
        int w = send(fd, buf->data, buf->len, 0);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return total > 0 ? total : -1;
        }
        memmove(buf->data, buf->data + w, buf->len - w);
        buf->len -= w;
        total += w;
        if (w == 0)
            break;
    }
    return total;
}

unsigned char *evbuffer_pullup(struct evbuffer *buf, ssize_t size)
{
    if (!buf)
        return NULL;
    if (size < 0 || (size_t)size > buf->len)
        size = buf->len;
    return buf->data; /* contiguous by construction */
}

/* ------------------------------------------------------------------ */
/* event (timers)                                                      */
/* ------------------------------------------------------------------ */

struct event_base {
    struct event       *events;
    struct bufferevent *bevs;
    volatile int        break_flag;
    volatile int        stop_flag;
};

struct event {
    struct event_base  *base;
    event_callback_fn   cb;
    void               *arg;
    int64_t             period_us;   /* 0 = one-shot */
    int64_t             expire_us;   /* absolute, when pending */
    int                 pending;
    struct event       *next;
    struct event      **prev_next;
};

struct event_base *event_base_new(void)
{
    return calloc(1, sizeof(struct event_base));
}

void event_base_free(struct event_base *base)
{
    if (!base)
        return;
    /* xfrpc frees its events before the base; anything left is a leak in
     * upstream terms — free defensively. */
    while (base->events)
        event_free(base->events);
    free(base);
}

int event_base_loopbreak(struct event_base *base)
{
    if (!base)
        return -1;
    base->break_flag = 1;
    return 0;
}

/* Public: request a full stop of the loop (used by xfrpc_stop()). */
void mini_event_base_stop(struct event_base *base)
{
    if (base)
        base->stop_flag = 1;
}

const char *event_base_get_method(const struct event_base *base)
{
    (void)base;
    return "mini_event_select";
}

struct event *event_new(struct event_base *base, evutil_socket_t fd,
                        short what, event_callback_fn cb, void *arg)
{
    /* Only timer events are supported (fd events go through bufferevents).
     * what/EV_PERSIST ignored: re-arm behavior mirrors libevent where
     * one-shot timers must be re-added by the callback. */
    (void)fd;
    (void)what;
    struct event *ev = calloc(1, sizeof(struct event));
    if (!ev)
        return NULL;
    ev->base = base;
    ev->cb = cb;
    ev->arg = arg;
    return ev;
}

int event_add(struct event *ev, const struct timeval *tv)
{
    if (!ev || !ev->base || !tv)
        return -1;
    ev->period_us = 0; /* treated as one-shot; libevent one-shot timers also
                          require re-add, and xfrpc re-arms in callbacks */
    ev->expire_us = mini_now_us() + tv_to_us(tv);
    ev->pending = 1;
    /* link */
    if (!ev->prev_next) {
        ev->next = ev->base->events;
        if (ev->next)
            ev->next->prev_next = &ev->next;
        ev->base->events = ev;
        ev->prev_next = &ev->base->events;
    }
    return 0;
}

int event_del(struct event *ev)
{
    if (!ev)
        return -1;
    ev->pending = 0;
    if (ev->prev_next) {
        *ev->prev_next = ev->next;
        if (ev->next)
            ev->next->prev_next = ev->prev_next;
        ev->next = NULL;
        ev->prev_next = NULL;
    }
    return 0;
}

void event_free(struct event *ev)
{
    if (!ev)
        return;
    event_del(ev);
    free(ev);
}

int event_get_events(const struct event *ev)
{
    return ev ? ev->pending : 0;
}

/* ------------------------------------------------------------------ */
/* bufferevent                                                         */
/* ------------------------------------------------------------------ */

struct bufferevent {
    int                  fd;
    struct event_base   *base;
    struct evbuffer     *input;
    struct evbuffer     *output;
    bufferevent_data_cb  readcb;
    bufferevent_data_cb  writecb;
    bufferevent_event_cb eventcb;
    void                *cbarg;
    short                enabled;
    int                  options;

    int                  connecting;
    int64_t              connect_start_us;

    struct timeval       tv_read, tv_write;  /* all zero = disabled */
    int64_t              last_read_us;
    int64_t              last_write_us;
    int64_t              last_activity_us;

    size_t               wm_read_low;

    struct bufferevent      *next;
    struct bufferevent     **prev_next;
};

static struct bufferevent *bev_new(struct event_base *base, int fd, int options)
{
    struct bufferevent *bev = calloc(1, sizeof(struct bufferevent));
    if (!bev)
        return NULL;
    bev->fd = fd;
    bev->base = base;
    bev->options = options;
    bev->input = evbuffer_new();
    bev->output = evbuffer_new();
    if (!bev->input || !bev->output) {
        evbuffer_free(bev->input);
        evbuffer_free(bev->output);
        free(bev);
        return NULL;
    }
    int64_t now = mini_now_us();
    bev->last_read_us = now;
    bev->last_write_us = now;
    bev->last_activity_us = now;
    bev->prev_next = &base->bevs;
    bev->next = base->bevs;
    if (bev->next)
        bev->next->prev_next = &bev->next;
    base->bevs = bev;
    return bev;
}

struct bufferevent *bufferevent_socket_new(struct event_base *base,
                                           evutil_socket_t fd, int options)
{
    if (!base)
        return NULL;
    struct bufferevent *bev = bev_new(base, fd, options);
    if (bev && fd >= 0) {
        if (evutil_make_socket_nonblocking(fd) < 0) {
            bufferevent_free(bev);
            return NULL;
        }
    }
    return bev;
}

void bufferevent_free(struct bufferevent *bev)
{
    if (!bev)
        return;
    if (bev->options & BEV_OPT_CLOSE_ON_FREE) {
        if (bev->fd >= 0) {
            shutdown(bev->fd, SHUT_RDWR);
            close(bev->fd);
        }
    }
    bev->fd = -1;
    /* unlink so the loop won't touch it again */
    if (bev->prev_next) {
        *bev->prev_next = bev->next;
        if (bev->next)
            bev->next->prev_next = bev->prev_next;
        bev->next = NULL;
        bev->prev_next = NULL;
    }
    evbuffer_free(bev->input);
    evbuffer_free(bev->output);
    free(bev);
}

void bufferevent_setcb(struct bufferevent *bev,
                       bufferevent_data_cb readcb, bufferevent_data_cb writecb,
                       bufferevent_event_cb eventcb, void *cbarg)
{
    if (!bev)
        return;
    bev->readcb = readcb;
    bev->writecb = writecb;
    bev->eventcb = eventcb;
    bev->cbarg = cbarg;
}

void bufferevent_getcb(struct bufferevent *bev,
                       bufferevent_data_cb *readcb, bufferevent_data_cb *writecb,
                       bufferevent_event_cb *eventcb, void **cbarg)
{
    if (!bev)
        return;
    if (readcb) *readcb = bev->readcb;
    if (writecb) *writecb = bev->writecb;
    if (eventcb) *eventcb = bev->eventcb;
    if (cbarg) *cbarg = bev->cbarg;
}

int bufferevent_enable(struct bufferevent *bev, short events)
{
    if (!bev)
        return -1;
    bev->enabled |= events;
    int64_t now = mini_now_us();
    if (events & EV_READ)
        bev->last_read_us = now;
    if (events & EV_WRITE)
        bev->last_write_us = now;
    return 0;
}

int bufferevent_disable(struct bufferevent *bev, short events)
{
    if (!bev)
        return -1;
    bev->enabled &= ~events;
    return 0;
}

short bufferevent_get_enabled(struct bufferevent *bev)
{
    return bev ? bev->enabled : 0;
}

int bufferevent_write(struct bufferevent *bev, const void *data, size_t size)
{
    if (!bev || !data)
        return -1;
    return evbuffer_add(bev->output, data, size);
}

int bufferevent_write_buffer(struct bufferevent *bev, struct evbuffer *buf)
{
    if (!bev || !buf)
        return -1;
    return evbuffer_add_buffer(bev->output, buf);
}

size_t bufferevent_read(struct bufferevent *bev, void *data, size_t size)
{
    if (!bev || !data)
        return 0;
    return evbuffer_remove(bev->input, data, size);
}

int bufferevent_read_buffer(struct bufferevent *bev, struct evbuffer *buf)
{
    if (!bev || !buf)
        return -1;
    return evbuffer_add_buffer(buf, bev->input) == 0 ? 0 : -1;
}

struct evbuffer *bufferevent_get_input(struct bufferevent *bev)
{
    return bev ? bev->input : NULL;
}

struct evbuffer *bufferevent_get_output(struct bufferevent *bev)
{
    return bev ? bev->output : NULL;
}

evutil_socket_t bufferevent_getfd(struct bufferevent *bev)
{
    return bev ? bev->fd : -1;
}

void bufferevent_set_timeouts(struct bufferevent *bev,
                              const struct timeval *tv_read,
                              const struct timeval *tv_write)
{
    if (!bev)
        return;
    if (tv_read)
        bev->tv_read = *tv_read;
    else
        memset(&bev->tv_read, 0, sizeof(bev->tv_read));
    if (tv_write)
        bev->tv_write = *tv_write;
    else
        memset(&bev->tv_write, 0, sizeof(bev->tv_write));
    int64_t now = mini_now_us();
    bev->last_read_us = now;
    bev->last_write_us = now;
}

void bufferevent_setwatermark(struct bufferevent *bev, short events,
                              size_t lowmark, size_t highmark)
{
    (void)highmark;
    if (!bev)
        return;
    if (events & EV_READ)
        bev->wm_read_low = lowmark;
}

int bufferevent_flush(struct bufferevent *bev, short iotype, short mode)
{
    /* Writes are already flushed eagerly by the loop; no-op. */
    (void)bev; (void)iotype; (void)mode;
    return 0;
}

int bufferevent_socket_connect(struct bufferevent *bev,
                               const struct sockaddr *sa, int socklen)
{
    if (!bev || !sa)
        return -1;
    if (bev->fd < 0) {
        int fd = socket(sa->sa_family, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        bev->fd = fd;
    }
    if (evutil_make_socket_nonblocking(bev->fd) < 0)
        return -1;
    if (connect(bev->fd, sa, socklen) < 0 && errno != EINPROGRESS) {
        return -1;
    }
    bev->connecting = 1;
    bev->connect_start_us = mini_now_us();
    bev->enabled = EV_READ | EV_WRITE; /* enabled once connected */
    return 0;
}

int bufferevent_socket_connect_hostname(struct bufferevent *bev,
                                        struct evdns_base *dns_base,
                                        int family, const char *hostname, int port)
{
    (void)dns_base; /* lwip resolver is used directly */
    if (!bev || !hostname || family != AF_INET)
        return -1;

    /* Blocking DNS (MVP tradeoff; see plan risk notes) */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    if (getaddrinfo(hostname, port_str, &hints, &res) != 0 || !res) {
        if (res)
            freeaddrinfo(res);
        return -1;
    }
    int rc = bufferevent_socket_connect(bev, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return rc;
}

/* ------------------------------------------------------------------ */
/* evutil                                                              */
/* ------------------------------------------------------------------ */

int evutil_make_socket_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int evutil_closesocket(int fd)
{
    return close(fd);
}

int evutil_socketpair(int d, int type, int protocol, int sv[2])
{
    (void)d; (void)type; (void)protocol; (void)sv;
    return -1; /* not supported on lwip */
}

/* ------------------------------------------------------------------ */
/* evdns stubs                                                         */
/* ------------------------------------------------------------------ */

static int s_dns_dummy;

struct evdns_base *evdns_base_new(struct event_base *base, int initialize)
{
    (void)base; (void)initialize;
    return (struct evdns_base *)&s_dns_dummy;
}

void evdns_base_free(struct evdns_base *base, int fail_requests)
{
    (void)base; (void)fail_requests;
}

int evdns_base_count_nameservers(struct evdns_base *base)
{
    (void)base;
    return 1; /* lwip resolver */
}

int evdns_base_nameserver_ip_add(struct evdns_base *base, const char *ip)
{
    (void)base; (void)ip;
    return 0;
}

int evdns_base_set_option(struct evdns_base *base, const char *option, const char *val)
{
    (void)base; (void)option; (void)val;
    return 0;
}

int evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *filename)
{
    (void)base; (void)flags; (void)filename;
    return 0;
}

/* ------------------------------------------------------------------ */
/* event loop                                                          */
/* ------------------------------------------------------------------ */

/* Is bev still linked into base? (guards against free inside callbacks) */
static int bev_linked(const struct event_base *base, const struct bufferevent *bev)
{
    for (struct bufferevent *b = base->bevs; b; b = b->next)
        if (b == bev)
            return 1;
    return 0;
}

static int event_linked(const struct event_base *base, const struct event *ev)
{
    for (struct event *e = base->events; e; e = e->next)
        if (e == ev)
            return 1;
    return 0;
}

static void bev_call_eventcb(struct bufferevent *bev, short what)
{
    if (bev->eventcb)
        bev->eventcb(bev, what, bev->cbarg);
}

static void bev_check_connect(struct bufferevent *bev)
{
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt(bev->fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0)
        err = errno;
    bev->connecting = 0;
    int64_t now = mini_now_us();
    bev->last_read_us = now;
    bev->last_write_us = now;
    if (err == 0) {
        bev->enabled = EV_READ | EV_WRITE;
        bev_call_eventcb(bev, BEV_EVENT_CONNECTED);
    } else {
        errno = err;
        bev_call_eventcb(bev, BEV_EVENT_ERROR);
    }
}

static void bev_read_input(struct bufferevent *bev)
{
    uint8_t chunk[MINI_RECV_CHUNK];
    size_t got = 0;
    for (;;) {
        int n = recv(bev->fd, chunk, sizeof(chunk), 0);
        if (n > 0) {
            evbuffer_add(bev->input, chunk, (size_t)n);
            got += (size_t)n;
            bev->last_read_us = mini_now_us();
            if (got >= MINI_RECV_MAX_PER_LOOP)
                break;
            continue;
        }
        if (n == 0) {
            bev_call_eventcb(bev, BEV_EVENT_EOF);
            return;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        bev_call_eventcb(bev, BEV_EVENT_ERROR);
        return;
    }
    if (got > 0 && bev->readcb && (bev->enabled & EV_READ) &&
        evbuffer_get_length(bev->input) >= bev->wm_read_low) {
        bev->readcb(bev, bev->cbarg);
    }
}

static void bev_flush_output(struct bufferevent *bev)
{
    struct evbuffer *out = bev->output;
    while (evbuffer_get_length(out) > 0) {
        int w = send(bev->fd, out->data, evbuffer_get_length(out), 0);
        if (w > 0) {
            evbuffer_drain(out, (size_t)w);
            bev->last_write_us = mini_now_us();
            continue;
        }
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (w < 0) {
            bev_call_eventcb(bev, BEV_EVENT_ERROR);
            return;
        }
        break; /* w == 0 */
    }
    if (evbuffer_get_length(out) == 0 && bev->writecb &&
        (bev->enabled & EV_WRITE)) {
        bev->writecb(bev, bev->cbarg);
    }
}

static void bev_check_timeouts(struct bufferevent *bev)
{
    int64_t now = mini_now_us();
    int64_t rd = tv_to_us(&bev->tv_read);
    int64_t wr = tv_to_us(&bev->tv_write);

    if (bev->connecting) {
        if (wr > 0 && now - bev->connect_start_us >= wr) {
            bev->connecting = 0;
            bev_call_eventcb(bev, BEV_EVENT_TIMEOUT | BEV_EVENT_WRITING);
        }
        return;
    }
    if ((bev->enabled & EV_READ) && rd > 0 && now - bev->last_read_us >= rd) {
        bev->last_read_us = now; /* avoid refire every loop tick */
        bev_call_eventcb(bev, BEV_EVENT_TIMEOUT | BEV_EVENT_READING);
        if (!bev_linked(bev->base, bev))
            return;
    }
    if ((bev->enabled & EV_WRITE) && wr > 0 &&
        evbuffer_get_length(bev->output) > 0 && now - bev->last_write_us >= wr) {
        bev->last_write_us = now;
        bev_call_eventcb(bev, BEV_EVENT_TIMEOUT | BEV_EVENT_WRITING);
    }
}

int event_base_dispatch(struct event_base *base)
{
    if (!base)
        return -1;
    base->break_flag = 0;

    for (;;) {
        if (base->break_flag || base->stop_flag)
            return 0;
        int64_t now = mini_now_us();

        /* ---- compute select sets + timeout ---- */
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = -1;
        int have_fds = 0;

        for (struct bufferevent *bev = base->bevs; bev; bev = bev->next) {
            if (bev->fd < 0)
                continue;
            if (bev->connecting) {
                FD_SET(bev->fd, &wfds);
            } else {
                if (bev->enabled & EV_READ)
                    FD_SET(bev->fd, &rfds);
                if ((bev->enabled & EV_WRITE) &&
                    evbuffer_get_length(bev->output) > 0)
                    FD_SET(bev->fd, &wfds);
            }
            if (FD_ISSET(bev->fd, &rfds) || FD_ISSET(bev->fd, &wfds)) {
                have_fds = 1;
                if (bev->fd > maxfd)
                    maxfd = bev->fd;
            }
        }

        int64_t next_expire = -1;
        for (struct event *ev = base->events; ev; ev = ev->next) {
            if (!ev->pending)
                continue;
            if (next_expire < 0 || ev->expire_us < next_expire)
                next_expire = ev->expire_us;
        }

        if (!have_fds && next_expire < 0)
            return 1; /* no events registered — like libevent */

        int64_t wait_us = MINI_SELECT_MAX_WAIT_MS * 1000LL;
        if (next_expire >= 0) {
            int64_t until = next_expire - now;
            if (until < 0)
                until = 0;
            if (until < wait_us)
                wait_us = until;
        }

        struct timeval tv = {
            .tv_sec = wait_us / 1000000,
            .tv_usec = wait_us % 1000000,
        };
        if (have_fds) {
            int rc = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
            (void)rc;
        } else {
            /* only timers pending — lwip select() with nfds==0 is unreliable */
            vTaskDelay(pdMS_TO_TICKS((wait_us + 999) / 1000));
        }
        now = mini_now_us();

        if (base->break_flag || base->stop_flag)
            return 0;

        /* ---- timers first ----
         * Restart the scan after every callback: the callback may free or
         * add events anywhere in the list. */
        int fired;
        do {
            fired = 0;
            for (struct event *ev = base->events; ev; ev = ev->next) {
                if (!ev->pending || now < ev->expire_us)
                    continue;
                ev->pending = 0; /* one-shot; callback re-arms via event_add */
                if (ev->cb)
                    ev->cb(-1, EV_TIMEOUT, ev->arg);
                fired = 1;
                break;
            }
        } while (fired);

        if (base->break_flag || base->stop_flag)
            return 0;

        /* ---- bufferevents ---- */
        for (struct bufferevent *bev = base->bevs; bev; ) {
            struct bufferevent *next = bev->next;
            if (bev->fd < 0 || !bev_linked(base, bev)) {
                bev = next;
                continue;
            }
            if (bev->connecting && FD_ISSET(bev->fd, &wfds)) {
                bev_check_connect(bev);
                bev = next;
                continue;
            }
            if (!bev->connecting && FD_ISSET(bev->fd, &rfds)) {
                bev_read_input(bev);
                if (!bev_linked(base, bev)) {
                    bev = next;
                    continue;
                }
            }
            if (!bev->connecting && FD_ISSET(bev->fd, &wfds)) {
                bev_flush_output(bev);
                if (!bev_linked(base, bev)) {
                    bev = next;
                    continue;
                }
            }
            bev_check_timeouts(bev);
            bev = next;
        }
    }
}
