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


struct queue_stats_mappings tx_queue_stats_mappings_array[MAX_TX_QUEUE_STATS_MAPPINGS];
struct queue_stats_mappings rx_queue_stats_mappings_array[MAX_RX_QUEUE_STATS_MAPPINGS];

struct queue_stats_mappings *tx_queue_stats_mappings = tx_queue_stats_mappings_array;
struct queue_stats_mappings *rx_queue_stats_mappings = rx_queue_stats_mappings_array;

uint16_t nb_tx_queue_stats_mappings = 0;
uint16_t nb_rx_queue_stats_mappings = 0;

uint64_t start_tsc_stats;

static int
set_tx_queue_stats_mapping_registers(portid_t port_id, struct rte_port *port)
{
	uint16_t i;
	int diag;
	uint8_t mapping_found = 0;

	for (i = 0; i < nb_tx_queue_stats_mappings; i++) {
		if ((tx_queue_stats_mappings[i].port_id == port_id) &&
				(tx_queue_stats_mappings[i].queue_id < 1 /* nb_txq */)) {
			diag = rte_eth_dev_set_tx_queue_stats_mapping(port_id,
					tx_queue_stats_mappings[i].queue_id,
					tx_queue_stats_mappings[i].stats_counter_id);
			if (diag != 0)
				return diag;
			mapping_found = 1;
		}
	}
	if (mapping_found)
		port->tx_queue_stats_mapping_enabled = 1;
	return 0;
}

static int
set_rx_queue_stats_mapping_registers(portid_t port_id, struct rte_port *port)
{
	uint16_t i;
	int diag;
	uint8_t mapping_found = 0;

	for (i = 0; i < nb_rx_queue_stats_mappings; i++) {
		if ((rx_queue_stats_mappings[i].port_id == port_id) &&
				(rx_queue_stats_mappings[i].queue_id < 1 /* nb_rxq */ )) {
			diag = rte_eth_dev_set_rx_queue_stats_mapping(port_id,
					rx_queue_stats_mappings[i].queue_id,
					rx_queue_stats_mappings[i].stats_counter_id);
			if (diag != 0)
				return diag;
			mapping_found = 1;
		}
	}
	if (mapping_found)
		port->rx_queue_stats_mapping_enabled = 1;
	return 0;
}

void
map_port_queue_stats_mapping_registers(portid_t pi, struct rte_port *port)
{
	int diag = 0;

	diag = set_tx_queue_stats_mapping_registers(pi, port);
	if (diag != 0) {
		if (diag == -ENOTSUP) {
			port->tx_queue_stats_mapping_enabled = 0;
			printf("TX queue stats mapping not supported port id=%d\n", pi);
		}
		else
			rte_exit(EXIT_FAILURE,
					"set_tx_queue_stats_mapping_registers "
					"failed for port id=%d diag=%d\n",
					pi, diag);
	}

	diag = set_rx_queue_stats_mapping_registers(pi, port);
	if (diag != 0) {
		if (diag == -ENOTSUP) {
			port->rx_queue_stats_mapping_enabled = 0;
			printf("RX queue stats mapping not supported port id=%d\n", pi);
		}
		else
			rte_exit(EXIT_FAILURE,
					"set_rx_queue_stats_mapping_registers "
					"failed for port id=%d diag=%d\n",
					pi, diag);
	}
}

void fwd_stats_display_neat(void)
{
	struct {
		struct fwd_stream *rx_stream;
		struct fwd_stream *tx_stream;
		uint64_t tx_dropped;
		uint64_t tx_packets;
		uint64_t rx_bad_ip_csum;
		uint64_t rx_bad_l4_csum;
		uint64_t rx_bad_outer_l4_csum;
	} ports_stats[RTE_MAX_ETHPORTS];

	uint64_t total_rx_dropped = 0;
	uint64_t total_tx_dropped = 0;
	uint64_t total_rx_nombuf = 0;

	struct rte_eth_stats stats;
#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	uint64_t fwd_cycles = 0;
#endif
	uint64_t total_recv = 0;
	uint64_t total_xmit = 0;
	struct rte_port *port;
	portid_t pt_id;

	memset(ports_stats, 0, sizeof(ports_stats));

    uint64_t curr_tsc = rte_get_tsc_cycles();
    uint64_t hz = rte_get_tsc_hz();
    uint64_t elapsed = (curr_tsc - start_tsc_stats) - hz * (MIN_TX_AFTER_DELAY) / 1000;

    struct fwd_stream *fs = &fwd_streams;

    ports_stats[fs->tx_port].tx_stream = fs;
    ports_stats[fs->rx_port].rx_stream = fs;

    ports_stats[fs->tx_port].tx_dropped += fs->fwd_dropped;
    ports_stats[fs->tx_port].tx_packets += fs->tx_packets;

    ports_stats[fs->rx_port].rx_bad_ip_csum += fs->rx_bad_ip_csum;
    ports_stats[fs->rx_port].rx_bad_l4_csum += fs->rx_bad_l4_csum;
    ports_stats[fs->rx_port].rx_bad_outer_l4_csum +=
            fs->rx_bad_outer_l4_csum;

    RTE_ETH_FOREACH_DEV(pt_id) {
		if ((enabled_port_mask & (1 << pt_id)) == 0)
			continue;
        uint8_t j;

		port = &ports[pt_id];

		rte_eth_stats_get(pt_id, &stats);
		stats.ipackets -= port->stats.ipackets;
		stats.opackets -= port->stats.opackets;
		stats.ibytes -= port->stats.ibytes;
		stats.obytes -= port->stats.obytes;
		stats.imissed -= port->stats.imissed;
		stats.oerrors -= port->stats.oerrors;
		stats.rx_nombuf -= port->stats.rx_nombuf;

		total_recv += stats.ipackets;
		total_xmit += stats.opackets;
		total_rx_dropped += stats.imissed;
		total_tx_dropped += ports_stats[pt_id].tx_dropped;
		total_tx_dropped += stats.oerrors;
		total_rx_nombuf  += stats.rx_nombuf;

		printf("port %d: tx-pps: %-14.2f\n",
				pt_id,
				1.0 * ports_stats[pt_id].tx_packets / elapsed * hz);
    }

	printf("all: tx-pps: %-14.2f\n", 1.0 * total_xmit / elapsed * hz);
}

void
fwd_stats_display(void)
{
	static const char *fwd_stats_border = "----------------------";
	static const char *acc_stats_border = "+++++++++++++++";
	struct {
		struct fwd_stream *rx_stream;
		struct fwd_stream *tx_stream;
		uint64_t tx_dropped;
		uint64_t tx_packets;
		uint64_t rx_bad_ip_csum;
		uint64_t rx_bad_l4_csum;
		uint64_t rx_bad_outer_l4_csum;
	} ports_stats[RTE_MAX_ETHPORTS];

	uint64_t total_rx_dropped = 0;
	uint64_t total_tx_dropped = 0;
	uint64_t total_rx_nombuf = 0;

	struct rte_eth_stats stats;
#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	uint64_t fwd_cycles = 0;
#endif
	uint64_t total_recv = 0;
	uint64_t total_xmit = 0;
	struct rte_port *port;
	portid_t pt_id;

	memset(ports_stats, 0, sizeof(ports_stats));

    uint64_t curr_tsc = rte_get_tsc_cycles();
    uint64_t hz = rte_get_tsc_hz();
    uint64_t elapsed = (curr_tsc - start_tsc_stats) - hz * (MIN_TX_AFTER_DELAY) / 1000;

    struct fwd_stream *fs = &fwd_streams;

    ports_stats[fs->tx_port].tx_stream = fs;
    ports_stats[fs->rx_port].rx_stream = fs;

    ports_stats[fs->tx_port].tx_dropped += fs->fwd_dropped;
    ports_stats[fs->tx_port].tx_packets += fs->tx_packets;

    ports_stats[fs->rx_port].rx_bad_ip_csum += fs->rx_bad_ip_csum;
    ports_stats[fs->rx_port].rx_bad_l4_csum += fs->rx_bad_l4_csum;
    ports_stats[fs->rx_port].rx_bad_outer_l4_csum +=
            fs->rx_bad_outer_l4_csum;

#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
    fwd_cycles += fs->core_cycles;
#endif
    
    RTE_ETH_FOREACH_DEV(pt_id) {
		if ((enabled_port_mask & (1 << pt_id)) == 0)
			continue;
        uint8_t j;

		port = &ports[pt_id];

		rte_eth_stats_get(pt_id, &stats);
		stats.ipackets -= port->stats.ipackets;
		stats.opackets -= port->stats.opackets;
		stats.ibytes -= port->stats.ibytes;
		stats.obytes -= port->stats.obytes;
		stats.imissed -= port->stats.imissed;
		stats.oerrors -= port->stats.oerrors;
		stats.rx_nombuf -= port->stats.rx_nombuf;

		total_recv += stats.ipackets;
		total_xmit += stats.opackets;
		total_rx_dropped += stats.imissed;
		total_tx_dropped += ports_stats[pt_id].tx_dropped;
		total_tx_dropped += stats.oerrors;
		total_rx_nombuf  += stats.rx_nombuf;

		printf("\n  %s Forward statistics for port %-2d %s\n",
		       fwd_stats_border, pt_id, fwd_stats_border);

		if (!port->rx_queue_stats_mapping_enabled &&
		    !port->tx_queue_stats_mapping_enabled) {
			printf("  RX-packets: %-14"PRIu64
			       " RX-dropped: %-14"PRIu64
			       "RX-total: %-"PRIu64"\n",
			       stats.ipackets, stats.imissed,
			       stats.ipackets + stats.imissed);

			// if (cur_fwd_eng == &csum_fwd_engine)
			// 	printf("  Bad-ipcsum: %-14"PRIu64
			// 	       " Bad-l4csum: %-14"PRIu64
			// 	       "Bad-outer-l4csum: %-14"PRIu64"\n",
			// 	       ports_stats[pt_id].rx_bad_ip_csum,
			// 	       ports_stats[pt_id].rx_bad_l4_csum,
			// 	       ports_stats[pt_id].rx_bad_outer_l4_csum);
			if (stats.ierrors + stats.rx_nombuf > 0) {
				printf("  RX-error: %-"PRIu64"\n",
				       stats.ierrors);
				printf("  RX-nombufs: %-14"PRIu64"\n",
				       stats.rx_nombuf);
			}

			printf("  TX-packets: %-14"PRIu64
			       " TX-dropped: %-14"PRIu64
			       "TX-total: %-"PRIu64"\n",
			       ports_stats[pt_id].tx_packets, ports_stats[pt_id].tx_dropped,
			       ports_stats[pt_id].tx_packets + ports_stats[pt_id].tx_dropped);
            printf("  RX-PPS: %-19.2f"
                   "TX-PPS: %-14.2f""\n",
                   1.0 * stats.ipackets / elapsed * hz,
                   1.0 * ports_stats[pt_id].tx_packets / elapsed * hz
                   );
		} else {
			printf("  RX-packets:             %14"PRIu64
			       "    RX-dropped:%14"PRIu64
			       "    RX-total:%14"PRIu64"\n",
			       stats.ipackets, stats.imissed,
			       stats.ipackets + stats.imissed);

			// if (cur_fwd_eng == &csum_fwd_engine)
			// 	printf("  Bad-ipcsum:%14"PRIu64
			// 	       "    Bad-l4csum:%14"PRIu64
			// 	       "    Bad-outer-l4csum: %-14"PRIu64"\n",
			// 	       ports_stats[pt_id].rx_bad_ip_csum,
			// 	       ports_stats[pt_id].rx_bad_l4_csum,
			// 	       ports_stats[pt_id].rx_bad_outer_l4_csum);
			if ((stats.ierrors + stats.rx_nombuf) > 0) {
				printf("  RX-error:%"PRIu64"\n", stats.ierrors);
				printf("  RX-nombufs:             %14"PRIu64"\n",
				       stats.rx_nombuf);
			}

			printf("  TX-packets:             %14"PRIu64
			       "    TX-dropped:%14"PRIu64
			       "    TX-total:%14"PRIu64"\n",
			       stats.opackets, ports_stats[pt_id].tx_dropped,
			       stats.opackets + ports_stats[pt_id].tx_dropped);
            printf("  RX-PPS: %14.2f"
                   "    TX-PPS: %14.2f""\n",
                   1.0 * stats.ipackets / elapsed * hz,
                   1.0 * stats.opackets / elapsed * hz
                   );
    }

#ifdef RTE_TEST_PMD_RECORD_BURST_STATS
		if (ports_stats[pt_id].rx_stream)
			pkt_burst_stats_display("RX",
				&ports_stats[pt_id].rx_stream->rx_burst_stats);
		if (ports_stats[pt_id].tx_stream)
			pkt_burst_stats_display("TX",
				&ports_stats[pt_id].tx_stream->tx_burst_stats);
#endif

		if (port->rx_queue_stats_mapping_enabled) {
			printf("\n");
			for (j = 0; j < RTE_ETHDEV_QUEUE_STAT_CNTRS; j++) {
				printf("  Stats reg %2d RX-packets:%14"PRIu64
				       "     RX-errors:%14"PRIu64
				       "    RX-bytes:%14"PRIu64"\n",
				       j, stats.q_ipackets[j],
				       stats.q_errors[j], stats.q_ibytes[j]);
			}
			printf("\n");
		}
		if (port->tx_queue_stats_mapping_enabled) {
			for (j = 0; j < RTE_ETHDEV_QUEUE_STAT_CNTRS; j++) {
				printf("  Stats reg %2d TX-packets:%14"PRIu64
				       "                                 TX-bytes:%14"
				       PRIu64"\n",
				       j, stats.q_opackets[j],
				       stats.q_obytes[j]);
			}
		}

		printf("  %s--------------------------------%s\n",
		       fwd_stats_border, fwd_stats_border);
	}

	printf("\n  %s Accumulated forward statistics for all ports"
	       "%s\n",
	       acc_stats_border, acc_stats_border);
	printf("  RX-packets: %-14"PRIu64" RX-dropped: %-14"PRIu64"RX-total: "
	       "%-"PRIu64"\n"
	       "  TX-packets: %-14"PRIu64" TX-dropped: %-14"PRIu64"TX-total: "
	       "%-"PRIu64"\n",
	       total_recv, total_rx_dropped, total_recv + total_rx_dropped,
	       total_xmit, total_tx_dropped, total_xmit + total_tx_dropped);
	if (total_rx_nombuf > 0)
		printf("  RX-nombufs: %-14"PRIu64"\n", total_rx_nombuf);
	printf("  %s++++++++++++++++++++++++++++++++++++++++++++++"
	       "%s\n",
	       acc_stats_border, acc_stats_border);
#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	if (total_recv > 0)
		printf("\n  CPU cycles/packet=%u (total cycles="
		       "%"PRIu64" / total RX packets=%"PRIu64")\n",
		       (unsigned int)(fwd_cycles / total_recv),
		       fwd_cycles, total_recv);
#endif
}

void
fwd_stats_reset(void)
{
	portid_t portid;

    RTE_ETH_FOREACH_DEV(portid) {
        rte_eth_stats_get(portid, &ports[portid].stats);
    }
    start_tsc_stats = rte_get_tsc_cycles();

	// for (sm_id = 0; sm_id < cur_fwd_config.nb_fwd_streams; sm_id++) {
    struct fwd_stream *fs = &fwd_streams;
    fs->rx_packets = 0;
    fs->tx_packets = 0;
    fs->fwd_dropped = 0;
    fs->rx_bad_ip_csum = 0;
    fs->rx_bad_l4_csum = 0;
    fs->rx_bad_outer_l4_csum = 0;

#ifdef RTE_TEST_PMD_RECORD_BURST_STATS
		memset(&fs->rx_burst_stats, 0, sizeof(fs->rx_burst_stats));
		memset(&fs->tx_burst_stats, 0, sizeof(fs->tx_burst_stats));
#endif
#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
		fs->core_cycles = 0;
#endif
}

void
latency_stats_display(void)
{
	printf("to be implemented.");
}