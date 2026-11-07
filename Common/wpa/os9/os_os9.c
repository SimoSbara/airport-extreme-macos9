/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * wpa_supplicant - Operating system functions for Mac OS 9 (src/utils/os.h)
 *
 * This is the porting file foreseen by wpa_supplicant (like os_unix.c,
 * os_win32.c, os_none.c). With OS_NO_C_LIB_DEFINES the C library functions
 * also go through here: the driver has no libc. Memory and time come
 * from the b43_env passed to b43os9_attach().
 */
#include "includes.h"
#include "os.h"
#include "common.h"
#include "b43os9_wpa.h"

/* Implemented in the compatibility layer (Common/compat) */
unsigned long long b43compat_time_us64(void);
int b43c_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
void get_random_bytes(void *buf, int nbytes);

/* =========================================================================
 * Time
 * ========================================================================= */
void os_sleep(os_time_t sec, os_time_t usec)
{
    const b43_env *env = b43os9_supp_env();
    unsigned long long us = (unsigned long long)sec * 1000000ULL + (unsigned long long)usec;

    while (us > 1000000ULL) {
        env->delay_us(env->ctx, 1000000UL);
        us -= 1000000ULL;
    }
    env->delay_us(env->ctx, (unsigned long)us);
}

int os_get_time(struct os_time *t)
{
    unsigned long long us = b43compat_time_us64();
    t->sec = (os_time_t)(us / 1000000ULL);
    t->usec = (os_time_t)(us % 1000000ULL);
    return 0;
}

int os_get_reltime(struct os_reltime *t)
{
    unsigned long long us = b43compat_time_us64();
    t->sec = (os_time_t)(us / 1000000ULL);
    t->usec = (os_time_t)(us % 1000000ULL);
    return 0;
}

int os_mktime(int year, int month, int day, int hour, int min, int sec, os_time_t *t)
{
    (void)year; (void)month; (void)day; (void)hour; (void)min; (void)sec;
    *t = 0;
    return -1;
}

int os_gmtime(os_time_t t, struct os_tm *tm)
{
    (void)t;
    os_memset(tm, 0, sizeof(*tm));
    return -1;
}

/* =========================================================================
 * Random numbers: the handshake SNonce comes from here.
 * get_random_bytes() from the compatibility layer.
 * ========================================================================= */
int os_get_random(unsigned char *buf, size_t len)
{
    get_random_bytes(buf, (int)len);
    return 0;
}

unsigned long os_random(void)
{
    unsigned long v;
    get_random_bytes(&v, sizeof(v));
    return v;
}

/* =========================================================================
 * Program, files, environment: not used by the RSN supplicant
 * ========================================================================= */
int os_daemonize(const char *pid_file) { (void)pid_file; return -1; }
void os_daemonize_terminate(const char *pid_file) { (void)pid_file; }
char * os_rel2abs_path(const char *rel_path) { return os_strdup(rel_path); }
int os_program_init(void) { return 0; }
void os_program_deinit(void) { }
int os_setenv(const char *name, const char *value, int overwrite)
{ (void)name; (void)value; (void)overwrite; return -1; }
int os_unsetenv(const char *name) { (void)name; return -1; }
char * os_readfile(const char *name, size_t *len) { (void)name; (void)len; return NULL; }
int os_file_exists(const char *fname) { (void)fname; return 0; }
int os_fdatasync(FILE *stream) { (void)stream; return 0; }
int os_exec(const char *program, const char *arg, int wait_completion)
{ (void)program; (void)arg; (void)wait_completion; return -1; }

/* =========================================================================
 * Memory (with the size stored in front, for os_realloc)
 * ========================================================================= */
void * os_malloc(size_t size)
{
    const b43_env *env = b43os9_supp_env();
    size_t *p = (size_t *)env->alloc(env->ctx, (unsigned long)(size + sizeof(size_t) * 2));
    if (!p)
        return NULL;
    p[0] = size;
    return p + 2;               /* 8-byte aligned */
}

void os_free(void *ptr)
{
    const b43_env *env = b43os9_supp_env();
    if (ptr)
        env->free(env->ctx, (size_t *)ptr - 2);
}

void * os_realloc(void *ptr, size_t size)
{
    void *n;
    size_t old;

    if (!ptr)
        return os_malloc(size);
    old = ((size_t *)ptr)[-2];
    n = os_malloc(size);
    if (!n)
        return NULL;
    os_memcpy(n, ptr, old < size ? old : size);
    os_free(ptr);
    return n;
}

void * os_zalloc(size_t size)
{
    void *p = os_malloc(size);
    if (p)
        os_memset(p, 0, size);
    return p;
}

void * os_memdup(const void *src, size_t len)
{
    void *r = os_malloc(len);
    if (r && src)
        os_memcpy(r, src, len);
    return r;
}

/* =========================================================================
 * C library
 * ========================================================================= */
void * os_memcpy(void *dest, const void *src, size_t n)
{
    return os_memmove(dest, src, n);
}

void * os_memmove(void *dest, const void *src, size_t n)
{
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;

    if (d < s || d >= s + n) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

void * os_memset(void *s, int c, size_t n)
{
    u8 *d = (u8 *)s;
    while (n--)
        *d++ = (u8)c;
    return s;
}

int os_memcmp(const void *s1, const void *s2, size_t n)
{
    const u8 *a = (const u8 *)s1, *b = (const u8 *)s2;
    for (; n; n--, a++, b++)
        if (*a != *b)
            return *a - *b;
    return 0;
}

/* Constant-time comparison (handshake MIC) */
int os_memcmp_const(const void *a, const void *b, size_t len)
{
    const u8 *aa = (const u8 *)a, *bb = (const u8 *)b;
    u8 res = 0;
    size_t i;
    for (i = 0; i < len; i++)
        res |= aa[i] ^ bb[i];
    return res;
}

size_t os_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

char * os_strdup(const char *s)
{
    return (char *)os_memdup(s, os_strlen(s) + 1);
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

int os_strcasecmp(const char *s1, const char *s2)
{
    return os_strncasecmp(s1, s2, (size_t)-1);
}

int os_strncasecmp(const char *s1, const char *s2, size_t n)
{
    for (; n; n--, s1++, s2++) {
        int a = lower((unsigned char)*s1), b = lower((unsigned char)*s2);
        if (a != b)
            return a - b;
        if (!a)
            break;
    }
    return 0;
}

char * os_strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? NULL : (char *)s;
}

char * os_strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (; *s; s++)
        if (*s == (char)c)
            r = s;
    return c ? (char *)r : (char *)s;
}

int os_strcmp(const char *s1, const char *s2)
{
    return os_strncmp(s1, s2, (size_t)-1);
}

int os_strncmp(const char *s1, const char *s2, size_t n)
{
    for (; n; n--, s1++, s2++) {
        if (*s1 != *s2)
            return (unsigned char)*s1 - (unsigned char)*s2;
        if (!*s1)
            break;
    }
    return 0;
}

char * os_strstr(const char *haystack, const char *needle)
{
    size_t len = os_strlen(needle);
    for (; *haystack; haystack++)
        if (!os_strncmp(haystack, needle, len))
            return (char *)haystack;
    return len ? NULL : (char *)haystack;
}

size_t os_strlcpy(char *dest, const char *src, size_t siz)
{
    size_t len = os_strlen(src);
    if (siz) {
        size_t n = len >= siz ? siz - 1 : len;
        os_memcpy(dest, src, n);
        dest[n] = '\0';
    }
    return len;
}

int os_snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    int n;
    va_start(ap, format);
    n = b43c_vsnprintf(str, size, format, ap);
    va_end(ap);
    return n;
}
