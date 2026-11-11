/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_DLE_H_
#define APX_DLE_H_

/*
 * dle.h - Interface of DLPISupportLib, the shared library inside the
 * "Open Transport" extension that implements the DLPI side of an Ethernet
 * driver (Mentat's "DLPI template", dlpiether.c).
 *
 * The library does the whole DLPI protocol (DL_INFO, DL_BIND, DL_UNITDATA,
 * multicast lists, statistics, SAP demultiplexing of inbound frames); the
 * board driver supplies start/stop/filter callbacks and moves frames.
 *
 * These are interoperability declarations of the library's binary
 * interface, written for this project: the structure layouts must match
 * what the library expects (the library initializes and uses every field of
 * dle_t). The interface is documented in Apple's PCCardNetworkSample
 * (dlpiether.h, dlpiuser.h) and in the "DLPI Template" document of the
 * Open Transport SDK.
 */
#include <OpenTransportKernel.h>
#include <dlpi.h>

/* Interface MIB statistics (RFC 1573). The transmit counters are kept by
 * the board driver: fast path frames never pass through the library. */
typedef struct dle_interface_status_t {
    UInt32 bytes_received;
    UInt32 unicast_frames_received;
    UInt32 bytes_sent;
    UInt32 unicast_frames_sent;
    UInt32 multicast_frames_received;
    UInt32 broadcast_frames_received;
    UInt32 receive_discards;
    UInt32 receive_errors;
    UInt32 receive_unknown_protos;
    UInt32 multicast_frames_sent;
    UInt32 broadcast_frames_sent;
    UInt32 transmit_discards;
    UInt32 transmit_errors;
    UInt32 mtu;                         /* 1514 by default */
    UInt32 speed;                       /* bits/s, 10 Mb/s by default */
} dle_interface_status_t;

/* Ethernet MIB statistics (RFC 1643), kept by the board driver */
typedef struct dle_ethernet_status_t {
    UInt32 ether_chip_set;
    UInt32 receive_alignment_errors;
    UInt32 receive_fcs_errors;
    UInt32 receive_frames_too_long;
    UInt32 receive_internal_mac_errors;
    UInt32 transmit_single_collision_frames;
    UInt32 transmit_multiple_collision_frames;
    UInt32 transmit_sqe_test_errors;
    UInt32 transmit_deferred_transmissions;
    UInt32 transmit_late_collisions;
    UInt32 transmit_excessive_collisions;
    UInt32 transmit_internal_mac_errors;
    UInt32 transmit_carrier_sense_errors;
} dle_ethernet_status_t;

/* Hardware address list passed to the address filter callback */
typedef struct dle_addr_t {
    struct dle_addr_t *dlea_next;
    UInt8              dlea_addr[6];
} dle_addr_t;

/* Board callbacks: the first argument is the dle_t (see dle_to_hw) */
typedef void (*dle_hw_start_t)(void *hw);
typedef void (*dle_hw_stop_t)(void *hw);
typedef void (*dle_hw_filter_t)(void *hw, dle_addr_t *addr_list, UInt32 addr_count,
                                UInt32 promisc_count, UInt32 multi_promisc_count,
                                UInt32 accept_broadcast, UInt32 accept_errors);
typedef int  (*dle_hw_send_error_t)(void *hw, mblk_t *mp, UInt32 flags);

typedef struct dlehw_t {
    dle_hw_start_t      dlehw_start;            /* first bind: start the hardware */
    dle_hw_stop_t       dlehw_stop;             /* last unbind: stop it */
    dle_hw_filter_t     dlehw_address_filter_reset;
    dle_hw_send_error_t dlehw_send_error;       /* NULL: not supported */
    UInt32              dlehw_recv_error_flags; /* errors the hardware can report */
} dlehw_t;

/* One per device; the board structure starts with it */
typedef struct dle_t {
    dlehw_t                 dle_hw;
    UInt8                   dle_factory_addr[6];
    UInt8                   dle_current_addr[6];
    volatile UInt32         dle_intr_active;
    dle_interface_status_t  dle_istatus;
    dle_ethernet_status_t   dle_estatus;
    void                   *dle_sap_hash_tbl[64];
    void                   *dle_match_any;
    void                   *dle_match_any_multicast;
    void                   *dle_match_matched;
    void                   *dle_match_any_802;
    void                   *dle_bind_list;
    UInt32                  dle_bound_count;
    UInt32                  dle_match_any_count;
    UInt32                  dle_match_any_multicast_count;
    void                   *dle_hw_addr_list;
    UInt32                  dle_refcnt;
    UInt32                  dle_xtra_hdr_len;
    UInt32                  dle_min_sdu;
    char                   *dle_instance_head;
    UInt32                  dle_reserved[6];
} dle_t;

/* One per open stream (q->q_ptr) */
typedef struct dcl_t {
    UInt32   dcl_state;
    void    *dcl_hw;                    /* the dle_t */
    queue_t *dcl_rq;
    UInt32   dcl_flags;
    UInt32   dcl_sap;
    UInt8    dcl_snap[5];
    char     dcl_pad[3];
    UInt32   dcl_mac_type;
    UInt32   dcl_framing_type;
    void    *dcl_addr_list;
    UInt32   dcl_truncation_length;
    UInt32   dcl_reserved[1];
} dcl_t;

#ifdef __cplusplus
extern "C" {
#endif

int     dle_close(queue_t *q);
void    dle_inbound(dle_t *dle, mblk_t *mp);
void    dle_inbound_error(dle_t *dle, mblk_t *mp, UInt32 flags);
void    dle_init(dle_t *dle, size_t xtra_hdr_len);
int     dle_open(dle_t *dle, queue_t *q, dev_t *devp, int flag, int sflag,
                 cred_t *credp, size_t dcl_len);
void    dle_rsrv_ctl(queue_t *q, mblk_t *mp);
void    dle_terminate(dle_t *dle);
mblk_t *dle_wput(queue_t *q, mblk_t *mp);
mblk_t *dle_wput_ud_error(mblk_t *mp, int dlpi_err, int unix_err);

#ifdef __cplusplus
}
#endif

#endif /* APX_DLE_H_ */
