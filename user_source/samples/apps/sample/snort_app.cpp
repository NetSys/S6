#include <fstream>
#include <iostream>
#include <pcre.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

#include "dist.hh"

#include "flow_key.hh"
#include "ip_key.hh"
#include "malicious_server.hh"
#include "stub.malicious_server.hh"
#include "stub.tcp_flow.hh"
#include "stub.whitelist.hh"
#include "tcp_flow.hh"
#include "whitelist.hh"

#define MAX_RULE 4096
#define MAX_STRLEN 1000

#define TO_OUT 1
#define FROM_OUT 1

#define TCP_FLAG_FIN 0x01

extern SwMap<FlowKey, TCPFlow> g_tcp_flow_map;
extern MwMap<IPKey, MaliciousServer> g_malicious_server_map;
extern MwMap<IPKey, WhiteList> g_whitelist_map;

char str_rule[MAX_RULE][MAX_STRLEN];
pcre *pcre_rule[MAX_RULE];

int str_rule_cnt = 0;
int pcre_rule_cnt = 0;

static void add_to_whitelist(uint32_t ip_addr) {
  IPKey ipkey(ip_addr, _LCAN_SRC);
  MwRef<WhiteList> list = g_whitelist_map.get(&ipkey);

  // The update needs to be synchronous
  // The packeet should not release before the rule is registered globally
  list->update(ip_addr, true);
  return;
}

static bool is_on_whitelist(uint32_t ip_addr) {
  IPKey ipkey(ip_addr, _LCAN_SRC);
  MwRef<WhiteList> list = g_whitelist_map.get(&ipkey);

  bool is_ok = list->get_is_ok();
  return is_ok;
}

static void load_ids_rules() {
  std::string line;
  // FIXME: relocate ruleset file location
  char *home = getenv("S6_HOME");
  std::string config_file =
      std::string(home) + "/user_source/config/community-rules/community.rules";
  std::ifstream myfile(config_file);

  if (!myfile.is_open()) {
    DEBUG_ERR("fail to open snort ruleset file");
    return;
  }

  DEBUG_INFO("Initialize IDS rules using " << config_file);

  char *str_pattern = "content:\"[^\"]+\"";
  char *pcre_pattern = "pcre:\"[^\"]+\"";

  const char *error;
  int erroffset;

  pcre *str_re =
      pcre_compile(str_pattern,            /* pattern */
                   0,                      /* options */
                   &error, &erroffset, 0); /* use default character tables */

  pcre *pcre_re =
      pcre_compile(pcre_pattern,           /* pattern */
                   0,                      /* options */
                   &error, &erroffset, 0); /* use default character tables */

  while (getline(myfile, line)) {
    const char *cline = line.c_str();
    int ovector[100];

    int rc = pcre_exec(str_re, 0, cline, strlen(cline), 0, 0, ovector,
                       sizeof(ovector));
    if (rc >= 0) {
      for (int i = 0; i < rc; ++i) {
        // printf("%2d: %.*s\n", str_rule_cnt, ovector[2*i+1]-ovector[2*i],
        // cline + ovector[2*i]);

        int size = ovector[2 * i + 1] - ovector[2 * i];
        if (size < MAX_STRLEN) {
          strncpy(str_rule[str_rule_cnt], cline + ovector[2 * i] + 9,
                  size - 10);
          str_rule_cnt++;
        } else {
          DEBUG_ERR("string is larger than defined maximum.");
        }

        if (str_rule_cnt > MAX_RULE)
          break;
      }
    }

    rc = pcre_exec(pcre_re, 0, cline, strlen(cline), 0, 0, ovector,
                   sizeof(ovector));
    if (rc >= 0) {
      for (int i = 0; i < rc; ++i) {
        // printf("%2d: %.*s\n", i, ovector[2*i+1]-ovector[2*i], cline +
        // ovector[2*i]);

        int size = ovector[2 * i + 1] - ovector[2 * i];
        if (size < MAX_STRLEN) {
          char pcre_pattern[1000] = {0};
          strncpy(pcre_pattern, cline + ovector[2 * i] + 5, size - 6);
          pcre_rule[pcre_rule_cnt] =
              pcre_compile(pcre_pattern, 0, &error, &erroffset, 0);
          pcre_rule_cnt++;
        } else {
          DEBUG_ERR("string is larger than defined maximum.");
        }

        if (pcre_rule_cnt > MAX_RULE)
          break;
      }
    }
  }

  DEBUG_INFO("Number of rules");
  DEBUG_INFO("\tstring matching: " << str_rule_cnt);
  DEBUG_INFO("\tpcre matching: " << pcre_rule_cnt);

  myfile.close();
}

static int match_str(uint8_t *payload, uint32_t payload_len, char *rule) {
  payload[payload_len] = 0;
  char *ret = strstr((char *)payload, rule);
  if (ret != NULL) {
    // DEBUG_APP("Detect flow to malicious pattern " << rule);
    return 1;
  }

  /* for s6 */
  double r = (double)rand() / RAND_MAX;
  if (r < 0.01)
    return 1;

  return 0;
}

static int match_pcre(uint8_t *payload, uint32_t payload_len, pcre *rule) {
  int ovector[100];
  payload[payload_len] = 0;

  int rc = pcre_exec(rule, 0, (char *)payload, payload_len, 0, 0, ovector,
                     sizeof(ovector));
  if (rc >= 0)
    return 1;

  /* for s6 */
  double r = (double)rand() / RAND_MAX;
  if (r < 0.01)
    return 1;

  return 0;
}

static int is_malicious(uint8_t *payload, uint32_t payload_len) {
  for (int i = 0; i < 10; i++) {
    int ridx = random() % str_rule_cnt;
    int ret = match_str(payload, payload_len, str_rule[ridx]);
    if (ret > 0)
      return ridx;
  }

  // N % of packets goes to pcre matching
  double r = (double)rand() / RAND_MAX;
  if (r > 0.2)
    return 0;

  for (int i = 0; i < 20; i++) {
    int ridx = random() % pcre_rule_cnt;
    int ret = match_pcre(payload, payload_len, pcre_rule[ridx]);
    if (ret > 0)
      return ridx;
  }

  return 0;
}

static int get_direction(uint32_t src_addr, uint32_t dst_addr) {
  /* TODO */
  return TO_OUT;
}

static int init(int param) {
  load_ids_rules();
  return 0;
}

static int packet_processing(struct rte_mbuf *mbuf) {
  ipv4_hdr *iph = rte_pktmbuf_mtod_offset(mbuf, struct ipv4_hdr *,
                                          sizeof(struct ether_hdr));

  if (iph->next_proto_id != IPPROTO_TCP)
    return 0;

  tcp_hdr *tcph = (tcp_hdr *)((u_char *)iph +
                              ((iph->version_ihl & IPV4_HDR_IHL_MASK) << 2));

#if 0
	int direction = get_direction(iph->src_addr, iph->dst_addr);

	/* check whether the ip is already known  --> firewall */
	if (direction == TO_OUT)
		add_to_whitelist(iph->dst_addr);

	if (!is_on_whitelist(iph->src_addr)) 
		return -1; // do not forward to next hop
#endif
  /* update TCP context --> ids */
  FlowKey fkey(ntohl(iph->src_addr), ntohl(iph->dst_addr),
               ntohs(tcph->src_port), ntohs(tcph->dst_port));

  RefState state;
  SwRef<TCPFlow> flow = g_tcp_flow_map.create(&fkey, state);
  if (state.created)
    flow->init_c2s(mbuf);
  flow->update_context(mbuf);

  /* check payload */
  uint8_t *payload = (uint8_t *)tcph + ((tcph->data_off & 0xf0) >> 2);
  uint32_t payload_len = ntohs(iph->total_length) - (payload - (uint8_t *)iph);
  int attack_pattern = 0;

  if (payload_len > 0)
    attack_pattern = is_malicious(payload, payload_len);

  if (attack_pattern > 0) {
    IPKey ipkey(iph->src_addr, _LCAN_SRC);

    MwRef<MaliciousServer> mserver = g_malicious_server_map.get(&ipkey);

    mserver->detect_malicious_pattern(attack_pattern, iph->src_addr,
                                      iph->dst_addr, tcph->src_port,
                                      tcph->dst_port);

    // mserver->add_cflow(flow);
  }

  if (tcph->tcp_flags & TCP_FLAG_FIN)
    g_tcp_flow_map.remove(flow);

  return 1;  // forward to next hop
};

static void report_malicious(uint32_t ip) {
  IPKey *ipkey = new IPKey(ip);

  const MwRef<MaliciousServer> mserver =
      g_malicious_server_map.lookup_const(ipkey);

  if (mserver)
    mserver->print_cflows();
};

static void report_malicious_all() {
  MwIter<MaliciousServer> *iter = g_malicious_server_map.get_local_iterator();
  if (!iter) {
    DEBUG_ERR("Cannot create iterator for g_mallicious_server_map");
    return;
  }

  DEBUG_APP("=== Hosts destined to malicious servers");
  while (iter->next()) {
    const IPKey *key = dynamic_cast<const IPKey *>(iter->key);
    const MwRef<MaliciousServer> server = *(iter->value);

    server->print_cflows();
  }

  g_malicious_server_map.release_local_iterator(iter);
};

Application *create_application() {
  Application *app = new Application();
  app->set_init_func(init);
  app->set_packet_func(packet_processing);
  app->set_background_func(report_malicious_all);

  return app;
};
