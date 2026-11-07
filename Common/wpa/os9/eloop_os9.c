/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * wpa_supplicant - Event loop for Mac OS 9 (src/utils/eloop.h)
 *
 * Porting file like eloop.c / eloop_win.c. The RSN supplicant only uses
 * timeouts (retransmissions, handshake deadlines): eloop_os9_poll() runs
 * them, and is called by b43os9_poll(). Sockets and signals do not exist.
 */
#include "includes.h"
#include "common.h"
#include "list.h"
#include "eloop.h"

struct eloop_timeout {
    struct dl_list list;
    struct os_reltime time;
    void *eloop_data;
    void *user_data;
    eloop_timeout_handler handler;
};

static struct dl_list g_timeouts;
static int g_init;
static int g_terminate;

int eloop_init(void)
{
    if (!g_init) {
        dl_list_init(&g_timeouts);
        g_init = 1;
    }
    g_terminate = 0;
    return 0;
}

int eloop_register_timeout(unsigned int secs, unsigned int usecs,
                           eloop_timeout_handler handler,
                           void *eloop_data, void *user_data)
{
    struct eloop_timeout *timeout, *tmp;

    eloop_init();
    timeout = os_zalloc(sizeof(*timeout));
    if (timeout == NULL)
        return -1;
    os_get_reltime(&timeout->time);
    timeout->time.sec += secs;
    timeout->time.usec += usecs;
    while (timeout->time.usec >= 1000000) {
        timeout->time.sec++;
        timeout->time.usec -= 1000000;
    }
    timeout->eloop_data = eloop_data;
    timeout->user_data = user_data;
    timeout->handler = handler;

    /* sorted by expiry, like eloop.c */
    dl_list_for_each(tmp, &g_timeouts, struct eloop_timeout, list) {
        if (os_reltime_before(&timeout->time, &tmp->time)) {
            dl_list_add(tmp->list.prev, &timeout->list);
            return 0;
        }
    }
    dl_list_add_tail(&g_timeouts, &timeout->list);
    return 0;
}

static void eloop_remove_timeout(struct eloop_timeout *timeout)
{
    dl_list_del(&timeout->list);
    os_free(timeout);
}

int eloop_cancel_timeout(eloop_timeout_handler handler,
                         void *eloop_data, void *user_data)
{
    struct eloop_timeout *timeout, *prev;
    int removed = 0;

    eloop_init();
    dl_list_for_each_safe(timeout, prev, &g_timeouts, struct eloop_timeout, list) {
        if (timeout->handler == handler &&
            (timeout->eloop_data == eloop_data || eloop_data == ELOOP_ALL_CTX) &&
            (timeout->user_data == user_data || user_data == ELOOP_ALL_CTX)) {
            eloop_remove_timeout(timeout);
            removed++;
        }
    }
    return removed;
}

int eloop_cancel_timeout_one(eloop_timeout_handler handler,
                             void *eloop_data, void *user_data,
                             struct os_reltime *remaining)
{
    struct eloop_timeout *timeout, *prev;
    struct os_reltime now;

    eloop_init();
    os_get_reltime(&now);
    remaining->sec = remaining->usec = 0;
    dl_list_for_each_safe(timeout, prev, &g_timeouts, struct eloop_timeout, list) {
        if (timeout->handler == handler && timeout->eloop_data == eloop_data &&
            timeout->user_data == user_data) {
            if (os_reltime_before(&now, &timeout->time))
                os_reltime_sub(&timeout->time, &now, remaining);
            eloop_remove_timeout(timeout);
            return 1;
        }
    }
    return 0;
}

int eloop_is_timeout_registered(eloop_timeout_handler handler,
                                void *eloop_data, void *user_data)
{
    struct eloop_timeout *tmp;

    eloop_init();
    dl_list_for_each(tmp, &g_timeouts, struct eloop_timeout, list) {
        if (tmp->handler == handler && tmp->eloop_data == eloop_data &&
            tmp->user_data == user_data)
            return 1;
    }
    return 0;
}

int eloop_deplete_timeout(unsigned int req_secs, unsigned int req_usecs,
                          eloop_timeout_handler handler, void *eloop_data,
                          void *user_data)
{
    struct os_reltime remaining;

    if (!eloop_is_timeout_registered(handler, eloop_data, user_data))
        return -1;
    eloop_cancel_timeout_one(handler, eloop_data, user_data, &remaining);
    if (remaining.sec > (os_time_t)req_secs ||
        (remaining.sec == (os_time_t)req_secs && remaining.usec > (os_time_t)req_usecs)) {
        eloop_register_timeout(req_secs, req_usecs, handler, eloop_data, user_data);
        return 1;
    }
    eloop_register_timeout((unsigned int)remaining.sec, (unsigned int)remaining.usec,
                           handler, eloop_data, user_data);
    return 0;
}

int eloop_replenish_timeout(unsigned int req_secs, unsigned int req_usecs,
                            eloop_timeout_handler handler, void *eloop_data,
                            void *user_data)
{
    struct os_reltime remaining;

    if (!eloop_is_timeout_registered(handler, eloop_data, user_data))
        return -1;
    eloop_cancel_timeout_one(handler, eloop_data, user_data, &remaining);
    if (remaining.sec < (os_time_t)req_secs ||
        (remaining.sec == (os_time_t)req_secs && remaining.usec < (os_time_t)req_usecs)) {
        eloop_register_timeout(req_secs, req_usecs, handler, eloop_data, user_data);
        return 1;
    }
    eloop_register_timeout((unsigned int)remaining.sec, (unsigned int)remaining.usec,
                           handler, eloop_data, user_data);
    return 0;
}

/* Runs expired timeouts (one at a time: a handler may change the list) */
void eloop_os9_poll(void)
{
    struct eloop_timeout *timeout;
    struct os_reltime now;

    if (!g_init)
        return;
    for (;;) {
        if (dl_list_empty(&g_timeouts))
            return;
        timeout = dl_list_first(&g_timeouts, struct eloop_timeout, list);
        os_get_reltime(&now);
        if (os_reltime_before(&now, &timeout->time))
            return;
        {
            void *eloop_data = timeout->eloop_data;
            void *user_data = timeout->user_data;
            eloop_timeout_handler handler = timeout->handler;
            eloop_remove_timeout(timeout);
            handler(eloop_data, user_data);
        }
    }
}

/* Sockets, events, signals: they do not exist on Mac OS 9 */
int eloop_register_read_sock(int sock, eloop_sock_handler handler,
                             void *eloop_data, void *user_data)
{ (void)sock; (void)handler; (void)eloop_data; (void)user_data; return -1; }
void eloop_unregister_read_sock(int sock) { (void)sock; }
int eloop_register_sock(int sock, eloop_event_type type, eloop_sock_handler handler,
                        void *eloop_data, void *user_data)
{ (void)sock; (void)type; (void)handler; (void)eloop_data; (void)user_data; return -1; }
void eloop_unregister_sock(int sock, eloop_event_type type) { (void)sock; (void)type; }
int eloop_register_event(void *event, size_t event_size, eloop_event_handler handler,
                         void *eloop_data, void *user_data)
{ (void)event; (void)event_size; (void)handler; (void)eloop_data; (void)user_data; return -1; }
void eloop_unregister_event(void *event, size_t event_size) { (void)event; (void)event_size; }
int eloop_register_signal(int sig, eloop_signal_handler handler, void *user_data)
{ (void)sig; (void)handler; (void)user_data; return 0; }
int eloop_register_signal_terminate(eloop_signal_handler handler, void *user_data)
{ (void)handler; (void)user_data; return 0; }
int eloop_register_signal_reconfig(eloop_signal_handler handler, void *user_data)
{ (void)handler; (void)user_data; return 0; }
int eloop_sock_requeue(void) { return 0; }
void eloop_run(void) { }
void eloop_terminate(void) { g_terminate = 1; }
int eloop_terminated(void) { return g_terminate; }
void eloop_wait_for_read_sock(int sock) { (void)sock; }

void eloop_destroy(void)
{
    struct eloop_timeout *timeout, *prev;

    if (!g_init)
        return;
    dl_list_for_each_safe(timeout, prev, &g_timeouts, struct eloop_timeout, list)
        eloop_remove_timeout(timeout);
}
