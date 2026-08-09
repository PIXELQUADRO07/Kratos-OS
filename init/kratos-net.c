/* kratos-net.c — KratosOS Network Management Utility & Native DHCP Client
 *
 * Responsabilità:
 *   1. Imposta l'interfaccia di loopback (lo -> 127.0.0.1/8)
 *   2. Scansiona le interfacce di rete fisiche/virtuali (eth0, enp*, wlan*)
 *   3. Configura indirizzi IP statici o richiede un lease via Client DHCP nativo
 *   4. Configura la tabella di routing di default (gateway)
 *   5. Genera /etc/resolv.conf con i server DNS ottenuti
 *
 * Uso:
 *   kratos-net --auto          (Configurazione automatica all'avvio)
 *   kratos-net --iface eth0 --dhcp
 *   kratos-net --iface eth0 --ip 192.168.1.100 --netmask 255.255.255.0 --gw 192.168.1.1
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /sbin/kratos-net kratos-net.c
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_CHADDR_LEN 16

/* Structure for DHCP Packet Header */
typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[DHCP_CHADDR_LEN];
    char     sname[64];
    char     file[128];
    uint32_t cookie;
    uint8_t  options[308];
} __attribute__((packed)) dhcp_packet_t;

/* ------------------------------------------------------------------ */
/* Loopback Setup                                                      */
/* ------------------------------------------------------------------ */

static int setup_loopback(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("[kratos-net] socket AF_INET failed");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);

    /* Set IP address 127.0.0.1 */
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
    ioctl(sock, SIOCSIFADDR, &ifr);

    /* Set netmask 255.0.0.0 */
    inet_pton(AF_INET, "255.0.0.0", &sin->sin_addr);
    ioctl(sock, SIOCSIFNETMASK, &ifr);

    /* Bring interface UP */
    ioctl(sock, SIOCGIFFLAGS, &ifr);
    ifr.ifr_flags |= (IFF_UP | IFF_LOOPBACK | IFF_RUNNING);
    ioctl(sock, SIOCSIFFLAGS, &ifr);

    close(sock);
    printf("[kratos-net] Loopback interface (lo) configured [127.0.0.1/8].\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* IP & Gateway Setup                                                  */
/* ------------------------------------------------------------------ */

static int set_iface_ip(const char *ifname, const char *ip_str, const char *mask_str)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;

    if (inet_pton(AF_INET, ip_str, &sin->sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        perror("[kratos-net] SIOCSIFADDR");
    }

    if (mask_str) {
        inet_pton(AF_INET, mask_str, &sin->sin_addr);
        ioctl(sock, SIOCSIFNETMASK, &ifr);
    }

    /* Bring UP */
    ioctl(sock, SIOCGIFFLAGS, &ifr);
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
    ioctl(sock, SIOCSIFFLAGS, &ifr);

    close(sock);
    printf("[kratos-net] %s set to %s netmask %s\n", ifname, ip_str, mask_str ? mask_str : "255.255.255.0");
    return 0;
}

static int set_default_gateway(const char *ifname, const char *gw_str)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct rtentry rt;
    memset(&rt, 0, sizeof(rt));

    struct sockaddr_in *dst = (struct sockaddr_in *)&rt.rt_dst;
    struct sockaddr_in *gw  = (struct sockaddr_in *)&rt.rt_gateway;
    struct sockaddr_in *genmask = (struct sockaddr_in *)&rt.rt_genmask;

    dst->sin_family = AF_INET;
    dst->sin_addr.s_addr = INADDR_ANY;

    genmask->sin_family = AF_INET;
    genmask->sin_addr.s_addr = INADDR_ANY;

    gw->sin_family = AF_INET;
    inet_pton(AF_INET, gw_str, &gw->sin_addr);

    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    rt.rt_dev = (char *)ifname;

    if (ioctl(sock, SIOCADDRT, &rt) < 0 && errno != EEXIST) {
        perror("[kratos-net] SIOCADDRT default gateway");
        close(sock);
        return -1;
    }

    close(sock);
    printf("[kratos-net] Default gateway set to %s via %s\n", gw_str, ifname);
    return 0;
}

static void update_resolv_conf(const char *dns1, const char *dns2)
{
    FILE *f = fopen("/etc/resolv.conf", "w");
    if (!f) return;

    fprintf(f, "# Generated by kratos-net\n");
    if (dns1 && strlen(dns1) > 0) fprintf(f, "nameserver %s\n", dns1);
    if (dns2 && strlen(dns2) > 0) fprintf(f, "nameserver %s\n", dns2);
    if (!dns1 && !dns2) {
        fprintf(f, "nameserver 1.1.1.1\n");
        fprintf(f, "nameserver 8.8.8.8\n");
    }

    fclose(f);
    printf("[kratos-net] /etc/resolv.conf updated.\n");
}

/* ------------------------------------------------------------------ */
/* Simple Native DHCP Client                                           */
/* ------------------------------------------------------------------ */

static int run_dhcp_client(const char *ifname)
{
    printf("[kratos-net] Starting native DHCP client on %s...\n", ifname);

    /* First, bring interface UP */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
        ioctl(sock, SIOCGIFFLAGS, &ifr);
        ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
        ioctl(sock, SIOCSIFFLAGS, &ifr);
        close(sock);
    }

    /* Create UDP socket bound to 0.0.0.0:68 with SO_BROADCAST */
    int dhcp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dhcp_sock < 0) {
        perror("[kratos-net] DHCP socket creation failed");
        return -1;
    }

    int optval = 1;
    setsockopt(dhcp_sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
    setsockopt(dhcp_sock, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname));

    struct sockaddr_in client_sa;
    memset(&client_sa, 0, sizeof(client_sa));
    client_sa.sin_family = AF_INET;
    client_sa.sin_port = htons(DHCP_CLIENT_PORT);
    client_sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(dhcp_sock, (struct sockaddr *)&client_sa, sizeof(client_sa)) < 0) {
        perror("[kratos-net] DHCP bind failed");
        close(dhcp_sock);
        return -1;
    }

    /* Get MAC address of interface */
    uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
        if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
            memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
        }
        close(s);
    }

    /* Build DHCP DISCOVER Packet */
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.op = 1; /* BOOTREQUEST */
    pkt.htype = 1; /* Ethernet */
    pkt.hlen = 6;
    pkt.xid = htonl(0x12345678);
    pkt.flags = htons(0x8000); /* Broadcast flag */
    memcpy(pkt.chaddr, mac, 6);
    pkt.cookie = htonl(0x63825363); /* Magic Cookie */

    /* Options: DHCP Discover (Option 53 = 1), End (255) */
    pkt.options[0] = 53;
    pkt.options[1] = 1;
    pkt.options[2] = 1; /* Discover */
    pkt.options[3] = 255; /* End */

    struct sockaddr_in server_sa;
    memset(&server_sa, 0, sizeof(server_sa));
    server_sa.sin_family = AF_INET;
    server_sa.sin_port = htons(DHCP_SERVER_PORT);
    server_sa.sin_addr.s_addr = INADDR_BROADCAST;

    sendto(dhcp_sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&server_sa, sizeof(server_sa));

    /* Receive OFFER / ACK (Timeout 3 sec) */
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(dhcp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    dhcp_packet_t offer_pkt;
    ssize_t res = recv(dhcp_sock, &offer_pkt, sizeof(offer_pkt), 0);

    if (res <= 0) {
        printf("[kratos-net] DHCP timeout on %s. Setting fallback IP 192.168.1.150...\n", ifname);
        close(dhcp_sock);
        set_iface_ip(ifname, "192.168.1.150", "255.255.255.0");
        set_default_gateway(ifname, "192.168.1.1");
        update_resolv_conf("1.1.1.1", "8.8.8.8");
        return 0;
    }

    struct in_addr assigned_ip;
    assigned_ip.s_addr = offer_pkt.yiaddr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &assigned_ip, ip_str, sizeof(ip_str));

    printf("[kratos-net] Received DHCP lease: %s on %s\n", ip_str, ifname);

    /* Parse Subnet Mask and Router from Options */
    char mask_str[INET_ADDRSTRLEN] = "255.255.255.0";
    char gw_str[INET_ADDRSTRLEN]   = "";
    char dns_str[INET_ADDRSTRLEN]  = "1.1.1.1";

    int idx = 0;
    while (idx < 300 && offer_pkt.options[idx] != 255) {
        uint8_t opt = offer_pkt.options[idx];
        if (opt == 0) { idx++; continue; }
        uint8_t opt_len = offer_pkt.options[idx + 1];

        if (opt == 1 && opt_len == 4) { /* Subnet Mask */
            struct in_addr m;
            memcpy(&m.s_addr, &offer_pkt.options[idx + 2], 4);
            inet_ntop(AF_INET, &m, mask_str, sizeof(mask_str));
        } else if (opt == 3 && opt_len >= 4) { /* Router / Gateway */
            struct in_addr g;
            memcpy(&g.s_addr, &offer_pkt.options[idx + 2], 4);
            inet_ntop(AF_INET, &g, gw_str, sizeof(gw_str));
        } else if (opt == 6 && opt_len >= 4) { /* DNS Server */
            struct in_addr d;
            memcpy(&d.s_addr, &offer_pkt.options[idx + 2], 4);
            inet_ntop(AF_INET, &d, dns_str, sizeof(dns_str));
        }

        idx += 2 + opt_len;
    }

    close(dhcp_sock);

    set_iface_ip(ifname, ip_str, mask_str);
    if (gw_str[0] != '\0') {
        set_default_gateway(ifname, gw_str);
    }
    update_resolv_conf(dns_str, "8.8.8.8");

    return 0;
}

/* ------------------------------------------------------------------ */
/* Interface Auto-Discovery & Auto Mode                              */
/* ------------------------------------------------------------------ */

static void auto_configure_network(void)
{
    setup_loopback();

    DIR *d = opendir("/sys/class/net");
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "lo") == 0) continue;

        printf("[kratos-net] Discovered interface: %s\n", entry->d_name);
        run_dhcp_client(entry->d_name);
        break; /* Configure primary ethernet interface */
    }

    closedir(d);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2 || strcmp(argv[1], "--auto") == 0) {
        auto_configure_network();
        return 0;
    }

    char ifname[IFNAMSIZ] = "eth0";
    char ip[64] = "";
    char mask[64] = "255.255.255.0";
    char gw[64] = "";
    int dhcp = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
            strncpy(ifname, argv[++i], sizeof(ifname) - 1);
        } else if (strcmp(argv[i], "--dhcp") == 0) {
            dhcp = 1;
        } else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            strncpy(ip, argv[++i], sizeof(ip) - 1);
        } else if (strcmp(argv[i], "--netmask") == 0 && i + 1 < argc) {
            strncpy(mask, argv[++i], sizeof(mask) - 1);
        } else if (strcmp(argv[i], "--gw") == 0 && i + 1 < argc) {
            strncpy(gw, argv[++i], sizeof(gw) - 1);
        }
    }

    setup_loopback();

    if (dhcp) {
        run_dhcp_client(ifname);
    } else if (ip[0] != '\0') {
        set_iface_ip(ifname, ip, mask);
        if (gw[0] != '\0') {
            set_default_gateway(ifname, gw);
        }
        update_resolv_conf("1.1.1.1", "8.8.8.8");
    }

    return 0;
}
