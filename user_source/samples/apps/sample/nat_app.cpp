#include <cstdlib>

#include "dist.hh"
#include "flow_key.hh"
#include "ip_key.hh"
#include "stub.nat_entry.hh"
#include "stub.port_pool.hh"

extern SwMap<FlowKey, NatEntry> g_nat_entry_map;

// NOTE: IPKey is just a placeholder. PortPool is a singleton object.
extern SwMap<IPKey, PortPool> g_port_pool_map;
static const uint32_t CLUSTER_ID = 0;

static void update_header(ipv4_hdr *iph, tcp_hdr *tcph,
                          const NatEntry::Data &data) {
  if (data.is_forward) {
    iph->src_addr = htonl(data.new_ip);
    tcph->src_port = htons(data.new_port);
  } else {
    iph->dst_addr = htonl(data.new_ip);
    tcph->dst_port = htons(data.new_port);
  }
}

// returns 0 if failed
static uint16_t find_free_port(PortBitmap &port_used) {
  const int max_trials = 10;

  for (int i = 0; i < max_trials; i++) {
    uint16_t ret = random() % port_used.size();
    if (ret && !port_used[ret]) {
      port_used[ret] = true;
      return ret;
    }
  }

  return 0;
}

static int packet_processing(struct rte_mbuf *mbuf) {
  ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, ipv4_hdr *, sizeof(ether_hdr));
  tcp_hdr *tcph = (tcp_hdr *)((char *)iph + ((iph->version_ihl & 0xf) << 2));

  FlowKey flow_key(ntohl(iph->src_addr), ntohl(iph->dst_addr),
                   ntohs(tcph->src_port), ntohs(tcph->dst_port));

  RefState state;
  SwRef<NatEntry> ref_entry = g_nat_entry_map.create(&flow_key, state);
  NatEntry::Data data_entry;

  if (!state.created) {
    data_entry = ref_entry->read();
  } else {
    // New connection. Create a new mapping.
    IPKey ip_key(CLUSTER_ID);
    SwRef<PortPool> ref_pool = g_port_pool_map.get(&ip_key);

    // XXX: choosing one among available ports must be ATOMIC.
    // No such thing exists in the StatelessNF implementation...
    PortPool::Data data_pool = ref_pool->read();

    if ((data_entry.new_port = find_free_port(data_pool.port_used)) == 0) {
      // Port numbers have been exhausted. Just forward the packet without NAT.
      return 1;
    }

    // Claim the port
    ref_pool->write(data_pool);

    // Add a forward entry
    data_entry.new_ip = data_pool.public_ip;
    data_entry.is_forward = true;
    ref_entry = g_nat_entry_map.get(&flow_key);
    ref_entry->write(data_entry);

    // Add a reverse entry
    flow_key = FlowKey(ntohl(iph->dst_addr), data_entry.new_ip,
                       ntohs(tcph->dst_port), data_entry.new_port);
    NatEntry::Data reverse_entry = {
        .new_ip = ntohl(iph->src_addr),
        .new_port = ntohs(tcph->src_port),
        .is_forward = false,
    };
    ref_entry = g_nat_entry_map.get(&flow_key);
    ref_entry->write(reverse_entry);
  }

  update_header(iph, tcph, data_entry);

  return 1;  // forward
}

static void background() {
  DEBUG_APP("bg: running");
}

Application *create_application() {
  Application *app = new Application();
  app->set_packet_func(packet_processing);
  app->set_background_func(background);

  return app;
}
