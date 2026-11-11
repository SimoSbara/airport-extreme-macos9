/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * OTCheck.c - Open Transport diagnostics for the AirPort Extreme driver.
 *
 *   1. lists every Open Transport port with its record (type, flags,
 *      module name), to compare the built-in Ethernet with the AirPort port;
 *   2. opens a TCP endpoint, which loads TCP/IP with the current
 *      configuration, and prints the error Open Transport returns;
 *   3. lists the TCP/IP interfaces (address, mask, hardware address).
 *
 * Everything is also written to "OTCheck.txt" next to the application.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <Files.h>              /* FSSpec, used by OpenTransportProviders.h */
#include <OpenTransport.h>
#include <OpenTptInternet.h>

static FILE *g_out;

static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_out) {
        va_start(ap, fmt);
        vfprintf(g_out, fmt, ap);
        va_end(ap);
        fflush(g_out);
    }
}

static void print_ip(const char *label, InetHost h)
{
    say("%s%lu.%lu.%lu.%lu\n", label, (h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF,
        h & 0xFF);
}

static void list_ports(void)
{
    OTPortRecord rec;
    OTItemCount i;

    say("\n[1] Open Transport ports\n");
    for (i = 0; OTGetIndexedPort(&rec, i); i++) {
        UInt32 ref = (UInt32)rec.fRef;
        say(" %2lu \"%s\" module \"%s\" slot \"%s\" resource \"%s\"\n", (unsigned long)i,
            rec.fPortName, rec.fModuleName, rec.fSlotID, rec.fResourceInfo);
        say("    ref 0x%08lX (bus %lu, device type %lu, slot %lu), port flags 0x%08lX,"
            " info flags 0x%08lX, capabilities 0x%08lX, children %lu\n",
            ref, (ref >> 24) & 0xFF, (ref >> 16) & 0xFF, ref & 0xFFFF,
            (unsigned long)rec.fPortFlags, (unsigned long)rec.fInfoFlags,
            (unsigned long)rec.fCapabilities, (unsigned long)rec.fNumChildPorts);
    }
    say(" %lu ports\n", (unsigned long)i);
}

static void open_tcp(void)
{
    OSStatus err = kOTNoError;
    TEndpointInfo info;
    EndpointRef ep;

    say("\n[2] Loading TCP/IP (OTOpenEndpoint \"tcp\")\n");
    ep = OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, &info, &err);
    say(" result %ld%s\n", (long)err, err == kOTNoError ? " (TCP/IP loaded)" : "");
    if (ep != kOTInvalidEndpointRef && err == kOTNoError) {
        say(" endpoint: address size %ld, max data %ld, flags 0x%lX\n", (long)info.addr,
            (long)info.tsdu, (unsigned long)info.flags);
        OTCloseProvider(ep);
    }
}

static void list_interfaces(void)
{
    InetInterfaceInfo ii;
    SInt32 i;
    OSStatus err;

    say("\n[3] TCP/IP interfaces\n");
    for (i = 0; i < 8; i++) {
        memset(&ii, 0, sizeof(ii));
        err = OTInetGetInterfaceInfo(&ii, i);
        if (err != kOTNoError) {
            say(" interface %ld: error %ld\n", (long)i, (long)err);
            break;
        }
        say(" interface %ld:\n", (long)i);
        print_ip("   address   ", ii.fAddress);
        print_ip("   netmask   ", ii.fNetmask);
        print_ip("   gateway   ", ii.fDefaultGatewayAddr);
        print_ip("   DNS       ", ii.fDNSAddr);
        say("   MTU %lu, hardware address", (unsigned long)ii.fIfMTU);
        if (ii.fHWAddr && ii.fHWAddrLen <= 16) {
            UInt16 k;
            for (k = 0; k < ii.fHWAddrLen; k++)
                say("%s%02X", k ? ":" : " ", ii.fHWAddr[k]);
        }
        say("\n");
    }
}

int main(void)
{
    OSStatus err;

    g_out = fopen("OTCheck.txt", "w");
    say("OTCheck - Open Transport diagnostics\n");
    err = InitOpenTransport();
    if (err != kOTNoError) {
        say("InitOpenTransport: %ld\n", (long)err);
        return 0;
    }
    list_ports();
    open_tcp();
    list_interfaces();
    CloseOpenTransport();
    say("\nDone. Results also in \"OTCheck.txt\".\n");
    if (g_out)
        fclose(g_out);
    return 0;
}
