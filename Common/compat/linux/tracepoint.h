#ifndef B43COMPAT_STUB_LINUX_TRACEPOINT_H
#define B43COMPAT_STUB_LINUX_TRACEPOINT_H
/*
 * Mac OS 9 compatibility: tracepoints disabled, as in Linux without
 * CONFIG_TRACEPOINTS: every trace_<name>() is an empty inline function.
 */
#include <b43compat.h>

#define TP_PROTO(...)           __VA_ARGS__
#define TP_ARGS(...)            __VA_ARGS__
#define TP_CONDITION(c)         c

#define TRACE_EVENT(name, proto, args, tstruct, assign, print) \
    static inline void trace_##name(proto) { }
#define TRACE_EVENT_CONDITION(name, proto, args, cond, tstruct, assign, print) \
    static inline void trace_##name(proto) { }
#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT(template, name, proto, args) \
    static inline void trace_##name(proto) { }
#define DEFINE_EVENT_PRINT(template, name, proto, args, print) \
    static inline void trace_##name(proto) { }
#define DEFINE_EVENT_CONDITION(template, name, proto, args, cond) \
    static inline void trace_##name(proto) { }
#define DECLARE_TRACE(name, proto, args) \
    static inline void trace_##name(proto) { }
#endif
