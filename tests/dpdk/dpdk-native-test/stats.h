#ifndef __STATS_H
#define __STATS_H

#define MAX_TX_QUEUE_STATS_MAPPINGS 1024 /* MAX_PORT of 32 @ 32 tx_queues/port */
#define MAX_RX_QUEUE_STATS_MAPPINGS 4096 /* MAX_PORT of 32 @ 128 rx_queues/port */

struct queue_stats_mappings {
	portid_t port_id;
	uint16_t queue_id;
	uint8_t stats_counter_id;
} __rte_cache_aligned;

void fwd_stats_display(void);
void fwd_stats_reset(void);

void map_port_queue_stats_mapping_registers(portid_t pi, struct rte_port *port);

#endif /* __STATS_H */