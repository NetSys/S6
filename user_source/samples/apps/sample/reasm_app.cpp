#include <cstdlib>

#include "dist.hh"
#include "flow_key.hh"
#include "ip_key.hh"
#include "stub.packet_buf.hh"
#include "stub.reasm_entry.hh"

extern SwMap<FlowKey, ReasmEntry> g_reasm_map;
extern SwMap<IPKey, PacketBuf> g_packet_buf_map;

// prob[i] denotes the probability of packet reordering that X'th packet
// is swapped with (X+i+1)'th one. Should be monotonically decreasing.
static const std::array<double, 5> reorder_prob = {0.128, 0.032, 0.008, 0.002,
                                                   0.0005};
// static const std::array<double, 5> reorder_prob = {0, 0, 0, 0, 0};

// Generate per-packet keys, unique across all flows
static inline uint32_t generate_pktbuf_key() {
  static uint32_t next_key = 1;  // key 0 is reserved for "invalid"
  return next_key++;
}

static void fill_new_entry(ReasmEntry::Data &data_entry) {
  data_entry.next_expected_seq = 1;
  data_entry.next_generated_seq = 1;

  for (auto &seq : data_entry.planned_seqs) {
    seq = data_entry.next_generated_seq++;
  }

  data_entry.buffered_segs = {};
}

// Rather than extracting a sequence number from the TCP packet, we randomly
// simulate the packet sequence and reordering among packets.
static uint32_t get_pkt_seq(ReasmEntry::Data &data_entry) {
  uint32_t ret = data_entry.planned_seqs[0];

  auto &planned = data_entry.planned_seqs;
  // Shift elements to the left by one slot
  std::rotate(&planned[0], &planned[1], planned.end());
  planned.back() = data_entry.next_generated_seq++;

  // Random shuffling
  double dice = drand48();
  for (size_t diff = reorder_prob.size(); diff > 0; diff--) {
    if (dice < reorder_prob[diff - 1]) {
      // swap planned[x] and planned[x + dif]
      size_t x = random() % (planned.size() - diff);
      std::swap(planned[x], planned[x + diff]);
      break;
    }
  }

  return ret;
}

static int packet_processing(struct rte_mbuf *mbuf) {
  ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, ipv4_hdr *, sizeof(ether_hdr));
  tcp_hdr *tcph = (tcp_hdr *)((char *)iph + ((iph->version_ihl & 0xf) << 2));
  char *payload = (char *)(tcph + 1) + (tcph->data_off >> 2);
  size_t payload_len = rte_pktmbuf_data_len(mbuf) -
                       ((uintptr_t)payload - rte_pktmbuf_mtod(mbuf, uintptr_t));

  if (payload_len == 0) {
    return 1;  // noop for non-data packets
  }

  FlowKey flow_key(ntohl(iph->src_addr), ntohl(iph->dst_addr),
                   ntohs(tcph->src_port), ntohs(tcph->dst_port));

  RefState state;
  SwRef<ReasmEntry> ref_entry = g_reasm_map.create(&flow_key, state);
  ReasmEntry::Data data_entry;

  if (state.created) {
    // new connection
    fill_new_entry(data_entry);
    ref_entry = g_reasm_map.get(&flow_key);
    ref_entry->write(data_entry);
  } else {
    data_entry = ref_entry->read();
  }

  uint32_t received_seq = get_pkt_seq(data_entry);

#if 0
  std::ostringstream out;
  out << received_seq << '/' << data_entry.next_expected_seq << " |";

  for (auto &seq : data_entry.planned_seqs) {
    out << ' ' << seq;
  }
  out << " |";

  int count = 0;
  for (auto &seg : data_entry.buffered_segs) {
    if (seg.seq) {
      out << ' ' << seg.seq;
      count++;
    }
  }
  out << " | " << count;
  if (count >= 12) {
    DEBUG_WRK(out.str());
  }
#endif

  // In the current API, there is no way to buffer packets and send them later.
  // Instead we emulate only the buffering behavior.
  if (received_seq == data_entry.next_expected_seq) {
    bool found_match;
    do {
      found_match = false;
      data_entry.next_expected_seq++;

      for (auto &seg : data_entry.buffered_segs) {
        if (seg.seq == data_entry.next_expected_seq) {
          seg.seq = 0;
          IPKey ip_key(seg.pktbuf_key);
          SwRef<PacketBuf> ref_pktbuf = g_packet_buf_map.get(&ip_key);
          ref_pktbuf->read();
          g_packet_buf_map.remove(ref_pktbuf);
          found_match = true;
          break;
        }
      }
    } while (found_match);
  } else {
    bool found_empty_slot = false;
    for (auto &seg : data_entry.buffered_segs) {
      if (seg.seq == 0) {
        seg.seq = received_seq;
        seg.pktbuf_key = generate_pktbuf_key();
        IPKey ip_key(seg.pktbuf_key);
        SwRef<PacketBuf> ref_pktbuf = g_packet_buf_map.get(&ip_key);
        PacketBuf::Data data_pktbuf = {.seq = received_seq};
        ref_pktbuf->write(data_pktbuf);
        found_empty_slot = true;
        break;
      }
    }
    if (!found_empty_slot) {
      DEBUG_WRK("-_-" << received_seq << ' ' << data_entry.next_expected_seq);
    }
    assert(found_empty_slot);
  }

  ref_entry->write(data_entry);
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
