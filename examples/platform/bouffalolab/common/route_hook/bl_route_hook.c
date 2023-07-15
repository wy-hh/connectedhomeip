#include <stdbool.h>
#include <string.h>

#include "lwip/icmp6.h"
#include "lwip/mld6.h"
#include "lwip/netif.h"
#include "lwip/prot/icmp6.h"
#include "lwip/prot/ip6.h"
#include "lwip/prot/nd6.h"
#include "lwip/raw.h"

#include "bl_route_hook.h"
#include "bl_route_table.h"

#define log_info    printf
#define log_error   printf
typedef struct bl_route_hook_t
{
    struct netif * netif;
    struct raw_pcb * pcb;
    struct bl_route_hook_t * next;
} bl_route_hook_t;

PACK_STRUCT_BEGIN
struct rio_header_t
{
    PACK_STRUCT_FLD_8(u8_t type);
    PACK_STRUCT_FLD_8(u8_t length);
    PACK_STRUCT_FLD_8(u8_t prefix_length);
    PACK_STRUCT_FLD_8(u8_t preference);
    PACK_STRUCT_FIELD(u32_t route_lifetime);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

typedef struct rio_header_t rio_header_t;

/// MAC address length in bytes.
#define MAC_ADDR_LEN 6

/// MAC address structure.
struct mac_addr
{
    /// Array of 16-bit words that make up the MAC address.
    uint16_t array[MAC_ADDR_LEN/2];
};

/// Net interface
typedef struct netif        net_if_t;

struct fhost_vif_tag
{
    /// RTOS network interface structure
    net_if_t *net_if;
    /// MAC address of the VIF
    struct mac_addr mac_addr;
    /// Socket for scan events
    int scan_sock;
    /// is scanning
    bool scanning;
    /// if the socket used for IPC
    bool is_cntrl_link;
    /// Socket for connect/disconnect events
    int conn_sock;
    /// Socket for ftm events
    int ftm_sock;
    /// Pointer to the MAC VIF structure
    void *mac_vif; /* struct vif_info_tag *mac_vif; */
    /// Index of the STA being the AP peer of the device - TODO rework
    uint8_t ap_id;
    /// Parameter to indicate if admission control is mandatory for any access category - TODO rework
    uint8_t acm;
    /// UAPSD queue config for STA interface (bitfield, same format as QoS info)
    uint8_t uapsd_queues;
    /// Isolation Mode - Only used for AP
    bool isolation_mode;
    #if RW_MESH_EN
    /// List of mpath
    struct co_list mpath_list;
    /// List of available memory nodes
    struct co_list free_mpath_list;
    /// Mesh Path Information Pool
    struct fhost_mesh_path fhost_mesh_path_pool[RW_MESH_PATH_NB];
    /// Whether a frame is being resent on this interface
    bool is_resending;
    #endif
};

/// Structure used for the inter-task communication
struct fhost_env_tag
{
    /// Table of RTOS network interface structures
    struct fhost_vif_tag vif[CFG_VIF_MAX];
    /// Table linking the MAC VIFs to the FHOST VIFs
    struct fhost_vif_tag *mac2fhost_vif[CFG_VIF_MAX];
};

static bl_route_hook_t * s_hooks;

extern struct netif * deviceInterface_getNetif(void);

static bool is_self_address(struct netif * netif, const ip6_addr_t * addr)
{
    for (size_t i = 0; i < LWIP_ARRAYSIZE(netif->ip6_addr); i++)
    {
        if (ip6_addr_isvalid(netif_ip6_addr_state(netif, i)) &&
            memcmp(addr->addr, netif_ip6_addr(netif, i)->addr, sizeof(addr->addr)) == 0)
        {
            return true;
        }
    }
    return false;
}

static void ra_recv_handler(struct netif * netif, const uint8_t * icmp_payload, uint16_t payload_len, const ip6_addr_t * src_addr)
{
    if (payload_len < sizeof(struct ra_header))
    {
        return;
    }
    icmp_payload += sizeof(struct ra_header);
    payload_len -= sizeof(struct ra_header);

    while (payload_len >= 2)
    {
        uint8_t opt_type = icmp_payload[0];
        uint8_t opt_len  = icmp_payload[1] << 3;

        if (opt_type == ND6_OPTION_TYPE_ROUTE_INFO && opt_len >= sizeof(rio_header_t) && !is_self_address(netif, src_addr) &&
            payload_len >= opt_len)
        {
            rio_header_t rio_header;
            memcpy(&rio_header, icmp_payload, sizeof(rio_header));

            // skip if prefix is longer than IPv6 address.
            if (rio_header.prefix_length > 128)
            {
                break;
            }
            uint8_t prefix_len_bytes = (rio_header.prefix_length + 7) / 8;
            int8_t preference        = -2 * ((rio_header.preference >> 4) & 1) + (((rio_header.preference) >> 3) & 1);
            const uint8_t * rio_data = &icmp_payload[sizeof(rio_header_t)];
            uint8_t rio_data_len     = opt_len - sizeof(rio_header_t);

            printf("Received RIO\r\n");
            if (rio_data_len >= prefix_len_bytes)
            {
                ip6_addr_t prefix;
                bl_route_entry_t route;

                memset(&prefix, 0, sizeof(prefix));
                memcpy(&prefix.addr, rio_data, prefix_len_bytes);
                route.netif            = netif;
                route.gateway          = *src_addr;
                route.prefix_length    = rio_header.prefix_length;
                route.prefix           = prefix;
                route.preference       = preference;
                route.lifetime_seconds = lwip_ntohl(rio_header.route_lifetime);
                printf("prefix %s lifetime %lu\r\n", ip6addr_ntoa(&prefix), route.lifetime_seconds);
                if (bl_route_table_add_route_entry(&route) == NULL)
                {
                    printf("Failed to add route table entry\r\n");
                }
                else
                {
                    printf("Added entry to route table\r\n");
                }
            }
        }
        icmp_payload += opt_len;
        payload_len -= opt_len;
    }
}

static uint8_t icmp6_raw_recv_handler(void * arg, struct raw_pcb * pcb, struct pbuf * p, const ip_addr_t * addr)
{
    uint8_t * icmp_payload = NULL;
    uint16_t icmp_payload_len;
    struct ip6_hdr * ip6_header = (struct ip6_hdr *) p->payload;
    struct icmp6_hdr * icmp6_header;
    ip6_addr_t src;
    ip6_addr_t dest;
    bl_route_hook_t * hook = (bl_route_hook_t *) arg;

    memcpy(src.addr, ip6_header->src.addr, sizeof(src.addr));
    memcpy(dest.addr, ip6_header->dest.addr, sizeof(dest.addr));
#if LWIP_IPV6_SCOPES
    src.zone = 0;
#endif

    if (p->tot_len != p->len)
    {
        printf("Ignore segmented ICMP packet\r\n");
        return 0;
    }
    if (p->tot_len <= sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr))
    {
        printf("Ignore invalid ICMP packet\r\n");
        return 0;
    }
    if (!ip6_addr_islinklocal(&dest) && !ip6_addr_isallnodes_linklocal(&dest) && !ip6_addr_isallrouters_linklocal(&dest))
    {
        return 0;
    }

    icmp_payload_len = p->tot_len - sizeof(struct ip6_hdr);
    icmp_payload     = p->payload + sizeof(struct ip6_hdr);

    icmp6_header = (struct icmp6_hdr *) icmp_payload;
    if (icmp6_header->type == ICMP6_TYPE_RA)
    {
        ra_recv_handler(hook->netif, icmp_payload, icmp_payload_len, &src);
    }
    return 0;
}

int8_t bl_route_hook_init(void)
{
    ip_addr_t router_group    = IPADDR6_INIT_HOST(0xFF020000, 0, 0, 0x02);
    bl_route_hook_t * hook    = NULL;
    uint8_t ret               = 0;
    struct netif * lwip_netif = deviceInterface_getNetif();

    if (lwip_netif == NULL)
    {
        printf("Invalid network interface\r\n");
        return -1;
    }

    for (bl_route_hook_t * iter = s_hooks; iter != NULL; iter++)
    {
        if (iter->netif == lwip_netif)
        {
            return 0;
        }
    }

    hook = (bl_route_hook_t *) malloc(sizeof(bl_route_hook_t));
    if (hook == NULL)
    {
        printf("Cannot allocate hook\r\n");
        return -1;
    }

    if (mld6_joingroup_netif(lwip_netif, ip_2_ip6(&router_group)) != ERR_OK)
    {
        printf("Failed to join multicast group\r\n");
        ret = -1;
        goto exit;
    }

    hook->netif = lwip_netif;
    hook->pcb   = raw_new_ip_type(IPADDR_TYPE_V6, IP6_NEXTH_ICMP6);
    hook->pcb->flags |= RAW_FLAGS_MULTICAST_LOOP;
    hook->pcb->chksum_reqd = 1;
    // The ICMPv6 header checksum offset
    hook->pcb->chksum_offset = 2;
    raw_bind_netif(hook->pcb, lwip_netif);
    raw_recv(hook->pcb, icmp6_raw_recv_handler, hook);
    hook->next = s_hooks;
    s_hooks    = hook;

exit:
    if (ret != 0 && hook != NULL)
    {
        free(hook);
    }
    return ret;
}
