// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 Hans Kramer
//
// Network interfaces: link state and counters, one slot per interface.
//
// On this board that is seven: Wi-Fi, Ethernet, three CAN buses, the Docker
// bridge and loopback. CAN is one interface kind among them rather than a
// section of its own - it is a transport that happens to hang off the MCU,
// and treating it as the whole of networking was the wrong shape.
//
// Interfaces are resolved once, at open, like the thermal zones and for the
// same reason: the CSV header written at startup has to describe every row
// that follows it, and a list that grows mid-run would shift the columns
// under a reader. The cost is that an interface plugged in later is missed
// for the rest of the run - consistent with how every other source here
// behaves (see io.h: a sysfs attribute absent at startup stays absent), but
// worth knowing, because a USB CDC link or a container bridge can appear at
// any time.
//
// Counters are emitted raw, not as rates. A consumer with two samples and
// the `t` on each can divide; one computed here would be a rate over this
// sampler's interval, which is not necessarily the consumer's.

#define _GNU_SOURCE
#include "io.h"
#include "sampler.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ARPHRD values from linux/if_arp.h, named here so this does not pull the
 * kernel header in for three constants. */
#define ARPHRD_ETHER    1u
#define ARPHRD_CAN      280u
#define ARPHRD_LOOPBACK 772u

#define NET_DIR "/sys/class/net"

struct iface {
    char        name[32];
    const char *kind;
    struct src  operstate, carrier, mtu, speed;
    struct src  rx_bytes, tx_bytes, rx_packets, tx_packets;
    struct src  rx_errors, tx_errors, rx_dropped, tx_dropped;
};

static struct { struct iface f[NET_MAX]; size_t n; int ok; } st;

/* Rank for the sort below. Loopback and the bridges last: on a board with
 * seven interfaces the ones that carry traffic should come first, and the
 * order has to be the same every run or the CSV columns move. */
static int kind_rank(const char *kind)
{
    if (!strcmp(kind, "ethernet")) return 0;
    if (!strcmp(kind, "wifi"))     return 1;
    if (!strcmp(kind, "can"))      return 2;
    if (!strcmp(kind, "usb"))      return 3;
    if (!strcmp(kind, "other"))    return 4;
    if (!strcmp(kind, "bridge"))   return 5;
    return 6;                                   /* loopback */
}

/* src_open applies the sysroot itself; spath() is for the raw syscalls
 * below that do not go through it. Prefixing twice breaks - see io.h. */
static int exists_at(const char *iface, const char *attr)
{
    struct src probe;
    int found;

    src_init(&probe);
    src_open(&probe, NET_DIR "/%s/%s", iface, attr);
    found = src_have(&probe);
    src_close(&probe);
    return found;
}

static uint64_t read_u64_at(const char *iface, const char *attr, int *ok)
{
    struct src s;
    uint64_t v = 0;

    src_init(&s);
    src_open(&s, NET_DIR "/%s/%s", iface, attr);
    *ok = src_have(&s) && src_u64(&s, &v) == 0;
    src_close(&s);
    return v;
}

/*
 * The ARPHRD type identifies CAN and loopback outright, but not Wi-Fi: a
 * managed-mode station reports ARPHRD_ETHER (1) exactly as a wired port
 * does, so the wireless directory is what tells them apart.
 */
static const char *classify(const char *iface)
{
    int ok = 0;
    uint64_t type = read_u64_at(iface, "type", &ok);

    if (ok && type == ARPHRD_CAN)
        return "can";
    if (ok && type == ARPHRD_LOOPBACK)
        return "loopback";
    if (exists_at(iface, "wireless") || exists_at(iface, "phy80211"))
        return "wifi";
    if (exists_at(iface, "bridge"))
        return "bridge";
    /* A USB CDC link is an ethernet device whose parent chain runs through a
     * USB controller, and the device symlink is the only place that shows. */
    {
        char path[PATH_MAXLEN];
        char link[PATH_MAXLEN];
        ssize_t n;

        spath(path, sizeof path, NET_DIR "/%s/device", iface);
        n = readlink(path, link, sizeof link - 1);
        if (n > 0) {
            link[n] = '\0';
            if (strstr(link, "usb"))
                return "usb";
        }
    }
    if (ok && type == ARPHRD_ETHER)
        return "ethernet";
    return "other";
}

static void open_counter(struct src *s, const char *iface, const char *attr)
{
    src_init(s);
    src_open(s, NET_DIR "/%s/statistics/%s", iface, attr);
}

static void iface_open(struct iface *f, const char *name)
{
    size_t len = strlen(name);

    /* net_open already dropped anything that does not fit, but the bound is
     * kept here too: a guarantee that lives only in the caller is one the
     * next reader of this function has to go and find. */
    if (len >= sizeof f->name)
        len = sizeof f->name - 1;
    memcpy(f->name, name, len);
    f->name[len] = '\0';
    f->kind = classify(name);

    src_init(&f->operstate);
    src_open(&f->operstate, NET_DIR "/%s/operstate", name);
    src_init(&f->carrier);
    src_open(&f->carrier, NET_DIR "/%s/carrier", name);
    src_init(&f->mtu);
    src_open(&f->mtu, NET_DIR "/%s/mtu", name);
    src_init(&f->speed);
    src_open(&f->speed, NET_DIR "/%s/speed", name);

    open_counter(&f->rx_bytes,   name, "rx_bytes");
    open_counter(&f->tx_bytes,   name, "tx_bytes");
    open_counter(&f->rx_packets, name, "rx_packets");
    open_counter(&f->tx_packets, name, "tx_packets");
    open_counter(&f->rx_errors,  name, "rx_errors");
    open_counter(&f->tx_errors,  name, "tx_errors");
    open_counter(&f->rx_dropped, name, "rx_dropped");
    open_counter(&f->tx_dropped, name, "tx_dropped");
}

static void iface_close(struct iface *f)
{
    src_close(&f->operstate);
    src_close(&f->carrier);
    src_close(&f->mtu);
    src_close(&f->speed);
    src_close(&f->rx_bytes);
    src_close(&f->tx_bytes);
    src_close(&f->rx_packets);
    src_close(&f->tx_packets);
    src_close(&f->rx_errors);
    src_close(&f->tx_errors);
    src_close(&f->rx_dropped);
    src_close(&f->tx_dropped);
}

static int net_open(void)
{
    char dir[PATH_MAXLEN];
    char names[NET_MAX][32];
    size_t n = 0;
    DIR *d;
    struct dirent *e;
    size_t i, j;

    st.n = 0;
    st.ok = 0;

    spath(dir, sizeof dir, NET_DIR);
    d = opendir(dir);
    if (!d) {
        fprintf(stderr, "qcstats: %s unreadable; no network section\n",
                NET_DIR);
        return -1;
    }
    while ((e = readdir(d)) != NULL && n < NET_MAX) {
        if (e->d_name[0] == '.')
            continue;
        /* IFNAMSIZ is 16, so this cannot happen on a sane kernel - but a
         * truncated name would address a different interface on every read
         * after this one, so it is skipped rather than shortened. */
        if (strlen(e->d_name) >= sizeof names[0]) {
            fprintf(stderr, "qcstats: interface name too long, skipped\n");
            continue;
        }
        memcpy(names[n], e->d_name, strlen(e->d_name) + 1);
        n++;
    }
    closedir(d);
    if (n == 0)
        return -1;

    /* Sorted by kind then name. readdir order is not promised to be stable
     * between runs, and the CSV header has to describe the same columns the
     * rows carry. */
    for (i = 0; i < n; i++) {
        iface_open(&st.f[i], names[i]);
    }
    for (i = 1; i < n; i++) {
        struct iface key = st.f[i];
        j = i;
        while (j > 0) {
            const struct iface *prev = &st.f[j - 1];
            int rank = kind_rank(prev->kind) - kind_rank(key.kind);

            if (rank < 0 || (rank == 0 && strcmp(prev->name, key.name) < 0))
                break;
            st.f[j] = st.f[j - 1];
            j--;
        }
        st.f[j] = key;
    }

    st.n = n;
    st.ok = 1;
    return 0;
}

static void read_counter(const struct src *s, uint64_t *v, int *ok)
{
    uint64_t raw = 0;

    *ok = (src_u64(s, &raw) == 0);
    *v = *ok ? raw : 0;
}

static void net_read(struct sample *s)
{
    size_t i;

    s->nnet = 0;
    if (!st.ok)
        return;

    for (i = 0; i < st.n; i++) {
        const struct iface *f = &st.f[i];

        snprintf(s->net[i].name, sizeof s->net[i].name, "%s", f->name);
        s->net[i].kind = f->kind;

        /* Straight into the field: the longest value the kernel writes is
         * "lowerlayerdown" at fourteen characters, and staging it through a
         * wider buffer only invites a truncating copy afterwards. */
        s->net[i].operstate_ok =
            (src_str(&f->operstate, s->net[i].operstate,
                     sizeof s->net[i].operstate) == 0);
        if (s->net[i].operstate_ok) {
            /* src_str hands back the file verbatim, newline included. */
            size_t len = strlen(s->net[i].operstate);

            while (len > 0 && (s->net[i].operstate[len - 1] == '\n' ||
                               s->net[i].operstate[len - 1] == '\r' ||
                               s->net[i].operstate[len - 1] == ' '))
                s->net[i].operstate[--len] = '\0';
        } else {
            s->net[i].operstate[0] = '\0';
        }

        read_counter(&f->carrier, &s->net[i].carrier.v, &s->net[i].carrier.ok);
        read_counter(&f->mtu, &s->net[i].mtu.v, &s->net[i].mtu.ok);

        /* speed reports -1 for a down link, which src_u64 will not parse -
         * and -1 Mbit/s is not a measurement, so an unparsed value is
         * exactly right here. */
        read_counter(&f->speed, &s->net[i].speed_mbit.v,
                     &s->net[i].speed_mbit.ok);

        read_counter(&f->rx_bytes, &s->net[i].rx_bytes.v,
                     &s->net[i].rx_bytes.ok);
        read_counter(&f->tx_bytes, &s->net[i].tx_bytes.v,
                     &s->net[i].tx_bytes.ok);
        read_counter(&f->rx_packets, &s->net[i].rx_packets.v,
                     &s->net[i].rx_packets.ok);
        read_counter(&f->tx_packets, &s->net[i].tx_packets.v,
                     &s->net[i].tx_packets.ok);
        read_counter(&f->rx_errors, &s->net[i].rx_errors.v,
                     &s->net[i].rx_errors.ok);
        read_counter(&f->tx_errors, &s->net[i].tx_errors.v,
                     &s->net[i].tx_errors.ok);
        read_counter(&f->rx_dropped, &s->net[i].rx_dropped.v,
                     &s->net[i].rx_dropped.ok);
        read_counter(&f->tx_dropped, &s->net[i].tx_dropped.v,
                     &s->net[i].tx_dropped.ok);
    }
    s->nnet = (int)st.n;
}

static void net_close(void)
{
    size_t i;

    for (i = 0; i < st.n; i++)
        iface_close(&st.f[i]);
    st.n = 0;
    st.ok = 0;
}

const struct sampler sampler_net = {
    .name  = "net",
    .open  = net_open,
    .read  = net_read,
    .close = net_close,
};
