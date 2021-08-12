/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include "config.h"
#include "stats.h"

static volatile bool force_quit;

/* MAC updating enabled by default */

#define RTE_LOGTYPE_L2FWD RTE_LOGTYPE_USER1

#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */
#define MEMPOOL_CACHE_SIZE 256

/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 8192 /** number of rx descriptors */
#define RTE_TEST_TX_DESC_DEFAULT 8192 /** number of tx descriptors */
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* mask of enabled ports, not used */
static uint32_t l2fwd_enabled_port_mask = 0;

/*
 * Ethernet device configuration.
 */
struct rte_eth_rxmode rx_mode = {
	.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
		/**< Default maximum frame length. */
};

struct rte_eth_txmode tx_mode = {
	.offloads = DEV_TX_OFFLOAD_MBUF_FAST_FREE,
};

/*
 * Probed Target Environment.
 */
struct fwd_lcore fwd_lcores; /**< For all probed logical cores. */
// lcoreid_t nb_lcores;      /**< Number of probed logical cores. it's 1 in our case*/

uint16_t nb_pkt_per_burst = DEF_PKT_BURST;
uint32_t burst_tx_retry_num = BURST_TX_RETRIES;  /**< Burst tx retry number for mac-retry. */
uint32_t burst_tx_delay_time = BURST_TX_WAIT_US; /**< Burst tx delay time(us) for mac-retry. */

enum tx_pkt_split tx_pkt_split = TX_PKT_SPLIT_OFF; /** we don't want to split packet */
// unsigned int fwd_lcores_cpuids[RTE_MAX_LCORE]; /** we only use the first lcore */

/*
 * Configuration of packet segments used by the "txonly" processing engine.
 */
uint16_t tx_pkt_length = TXONLY_DEF_PACKET_LEN; /**< TXONLY packet length. */
uint16_t tx_pkt_seg_lengths[RTE_MAX_SEGS_PER_PKT] = {
	TXONLY_DEF_PACKET_LEN,
};
/**< Number of segments in TXONLY packets, only 1 in our case to keep it simple */
uint8_t  tx_pkt_nb_segs = 1;


/*
 * Record the Ethernet address of peer target ports to which packets are
 * forwarded.
 * Must be instantiated with the ethernet addresses of peer traffic generator
 * ports.
 */

/* tables */
struct rte_port ports[RTE_MAX_ETHPORTS];	       /**< For all probed ethernet ports. */
// portid_t nb_ports;             /**< Number of probed ethernet ports. */

portid_t ports_ids[RTE_MAX_ETHPORTS];		/* store port ids */
struct rte_ether_addr peer_eth_addrs[RTE_MAX_ETHPORTS];
struct fwd_stream fwd_streams;		/* we only have one stream */
// portid_t nb_peer_eth_addrs = 0;
char *peer_addr_str;
char *file_to_transmit = NULL;

/* Per-port statistics struct */
struct l2fwd_port_statistics {
	uint64_t tx;
	uint64_t rx;
	uint64_t dropped;
} __rte_cache_aligned;
struct l2fwd_port_statistics port_statistics[RTE_MAX_ETHPORTS];

/**	TODO:
	1. do we need header split?
	2. do we need to use hw CRC support?
*/
static void
signal_handler(int signum) {
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		fwd_lcores.stopped = 1;
		force_quit = true;
		// signal(signum, SIG_DFL);
		// kill(getpid(), signum);
	}
}


static int
start_remote_callback(void *arg) {
	struct fwd_lcore *fc = (struct fwd_lcore *)arg;
	packet_fwd_t packet_fwd = global_config.fwd_eng->packet_fwd;
	do {
		(*packet_fwd)(&fwd_streams);
		if (fwd_streams.done) {
			break;
		}
	} while (!fc->stopped);
	return 0;
}

static void
launch_pkt_fwd() {
	/* initialize */
	fwd_callback_t fwd_begin = global_config.fwd_eng->port_fwd_begin;
	fwd_callback_t fwd_end = global_config.fwd_eng->port_fwd_end;
	int error;
	if (fwd_begin) {
		/* for each port */
		(*fwd_begin)(&fwd_streams);
	}

	/* for now we only have one lcore, call it on master */
	// for (i = 0; i < cur_fwd_config.nb_fwd_lcores; i++) {
		/* calling run_pkt_fwd_on_lcore */

	// error = rte_eal_remote_launch(start_remote_callback,
	// 		(void *) &fwd_lcores, fwd_lcores.cpuid_idx);

	fwd_stats_reset();
	error = start_remote_callback((void *) &fwd_lcores);
	rte_delay_ms(MIN_TX_AFTER_DELAY); /* allow NIC to consume all due packets */
	fwd_stats_display();

	if (fwd_end) {
		(*fwd_end)(NULL);
	}
	if (error < 0) {
		rte_exit(EXIT_FAILURE, "core busy\n");
	}
}

/* display usage */
static void
usage(const char *prgname)
{
	printf("./tx -l 0 -n number_of_memory_channels [EAL parameters] -- [tx parameters]\n"
			"tx parameters: (--peer peer_mac_address) \n"
			"	(-l length) the length of each packet(burstlet)\n"
			"	(--tx-mode) for testing only. send 0 filled packets\n"
			"	(--rx-mode) for testing only. poll and receive packets \n"
			"	(--file-name filename) the path to file to send\n"
			);
	// printf("invalid param\n");
}

static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

/* Parse the argument given in the command line of the application */
static int
tx_parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];

	argvopt = argv;

	global_config.fwd_eng = &tx_engine;
	global_config.nb_fwd_lcores = 1;
	global_config.nb_fwd_streams = 1;
	global_config.nb_fwd_ports = 1;

	static struct option long_options[] = {
			{"peer", required_argument, NULL, 1},
			// {"src-ip", required_argument, NULL, 2},
			// {"dst-ip", required_argument, NULL, 3},
			{"pkt-len", required_argument, NULL, 'l'},
			{"rx-mode", no_argument, NULL, 'r'},
			{"tx-mode", no_argument, NULL, 't'},
			{"file-name", required_argument, NULL, 'f'},
			{0, 0, 0, 0}
    };

	while ((opt = getopt_long(argc, argvopt, "l:f:p:",
				  long_options, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			l2fwd_enabled_port_mask = parse_portmask(optarg);
			if (l2fwd_enabled_port_mask == 0) {
				printf("invalid portmask\n");
				usage(prgname);
				return -1;
			}
			break;

		// /* nqueue */
		// case 'q':
		// 	/* TODO: parse nqueue here */
		// 	break;
		/* long options */
		case 'l':
			tx_pkt_length = atoi(optarg);
			/* we don't split */
			tx_pkt_seg_lengths[0] = tx_pkt_length;
			break;
		case 'r':
			global_config.fwd_eng = &rx_engine;
			break;
		case 't':
			global_config.fwd_eng = &tx_engine;
			break;
		case 'f':
			file_to_transmit = optarg;
			break;
			
		case 1:
			peer_addr_str = optarg;
			break;

		default:
			usage(prgname);
			return -1;
		}
	}

	if ((global_config.fwd_eng == &tx_engine
			|| global_config.fwd_eng == &tx_engine)
			&& peer_addr_str == NULL) {
		usage(prgname);
		return -1;
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 1; /* reset getopt lib */
	return ret;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint16_t portid;
	uint8_t count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;
	int ret;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		all_ports_up = 1;
		RTE_ETH_FOREACH_DEV(portid) {
			if (force_quit)
				return;
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			ret = rte_eth_link_get_nowait(portid, &link);
			if (ret < 0) {
				all_ports_up = 0;
				if (print_flag == 1)
					printf("Port %u link get failed: %s\n",
						portid, rte_strerror(-ret));
				continue;
			}
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf(
					"Port%d Link Up. Speed %u Mbps - %s\n",
						portid, link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n", portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}

// /* TODO: implement this to use multiple lcore */
// static void
// init_fwd_lcore_config(void)
// {	
// 	unsigned int i;
// 	unsigned int nb_lc = 0;
// 	// unsigned int sock_num;

// 	for (i = 0; i < RTE_MAX_LCORE; i++) {
// 		if (!rte_lcore_is_enabled(i))
// 			continue;
// 		// sock_num = rte_lcore_to_socket_id(i);
// 		if (i == rte_get_master_lcore())
// 			continue;
// 		fwd_lcores_cpuids[nb_lc++] = i;
// 	}
// 	nb_lcores = (lcoreid_t) nb_lc;
// }

static int
parse_ether_addr_str(const char *addr, struct rte_ether_addr *eth_addr)
{
	char addrbuf[18] = {0}, b[3] = {0};
	size_t len = strlen(addr);
	if (len != 17) {
		return -1;
	}
	strncpy(addrbuf, addr, 17);
	addrbuf[17] = 0;

	int c = 0, byte = -0, newbyte = 0;
	while (c != len) {
		if (addrbuf[c] == ':') {
			if (c - newbyte != 2) {
				return -1;
			}
			strncpy(b, addrbuf + newbyte, 2);
			sscanf(b, "%hhx", &eth_addr->addr_bytes[byte++]);
			if (c + 1 < len) {
				newbyte = c + 1;
			}
		}
		c++;
	}
	strncpy(b, addrbuf + newbyte, 2);
	sscanf(b, "%hhx", &eth_addr->addr_bytes[byte++]);

	if (byte != 6) {
		return -1;
	}
	return 0;
}

static void
set_def_peer_eth_addrs(void)
{
	portid_t i;
	int error;

	if (peer_addr_str == NULL)
		return;
	for (i = 0; i < RTE_MAX_ETHPORTS; i++) {
		error = parse_ether_addr_str(peer_addr_str, &peer_eth_addrs[i]);
		if (error < 0) {
			rte_exit(EXIT_FAILURE, "Invalid peer MAC address\n");
		}
	}

	printf("Peer MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				peer_eth_addrs[0].addr_bytes[0],
				peer_eth_addrs[0].addr_bytes[1],
				peer_eth_addrs[0].addr_bytes[2],
				peer_eth_addrs[0].addr_bytes[3],
				peer_eth_addrs[0].addr_bytes[4],
				peer_eth_addrs[0].addr_bytes[5]
				);
}

/* TODO: implement this to use multiple ports */
// set_default_fwd_ports_config(void)

static bool
check_nb_ports()
{
	int count = 0;
	portid_t portid;
	RTE_ETH_FOREACH_DEV(portid) {
		ports_ids[count] = portid;
		count++;
	}
	if (count != 1) {
		return -1;
	}
	return 0;
}



static void
init_fwd_lcore(void)
{
	unsigned int i, nb_mbufs;
	fwd_lcores.stopped = 0;
	for (i = 0; i < RTE_MAX_LCORE; i++) {
		if (rte_lcore_is_enabled(i)) {
			fwd_lcores.cpuid_idx = i;
			break;
		}
	}

	nb_mbufs = RTE_MAX(1 * (nb_rxd + nb_txd + MAX_PKT_BURST +
		1 * MEMPOOL_CACHE_SIZE), 8192U);

	/* create the mbuf pool */
	fwd_lcores.mbp = rte_pktmbuf_pool_create("mbuf_pool", nb_mbufs,
		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());
	if (fwd_lcores.mbp == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");
}

static void
init_fwd_stream(portid_t portid) {
	fwd_streams.rx_port = portid;
	fwd_streams.rx_queue = 0;
	fwd_streams.tx_port = portid;
	fwd_streams.rx_queue = 0;
	fwd_streams.peer_addr = fwd_streams.tx_port;
	fwd_streams.retry_enabled = 1;
}

static void
init_port(portid_t portid) {
		struct rte_eth_dev_info *dev_info;
		struct rte_eth_conf *dev_conf;
		struct rte_eth_rxconf *rx_conf;
		struct rte_eth_txconf *tx_conf;
		struct rte_port *port;
		int ret;

		port = &ports[portid];
		dev_info = &(port->dev_info);
		dev_conf = &(port->dev_conf);
		rx_conf = &port->rx_conf[0];
		tx_conf = &port->tx_conf[0];

		/* TODO: we only have one queue per port */
		port->nb_rx_desc[0] = RTE_TEST_RX_DESC_DEFAULT;
		port->nb_tx_desc[0] = RTE_TEST_TX_DESC_DEFAULT;
		port->socket_id = rte_eth_dev_socket_id(portid);
		dev_conf->rx_adv_conf.rss_conf.rss_key = NULL;
		dev_conf->rx_adv_conf.rss_conf.rss_hf = 0;
		dev_conf->rxmode.mq_mode = ETH_MQ_RX_NONE;


		ret = rte_eth_dev_info_get(portid, dev_info);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				"Error during getting device (port %u) info: %s\n",
				portid, strerror(-ret));

		if (dev_info->tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
			dev_conf->txmode.offloads |=
				DEV_TX_OFFLOAD_MBUF_FAST_FREE;
		/* port id to configure, number of rx queue for that device, 
			number of tx queue for that device, andd config structure */
		ret = rte_eth_dev_configure(portid, 1, 1, dev_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
				  ret, portid);

		/* Check that numbers of Rx and Tx descriptors satisfy 
		descriptors limits from the ethernet device information,
		otherwise adjust them to boundaries. */
		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &port->nb_rx_desc[0],
						       &port->nb_tx_desc[0]);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, portid);

		ret = rte_eth_macaddr_get(portid, &port->eth_addr);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot get MAC address: err=%d, port=%u\n",
				 ret, portid);

		/* init one RX queue */
		fflush(stdout);
		*rx_conf = dev_info->default_rxconf;
		rx_conf->offloads = dev_conf->rxmode.offloads;
		/* port, queue id, number of descriptor, socket id, config,
			from which to allocate*/
		ret = rte_eth_rx_queue_setup(portid, 0, port->nb_rx_desc[0],
					     port->socket_id,
					     rx_conf,
					     fwd_lcores.mbp);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
				  ret, portid);

		/* init one TX queue on each port */
		fflush(stdout);
		*tx_conf = dev_info->default_txconf;
		tx_conf->offloads = dev_conf->txmode.offloads;
		ret = rte_eth_tx_queue_setup(portid, 0, port->nb_tx_desc[0],
				port->socket_id,
				tx_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
				ret, portid);

		/* XXX what is this */
		ret = rte_eth_dev_set_ptypes(portid, RTE_PTYPE_UNKNOWN, NULL,
					     0);
		if (ret < 0)
			printf("Port %u, Failed to disable Ptype parsing\n",
					portid);
		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				  ret, portid);

		ret = rte_eth_promiscuous_enable(portid);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				 "rte_eth_promiscuous_enable:err=%s, port=%u\n",
				 rte_strerror(-ret), portid);

		printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				portid,
				port->eth_addr.addr_bytes[0],
				port->eth_addr.addr_bytes[1],
				port->eth_addr.addr_bytes[2],
				port->eth_addr.addr_bytes[3],
				port->eth_addr.addr_bytes[4],
				port->eth_addr.addr_bytes[5]);

		map_port_queue_stats_mapping_registers(portid, port);
}

int
main(int argc, char **argv)
{
	int ret;
	uint16_t nb_ports;
	uint16_t nb_ports_available = 0;
	uint16_t portid;
	int do_mlockall = 0;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
	argv += ret;

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* on FreeBSD, mlockall() is disabled by default */
#ifdef RTE_EXEC_ENV_FREEBSD
	do_mlockall = 0;
#else
	do_mlockall = 1;
#endif


	/* parse application arguments (after the EAL ones) */
	ret = tx_parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid arguments\n");

	if (do_mlockall && mlockall(MCL_CURRENT | MCL_FUTURE)) {
		perror("mlockall failed");
	}

	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

	/* check port mask to possible port mask */
	if (l2fwd_enabled_port_mask & ~((1 << nb_ports) - 1))
		rte_exit(EXIT_FAILURE, "Invalid portmask; possible (0x%x)\n",
			(1 << nb_ports) - 1);

	init_fwd_lcore();
	check_nb_ports();
	set_def_peer_eth_addrs();

	/* Initialise each port */
	RTE_ETH_FOREACH_DEV(portid) {
		/* skip port that's not enabled */
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0)
			continue;

		nb_ports_available++;

		/* init port */
		printf("Initializing port %u... ", portid);
		fflush(stdout);
		init_port(portid);

		init_fwd_stream(portid);
		printf("done: \n");
		
		// /* Initialize TX buffers */ we don't need buffers
		// tx_buffer[portid] = rte_zmalloc_socket("tx_buffer",
		// 		RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST), 0,
		// 		rte_eth_dev_socket_id(portid));
		// if (tx_buffer[portid] == NULL)
		// 	rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
		// 			portid);

		// rte_eth_tx_buffer_init(tx_buffer[portid], MAX_PKT_BURST);

		/* Register a specific callback to be called when an attempt is
			made to send all packets buffered on an ethernet port, but 
			not all packets can successfully be sent */
		// ret = rte_eth_tx_buffer_set_err_callback(tx_buffer[portid],
		// 		rte_eth_tx_buffer_count_callback,
		// 		&port_statistics[portid].dropped);
		// if (ret < 0)
		// 	rte_exit(EXIT_FAILURE,
		// 	"Cannot set error callback for tx buffer on port %u\n",
		// 		 portid);
	}

	if (!nb_ports_available) {
		rte_exit(EXIT_FAILURE,
			"All available ports are disabled. Please set portmask.\n");
	}

	check_all_ports_link_status(l2fwd_enabled_port_mask);

	ret = 0;

	/* we run on main, no need to wait */
	launch_pkt_fwd();

	RTE_ETH_FOREACH_DEV(portid) {
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0)
			continue;
		printf("Closing port %d...", portid);
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf(" Done\n");
	}
	if (fwd_lcores.mbp != NULL) {
		rte_mempool_free(fwd_lcores.mbp);
	}
	printf("Bye...\n");

	return ret;
}
