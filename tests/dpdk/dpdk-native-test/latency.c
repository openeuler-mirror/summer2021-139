#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>

#include <sys/queue.h>
#include <sys/stat.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_debug.h>
#include <rte_cycles.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_flow.h>

#include "config.h"
#include "stats.h"

/* use RFC863 Discard Protocol */
static uint16_t tx_udp_src_port = 9;
static uint16_t tx_udp_dst_port = 9;

/* use RFC5735 / RFC2544 reserved network test addresses */
static uint32_t tx_ip_src_addr = (198U << 24) | (18 << 16) | (0 << 8) | 1;
static uint32_t tx_ip_dst_addr = (198U << 24) | (18 << 16) | (0 << 8) | 2;

#define IP_DEFTTL  64   /* from RFC 1340. */

static struct rte_ipv4_hdr pkt_ip_hdr; /**< IP header of transmitted packets. */
//RTE_DEFINE_PER_LCORE(uint8_t, _ip_var); /**< IP address variation */
static struct rte_udp_hdr pkt_udp_hdr; /**< UDP header of tx packets. */

#define TEST_ECHO_TIMES_NB 10  /* 10 burst of echo packets */
#define TEST_INTERVAL_US 1e6

static uint64_t timer_start_tsc;
static uint64_t timer_prev_tsc;
static uint64_t timer_period;
static uint64_t timer_curr_tsc;
static uint64_t timer_diff_tsc;

static uint64_t echo_nb = 0;

static void
copy_buf_to_pkt_segs(void* buf, unsigned len, struct rte_mbuf *pkt,
		     unsigned offset)
{
	struct rte_mbuf *seg;
	void *seg_buf;
	unsigned copy_len;

	seg = pkt;
	while (offset >= seg->data_len) {
		offset -= seg->data_len;
		seg = seg->next;
	}
	copy_len = seg->data_len - offset;
	seg_buf = rte_pktmbuf_mtod_offset(seg, char *, offset);
	while (len > copy_len) {
		rte_memcpy(seg_buf, buf, (size_t) copy_len);
		len -= copy_len;
		buf = ((char*) buf + copy_len);
		seg = seg->next;
		seg_buf = rte_pktmbuf_mtod(seg, char *);
		copy_len = seg->data_len;
	}
	rte_memcpy(seg_buf, buf, (size_t) len);
}

static inline void
copy_buf_to_pkt(void* buf, unsigned len, struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset),
			buf, (size_t) len);
		return;
	}
	copy_buf_to_pkt_segs(buf, len, pkt, offset);
}

static void
setup_pkt_udp_ip_headers(struct rte_ipv4_hdr *ip_hdr,
			 struct rte_udp_hdr *udp_hdr,
			 uint16_t pkt_data_len)
{
	uint16_t pkt_len;

	/*
	 * Initialize UDP header.
	 */
	pkt_len = (uint16_t) (pkt_data_len + sizeof(struct rte_udp_hdr));
	udp_hdr->src_port = rte_cpu_to_be_16(tx_udp_src_port);
	udp_hdr->dst_port = rte_cpu_to_be_16(tx_udp_dst_port);
	udp_hdr->dgram_len      = RTE_CPU_TO_BE_16(pkt_len);
	udp_hdr->dgram_cksum    = 0; /* No UDP checksum. */

	/*
	 * Initialize IP header.
	 */
	pkt_len = (uint16_t) (pkt_len + sizeof(struct rte_ipv4_hdr));
	ip_hdr->version_ihl   = RTE_IPV4_VHL_DEF;
	ip_hdr->type_of_service   = 0;
	ip_hdr->fragment_offset = 0;
	ip_hdr->time_to_live   = IP_DEFTTL;
	ip_hdr->next_proto_id = IPPROTO_UDP;
	ip_hdr->packet_id = 0;
	ip_hdr->total_length   = RTE_CPU_TO_BE_16(pkt_len);
	ip_hdr->src_addr = rte_cpu_to_be_32(tx_ip_src_addr);
	ip_hdr->dst_addr = rte_cpu_to_be_32(tx_ip_dst_addr);

	/*
	 * Compute IP header checksum.
	 */
	ip_hdr->hdr_checksum = 0;
	ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
}

static inline bool
pkt_burst_prepare(struct rte_mbuf *pkt, struct rte_mempool *mbp,
		struct rte_ether_hdr *eth_hdr, const uint16_t vlan_tci,
		const uint16_t vlan_tci_outer, const uint64_t ol_flags)
{
	struct rte_mbuf *pkt_segs[RTE_MAX_SEGS_PER_PKT];
	struct rte_mbuf *pkt_seg;
	uint32_t nb_segs, pkt_len;
	uint8_t i;

	if (unlikely(tx_pkt_split == TX_PKT_SPLIT_RND))
		nb_segs = rte_rand() % tx_pkt_nb_segs + 1;
	else
		nb_segs = tx_pkt_nb_segs;

	if (nb_segs > 1) {
		if (rte_mempool_get_bulk(mbp, (void **)pkt_segs, nb_segs - 1))
			return false;
	}

	rte_pktmbuf_reset_headroom(pkt);
	pkt->data_len = tx_pkt_seg_lengths[0];
	pkt->ol_flags = ol_flags;
	pkt->vlan_tci = vlan_tci;
	pkt->vlan_tci_outer = vlan_tci_outer;
	pkt->l2_len = sizeof(struct rte_ether_hdr);
	pkt->l3_len = sizeof(struct rte_ipv4_hdr);

	pkt_len = pkt->data_len;
	pkt_seg = pkt;
	for (i = 1; i < nb_segs; i++) {
		pkt_seg->next = pkt_segs[i - 1];
		pkt_seg = pkt_seg->next;
		pkt_seg->data_len = tx_pkt_seg_lengths[i];
		pkt_len += pkt_seg->data_len;
	}
	pkt_seg->next = NULL; /* Last segment of packet. */
	/*
	 * Copy headers in first packet segment(s).
	 */
	copy_buf_to_pkt(eth_hdr, sizeof(*eth_hdr), pkt, 0);
	copy_buf_to_pkt(&pkt_ip_hdr, sizeof(pkt_ip_hdr), pkt,
			sizeof(struct rte_ether_hdr));
	// if (txonly_multi_flow) { ...
	copy_buf_to_pkt(&pkt_udp_hdr, sizeof(pkt_udp_hdr), pkt,
			sizeof(struct rte_ether_hdr) +
			sizeof(struct rte_ipv4_hdr));
	/*
	 * Complete first mbuf of packet and append it to the
	 * burst of packets to be transmitted.
	 */
	pkt->nb_segs = nb_segs;
	pkt->pkt_len = pkt_len;

	return true;
}

/*
 * Transmit a burst of multi-segments packets.
 */
static void
pkt_burst_transmit(struct fwd_stream *fs)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	struct rte_port *txp;
	struct rte_mbuf *pkt;
	struct rte_mempool *mbp;
	struct rte_ether_hdr eth_hdr;
	uint16_t nb_tx;
	uint16_t nb_pkt;
	uint16_t vlan_tci, vlan_tci_outer;
	uint32_t retry;
	uint64_t ol_flags = 0;
	uint64_t tx_offloads;
#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	uint64_t start_tsc;
	uint64_t end_tsc;
	uint64_t core_cycles;
#endif

#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	start_tsc = rte_rdtsc();
#endif


    timer_curr_tsc = rte_rdtsc();
    timer_diff_tsc = timer_curr_tsc - timer_prev_tsc;
    if (likely(timer_diff_tsc < timer_period)) {
        return; /* not yet, continue */
    }
    echo_nb++;
    timer_prev_tsc = timer_curr_tsc;
    

// 	mbp = current_fwd_lcore()->mbp;
// 	txp = &ports[fs->tx_port];
// 	tx_offloads = txp->dev_conf.txmode.offloads;
// 	vlan_tci = txp->tx_vlan_id;
// 	vlan_tci_outer = txp->tx_vlan_id_outer;
// 	if (tx_offloads	& DEV_TX_OFFLOAD_VLAN_INSERT)
// 		ol_flags = PKT_TX_VLAN_PKT;
// 	if (tx_offloads & DEV_TX_OFFLOAD_QINQ_INSERT)
// 		ol_flags |= PKT_TX_QINQ_PKT;
// 	if (tx_offloads & DEV_TX_OFFLOAD_MACSEC_INSERT)
// 		ol_flags |= PKT_TX_MACSEC;

// 	/*
// 	 * Initialize Ethernet header.
// 	 */
// 	 /* from ... to ... */
// 	rte_ether_addr_copy(&peer_eth_addrs[fs->peer_addr], &eth_hdr.d_addr);
// 	rte_ether_addr_copy(&ports[fs->tx_port].eth_addr, &eth_hdr.s_addr);
// 	eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

// 	if (rte_mempool_get_bulk(mbp, (void **)pkts_burst,
// 				nb_pkt_per_burst) == 0) {
// 		for (nb_pkt = 0; nb_pkt < nb_pkt_per_burst; nb_pkt++) {
// 			if (unlikely(!pkt_burst_prepare(pkts_burst[nb_pkt], mbp,
// 							&eth_hdr, vlan_tci,
// 							vlan_tci_outer,
// 							ol_flags))) {
// 				rte_mempool_put_bulk(mbp,
// 						(void **)&pkts_burst[nb_pkt],
// 						nb_pkt_per_burst - nb_pkt);
// 				break;
// 			}
// 		}
// 	} else {
// 		for (nb_pkt = 0; nb_pkt < nb_pkt_per_burst; nb_pkt++) {
// 			pkt = rte_mbuf_raw_alloc(mbp);
// 			if (pkt == NULL)
// 				break;
// 			if (unlikely(!pkt_burst_prepare(pkt, mbp, &eth_hdr,
// 							vlan_tci,
// 							vlan_tci_outer,
// 							ol_flags))) {
// 				rte_pktmbuf_free(pkt);
// 				break;
// 			}
// 			pkts_burst[nb_pkt] = pkt;
// 		}
// 	}

// 	if (nb_pkt == 0)
// 		return;

// 	nb_tx = rte_eth_tx_burst(fs->tx_port, fs->tx_queue, pkts_burst, nb_pkt);
// 	/*
// 	 * Retry if necessary
// 	 */
// 	if (unlikely(nb_tx < nb_pkt) && fs->retry_enabled) {
// 		retry = 0;
// 		while (nb_tx < nb_pkt && retry++ < burst_tx_retry_num) {
// 			rte_delay_us(burst_tx_delay_time);
// 			nb_tx += rte_eth_tx_burst(fs->tx_port, fs->tx_queue,
// 					&pkts_burst[nb_tx], nb_pkt - nb_tx);
// 		}
// 	}
// 	fs->tx_packets += nb_tx;

// 	// if (txonly_multi_flow)
// 	// 	RTE_PER_LCORE(_ip_var) -= nb_pkt - nb_tx;

// #ifdef RTE_TEST_PMD_RECORD_BURST_STATS
// 	fs->tx_burst_stats.pkt_burst_spread[nb_tx]++;
// #endif
// 	if (unlikely(nb_tx < nb_pkt)) {
// 		fs->fwd_dropped += (nb_pkt - nb_tx);
// 		do {
// 			rte_pktmbuf_free(pkts_burst[nb_tx]);
// 		} while (++nb_tx < nb_pkt);
// 	}

// #ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
// 	end_tsc = rte_rdtsc();
// 	core_cycles = (end_tsc - start_tsc);
// 	fs->core_cycles = (uint64_t) (fs->core_cycles + core_cycles);
// #endif

    if (echo_nb >= TEST_ECHO_TIMES_NB) {
        fs->done = true;
    }
}

static void
tx_only_begin(void *arg)
{
	uint16_t pkt_data_len;
	timer_period = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S *
			TEST_INTERVAL_US;

	printf("starting... press ctrl + c to quit and see statistics\n");
	pkt_data_len = (uint16_t) (tx_pkt_length - (
					sizeof(struct rte_ether_hdr) +
					sizeof(struct rte_ipv4_hdr) +
					sizeof(struct rte_udp_hdr)));
	setup_pkt_udp_ip_headers(&pkt_ip_hdr, &pkt_udp_hdr, pkt_data_len);
	timer_start_tsc = rte_rdtsc();
    timer_curr_tsc = timer_start_tsc;
}

static void
tx_only_end(void *arg)
{
    latency_stats_display();
}

struct fwd_engine latency_engine = {
	.fwd_mode_name  = "txonly",
	.port_fwd_begin = tx_only_begin,
	.port_fwd_end   = tx_only_end,
	.packet_fwd     = pkt_burst_transmit,
};
