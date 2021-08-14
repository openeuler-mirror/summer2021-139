#ifndef __CONFIG_H_
#define __CONFIG_H_

#include <stdbool.h>

#include <rte_pci.h>
#include <rte_bus_pci.h>
#include <rte_gro.h>
#include <rte_gso.h>
#include <cmdline.h>

#define TX_DEF_PACKET_LEN 64
/*
 * The maximum number of segments per packet is used when creating
 * scattered transmit packets composed of a list of mbufs.
 */
#define RTE_MAX_SEGS_PER_PKT 255 /**< nb_segs is a 8-bit unsigned char. */
/*
 * Default size of the mbuf data buffer to receive standard 1518-byte
 * Ethernet frames in a mono-segment memory buffer.
 */
#define DEFAULT_MBUF_DATA_SIZE RTE_MBUF_DEFAULT_BUF_SIZE
/**< Default size of mbuf data buffer. */

#define MAX_PKT_BURST 512
#define DEF_PKT_BURST 32
#define BURST_TX_WAIT_US 1
#define BURST_TX_RETRIES 64
#define MIN_TX_AFTER_DELAY 1000


typedef uint8_t  lcoreid_t;
typedef uint16_t portid_t;
typedef uint16_t queueid_t;
typedef uint16_t streamid_t;

/**
 * The data structure associated with each port.
 */
struct rte_port {
	struct rte_eth_dev_info dev_info;   /**< PCI info + driver name */
	struct rte_eth_conf     dev_conf;   /**< Port configuration. */
	struct rte_ether_addr       eth_addr;   /**< Port ethernet address */
	struct rte_eth_stats    stats;      /**< Last port statistics */
	unsigned int            socket_id;  /**< For NUMA support */
	uint16_t		parse_tunnel:1; /**< Parse internal headers */
	uint16_t                tso_segsz;  /**< Segmentation offload MSS for non-tunneled packets. */
	uint16_t                tunnel_tso_segsz; /**< Segmentation offload MSS for tunneled pkts. */
	uint16_t                tx_vlan_id;/**< The tag ID */
	uint16_t                tx_vlan_id_outer;/**< The outer tag ID */
	uint8_t                 tx_queue_stats_mapping_enabled;
	uint8_t                 rx_queue_stats_mapping_enabled;
	volatile uint16_t        port_status;    /**< port started or not */
	uint8_t                 need_setup;     /**< port just attached */
	uint8_t                 need_reconfig;  /**< need reconfiguring port or not */
	uint8_t                 need_reconfig_queues; /**< need reconfiguring queues or not */
	uint8_t                 rss_flag;   /**< enable rss or not */
	uint8_t                 dcb_flag;   /**< enable dcb */
	uint16_t                nb_rx_desc[RTE_MAX_QUEUES_PER_PORT+1]; /**< per queue rx desc number */
	uint16_t                nb_tx_desc[RTE_MAX_QUEUES_PER_PORT+1]; /**< per queue tx desc number */
	struct rte_eth_rxconf   rx_conf[RTE_MAX_QUEUES_PER_PORT+1]; /**< per queue rx configuration */
	struct rte_eth_txconf   tx_conf[RTE_MAX_QUEUES_PER_PORT+1]; /**< per queue tx configuration */
	struct rte_ether_addr   *mc_addr_pool; /**< pool of multicast addrs */
	uint32_t                mc_addr_nb; /**< nb. of addr. in mc_addr_pool */
	uint8_t                 slave_flag; /**< bonding slave port */
	struct port_flow        *flow_list; /**< Associated flows. */
	const struct rte_eth_rxtx_callback *rx_dump_cb[RTE_MAX_QUEUES_PER_PORT+1];
	const struct rte_eth_rxtx_callback *tx_dump_cb[RTE_MAX_QUEUES_PER_PORT+1];
#ifdef SOFTNIC
	struct softnic_port     softport;  /**< softnic params */
#endif
	/**< metadata value to insert in Tx packets. */
	uint32_t		tx_metadata;
	const struct rte_eth_rxtx_callback *tx_set_md_cb[RTE_MAX_QUEUES_PER_PORT+1];
};

struct fwd_stream {
	/* "read-only" data */
	portid_t   rx_port;   /**< port to poll for received packets */
	queueid_t  rx_queue;  /**< RX queue to poll on "rx_port" */
	portid_t   tx_port;   /**< forwarding port of received packets */
	queueid_t  tx_queue;  /**< TX queue to send forwarded packets */
	streamid_t peer_addr; /**< index of peer ethernet address of packets */

	unsigned int retry_enabled;

	/* "read-write" results */
	uint64_t rx_packets;  /**< received packets */
	uint64_t tx_packets;  /**< received packets transmitted */
	uint64_t fwd_dropped; /**< received packets not forwarded */
	uint64_t rx_bad_ip_csum ; /**< received packets has bad ip checksum */
	uint64_t rx_bad_l4_csum ; /**< received packets has bad l4 checksum */
	uint64_t rx_bad_outer_l4_csum;
	/**< received packets has bad outer l4 checksum */
	unsigned int gro_times;	/**< GRO operation times */
#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	uint64_t     core_cycles; /**< used for RX and TX processing */
#endif
#ifdef RTE_TEST_PMD_RECORD_BURST_STATS
	struct pkt_burst_stats rx_burst_stats;
	struct pkt_burst_stats tx_burst_stats;
#endif
	int done;
};

/**
 * The data structure associated with each forwarding logical core.
 * The logical cores are internally numbered by a core index from 0 to
 * the maximum number of logical cores - 1.
 * The system CPU identifier of all logical cores are setup in a global
 * CPU id. configuration table.
 */
struct fwd_lcore {
	struct rte_gso_ctx gso_ctx;     /**< GSO context */
	struct rte_mempool *mbp; /**< The mbuf pool to use by this core */
	void *gro_ctx;		/**< GRO context */
	streamid_t stream_idx;   /**< index of 1st stream in "fwd_streams" */
	streamid_t stream_nb;    /**< number of streams in "fwd_streams" */
	lcoreid_t  cpuid_idx;    /**< index of logical core in CPU id table */
	queueid_t  tx_queue;     /**< TX queue to send forwarded packets */
	volatile char stopped;   /**< stop forwarding when set */
};

/* for sharing variables */
/*
 * Configuration of packet segments used by the "txonly" processing engine.
 */
#define TXONLY_DEF_PACKET_LEN 64
extern uint16_t tx_pkt_length; /**< Length of TXONLY packet */
extern uint16_t tx_pkt_seg_lengths[RTE_MAX_SEGS_PER_PKT]; /**< Seg. lengths */
extern uint8_t  tx_pkt_nb_segs; /**< Number of segments in TX packets */
extern uint16_t nb_pkt_per_burst;
extern uint32_t burst_tx_retry_num;  /**< Burst tx retry number for mac-retry. */
extern uint32_t burst_tx_delay_time; /**< Burst tx delay time(us) for mac-retry. */

extern struct rte_port ports[RTE_MAX_ETHPORTS];	       /**< For all probed ethernet ports. */
// extern portid_t nb_peer_eth_addrs; /**< Number of peer ethernet addresses. */
extern struct rte_ether_addr peer_eth_addrs[RTE_MAX_ETHPORTS];
extern struct fwd_lcore  fwd_lcores;
// extern unsigned int fwd_lcores_cpuids[RTE_MAX_LCORE];

extern struct fwd_stream fwd_streams;
extern char *file_to_transmit;

typedef void (*fwd_callback_t)(void *);
typedef void (*packet_fwd_t)(struct fwd_stream *fs);

enum tx_pkt_split {
	TX_PKT_SPLIT_OFF,
	TX_PKT_SPLIT_ON,
	TX_PKT_SPLIT_RND,
};
extern enum tx_pkt_split tx_pkt_split;

struct fwd_engine {
	const char          *fwd_mode_name; /**< Forwarding mode name. */
	fwd_callback_t       port_fwd_begin; /**< NULL if nothing special to do. */
	fwd_callback_t       port_fwd_end;   /**< NULL if nothing special to do. */
	packet_fwd_t       	 packet_fwd;     /**< Mandatory. */
};

extern struct fwd_engine tx_engine;
extern struct fwd_engine rx_engine;
extern struct fwd_engine latency_engine;
extern struct fwd_engine icmp_echo_engine;

struct fwd_config {
	struct fwd_engine	*fwd_eng;
	int					 nb_fwd_streams; /* only one in our case*/
	int 				 nb_fwd_lcores; /* only one in our case*/
	int 				 nb_fwd_ports; /* only one in our case*/
} global_config;

static inline struct fwd_lcore *
current_fwd_lcore(void)
{
	return &fwd_lcores;
}

/*
 * Work-around of a compilation error with ICC on invocations of the
 * rte_be_to_cpu_16() function.
 */
#ifdef __GCC__
#define RTE_BE_TO_CPU_16(be_16_v)  rte_be_to_cpu_16((be_16_v))
#define RTE_CPU_TO_BE_16(cpu_16_v) rte_cpu_to_be_16((cpu_16_v))
#else
#if RTE_BYTE_ORDER == RTE_BIG_ENDIAN
#define RTE_BE_TO_CPU_16(be_16_v)  (be_16_v)
#define RTE_CPU_TO_BE_16(cpu_16_v) (cpu_16_v)
#else
#define RTE_BE_TO_CPU_16(be_16_v) \
	(uint16_t) ((((be_16_v) & 0xFF) << 8) | ((be_16_v) >> 8))
#define RTE_CPU_TO_BE_16(cpu_16_v) \
	(uint16_t) ((((cpu_16_v) & 0xFF) << 8) | ((cpu_16_v) >> 8))
#endif
#endif /* __GCC__ */

#endif /* __CONFIG_H_ */