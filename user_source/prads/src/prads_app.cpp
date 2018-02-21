#include "dist.hh"

#include "ip_key.hh"
#include "sha1_key.hh"
#include "sym_flow_key.hh"
#include "stub.connection.hh"
#include "stub.asset.hh"
#include "stub.prads_stat.hh"

#include "prads.h"
#include "config.h"
#include "sig_tcp.h"
#include "sys_func.h"
#include "mac.h"
#include "sig.h"
#include "servicefp.h"

extern MwMap<SHA1Key, PRADSStat> g_prads_stat;
extern SwMap<SymFlowKey, Connection> g_prads_conn;
extern MwMap<IPKey, Asset> g_asset;

SHA1Key *stat_key = new SHA1Key("PradsGlobalStat");	
globalconfig config;

/***************** Managing configurations *****************/

static void update_config(int param)
{
	/* TODO: load signature lists */
	set_default_config_options(&config);

	parse_config_file(config.file);

#define load_foo(func, conf, flag, file, hash, len, dump) \
	if(config. conf & flag) { \
		int _rc; \
		olog("  %-11s %s\n", # flag, (config. file)); \
		_rc = func (config. file, & config. hash, config. len); \
		if(_rc) perror( #flag " load failed!"); \
		else if(config.verbose > 1) { \
			printf("[*] Dumping " #flag " signatures:\n"); \
			dump (config. hash, config. len); \
			printf("[*] " #flag " signature dump ends.\n"); \
		} \
	}

	load_foo(load_mac , cof, CS_MAC, sig_file_mac, sig_mac, 
			mac_hashsize, dump_macs);
	load_foo(load_sigs, ctf, CO_SYN, sig_file_syn, sig_syn, 
			sig_hashsize, dump_sigs);
	load_foo(load_sigs, ctf, CO_SYNACK, sig_file_synack, sig_synack, 
			sig_hashsize, dump_sigs);
	load_foo(load_sigs, ctf, CO_ACK, sig_file_ack, sig_ack, 
			sig_hashsize, dump_sigs);
	load_foo(load_sigs, ctf, CO_FIN, sig_file_fin, sig_fin, 
			sig_hashsize, dump_sigs);
	load_foo(load_sigs, ctf, CO_RST, sig_file_rst, sig_rst, 
			sig_hashsize, dump_sigs);
	//load_foo(load_dhcp_sigs, ctf, CO_DHCP, sig_file_dhcp, sig_dhcp, 
	//sig_hashsize, dump_dhcp_sigs);
	load_foo(load_servicefp_file, cof, CS_TCP_SERVER, sig_file_serv_tcp, 
			sig_serv_tcp, sig_hashsize, dump_sig_service);
	load_foo(load_servicefp_file, cof, CS_UDP_SERVICES, sig_file_serv_udp, 
			sig_serv_udp, sig_hashsize, dump_sig_service);
	load_foo(load_servicefp_file, cof, CS_TCP_CLIENT, sig_file_cli_tcp, 
			sig_client_tcp, sig_hashsize, dump_sig_service);
#undef load_foo

	init_services();

	display_config(&config);

	return;
}

/********************* Asset management ********************/

static MwRef<Asset> connection_lookup(packetinfo *pi)
{
	if (nullptr == pi->cxt)
		return nullptr;

	if (pi->sc == SC_CLIENT) {
		if (!pi->cxt->is_reversed()) {
			if (pi->cxt->get_casset() != nullptr)
				return pi->cxt->get_casset();
		} else {
			if (pi->cxt->get_sasset() != nullptr)
				return pi->cxt->get_sasset();
		}
	} else {
		if (!pi->cxt->is_reversed()) {
			if (pi->cxt->get_sasset() != nullptr)
				return pi->cxt->get_sasset();
		} else {
			if (pi->cxt->get_casset() != nullptr)
				return pi->cxt->get_casset();
		}
	}

	return nullptr;
}

static void update_asset(packetinfo *pi) 
{
	if (pi->asset != nullptr) {
		DEBUG_ERR("1update from connection lookuped asset");
		pi->asset->update_asset(pi->vlan, pi->pheader->ts.tv_sec);
		return;
	}

	pi->asset = connection_lookup(pi);
	if (pi->asset != nullptr) {
		DEBUG_ERR("2update from connection lookuped asset");
		pi->asset->update_asset(pi->vlan, pi->pheader->ts.tv_sec);
		return;
	}

	IPKey ipkey(pi->ip4->ip_src);
	RefState state;
	MwRef<Asset> asset = g_asset.create(&ipkey, state);

	/* FIXME: how to initialize mwref_object? */
	if (state.created) {
		pi->stat->inc_assets();
		asset->init_asset(pi->af, pi->vlan, 
				pi->pheader->ts.tv_sec, pi->ip4->ip_src);	
	} else {
		asset->update_asset(pi->vlan, pi->pheader->ts.tv_sec);
	}

	pi->asset = asset;
}

/****************** Connection management ******************/

static int cx_track(packetinfo *pi) 
{

	int af = pi->af;
	if (af == AF_INET6)
		return -1;

	uint32_t src_ip = pi->ip4->ip_src;
	uint32_t dst_ip = pi->ip4->ip_dst;
	uint16_t src_port = pi->s_port;
	uint16_t dst_port = pi->d_port;

	SymFlowKey fkey(src_ip, dst_ip, src_port, dst_port);
	RefState state;
	SwRef<Connection> conn = g_prads_conn.create(&fkey, state);

	if (state.created) {
		conn->init_cxt(pi);
		//log_connection(pi->cxt, CX_NEW);
	} else {
		if (src_ip == conn->get_src_ip() && src_port == conn->get_src_port())
			conn->cxt_update_client(pi);
		else
			conn->cxt_update_server(pi);
	}

	pi->cxt = conn;

	return 0;
}

static int connection_tracking(packetinfo *pi)
{
	cx_track(pi);

	//if(config.cflags & CONFIG_CONNECT){
	//    log_connection(pi->cxt, CX_EXCESSIVE);
	//}
	return 0;
}

/******************* Parsing protocols *********************/

static void prepare_eth(packetinfo *pi) 
{
	pi->stat->inc_eth_recv();
	pi->eth_hdr  = (ether_header *) (pi->packet);
	pi->eth_type = ntohs(pi->eth_hdr->eth_ip_type);
	pi->eth_hlen = ETHERNET_HEADER_LEN;
	return;
}

static void parse_eth(packetinfo *pi)
{
	/* do nothing on prads as well */
}

static void check_vlan (packetinfo *pi)
{
	if (pi->eth_type == ETHERNET_TYPE_8021Q) {
		vlog(0x3, "[*] ETHERNET TYPE 8021Q\n");
		pi->stat->inc_vlan_recv();
		pi->vlan = pi->eth_hdr->eth_8_vid;
		pi->eth_type = ntohs(pi->eth_hdr->eth_8_ip_type);
		pi->eth_hlen += 4;

		/* This is b0rked - kwy and ebf fix */
	} else if (pi->eth_type ==
			(ETHERNET_TYPE_802Q1MT | ETHERNET_TYPE_802Q1MT2 |
			 ETHERNET_TYPE_802Q1MT3 | ETHERNET_TYPE_8021AD)) {
		vlog(0x3, "[*] ETHERNET TYPE 802Q1MT\n");
		pi->mvlan = pi->eth_hdr->eth_82_mvid;
		pi->eth_type = ntohs(pi->eth_hdr->eth_82_ip_type);
		pi->eth_hlen += 8;
	}
	return;
}

static void prepare_ip4(packetinfo *pi) 
{
	pi->stat->inc_ip4_recv();
	pi->af = AF_INET;
	pi->ip4 = (ip4_header *) (pi->packet + pi->eth_hlen);
	pi->packet_bytes = (ntohs(pi->ip4->ip_len) - (IP_HL(pi->ip4) * 4));

	pi->our = true;
	//pi->our = filter_packet(pi->af, &PI_IP4SRC(pi));
	vlog(0x3, "Got %s IPv4 Packet...\n", (pi->our?"our":"foregin"));
	return;
}

static void prepare_tcp (packetinfo *pi)
{
	pi->stat->inc_tcp_recv();
	if (pi->af==AF_INET) {
		vlog(0x3, "[*] IPv4 PROTOCOL TYPE TCP:\n");
		pi->tcph = (tcp_header *) 
			(pi->packet + pi->eth_hlen + (IP_HL(pi->ip4) * 4));
		pi->plen = (pi->pheader->caplen - (TCP_OFFSET(pi->tcph)) * 4 - 
				(IP_HL(pi->ip4) * 4) - pi->eth_hlen);
		pi->payload = (pi->packet + pi->eth_hlen + (IP_HL(pi->ip4) * 4) + 
				(TCP_OFFSET(pi->tcph) * 4));
	} else if (pi->af==AF_INET6) {
		vlog(0x3, "[*] IPv6 PROTOCOL TYPE TCP:\n");
		pi->tcph = (tcp_header *) (pi->packet + pi->eth_hlen + IP6_HEADER_LEN);
		pi->plen = (pi->pheader->caplen - (TCP_OFFSET(pi->tcph)) * 4 - 
				IP6_HEADER_LEN - pi->eth_hlen);
		pi->payload = (pi->packet + pi->eth_hlen + IP6_HEADER_LEN + 
				(TCP_OFFSET(pi->tcph)*4));
	}

	pi->proto  = IP_PROTO_TCP;
	pi->s_port = pi->tcph->src_port;
	pi->d_port = pi->tcph->dst_port;

	connection_tracking(pi);

	//if(config.payload)
	//   dump_payload(pi->payload, (config.payload < pi->plen)?config.payload:pi->plen);

	return; 
}

static void parse_tcp (packetinfo *pi)
{
	update_asset(pi);

	if (TCP_ISFLAGSET(pi->tcph, (TF_SYN))) {
		if (!TCP_ISFLAGSET(pi->tcph, (TF_ACK))) {
			if (IS_COSET(&config,CO_SYN)) {
				vlog(0x3, "[*] - Got a SYN from a CLIENT: dst_port:%d\n",
						ntohs(pi->tcph->dst_port));
				fp_tcp(pi, CO_SYN);
				return;
			}
		} else {
			if (IS_COSET(&config,CO_SYNACK)) {
				vlog(0x3, "[*] Got a SYNACK from a SERVER: src_port:%d\n", 
						ntohs(pi->tcph->src_port));
				fp_tcp(pi, CO_SYNACK);
				if (pi->sc == SC_SERVER) {
					pi->cxt->reverse_pi_cxt();
					pi->sc = SC_CLIENT;
				}
				return;
			}
		} 
	}


	// Check payload for known magic bytes that defines files!
	uint8_t check = pi->cxt->get_check();
	if (pi->sc == SC_CLIENT && !ISSET_CXT_DONT_CHECK_CLIENT(check)) {
		if (IS_CSSET(&config,CS_TCP_CLIENT)
				&& !ISSET_DONT_CHECK_CLIENT(check)) {
			if (pi->af == AF_INET)
				client_tcp4(pi, config.sig_client_tcp);
			else /* do not reach here */
				client_tcp6(pi, config.sig_client_tcp);
		}
		goto bastard_checks;

	} else if (pi->sc == SC_SERVER && !ISSET_CXT_DONT_CHECK_SERVER(check)) {
		if (IS_CSSET(&config,CS_TCP_SERVER)
				&& !ISSET_DONT_CHECK_SERVICE(check)) {
			if (pi->af == AF_INET)
				service_tcp4(pi, config.sig_serv_tcp);
			else /* do not reach here */
				service_tcp6(pi, config.sig_serv_tcp);
		}
		goto bastard_checks;
	}
	vlog(0x3, "[*] - NOT CHECKING TCP PACKAGE\n");
	return;

bastard_checks:
	if (IS_COSET(&config,CO_ACK)
			&& TCP_ISFLAGSET(pi->tcph, (TF_ACK))
			&& !TCP_ISFLAGSET(pi->tcph, (TF_SYN))
			&& !TCP_ISFLAGSET(pi->tcph, (TF_RST))
			&& !TCP_ISFLAGSET(pi->tcph, (TF_FIN))) {
		vlog(0x3, "[*] Got a STRAY-ACK: src_port:%d\n",ntohs(pi->tcph->src_port));
		fp_tcp(pi, CO_ACK);
		return;
	} else if (IS_COSET(&config,CO_FIN) && TCP_ISFLAGSET(pi->tcph, (TF_FIN))) {
		vlog(0x3, "[*] Got a FIN: src_port:%d\n",ntohs(pi->tcph->src_port));
		fp_tcp(pi, CO_FIN);
		return;
	} else if (IS_COSET(&config,CO_RST) && TCP_ISFLAGSET(pi->tcph, (TF_RST))) {
		vlog(0x3, "[*] Got a RST: src_port:%d\n",ntohs(pi->tcph->src_port));
		fp_tcp(pi, CO_RST);
		return;
	}

}

static void prepare_udp (packetinfo *pi)
{
	pi->stat->inc_udp_recv();

	if (pi->af==AF_INET) {
		vlog(0x3, "[*] IPv4 PROTOCOL TYPE UDP:\n");
		pi->udph = (udp_header *) (pi->packet + pi->eth_hlen + (IP_HL(pi->ip4) * 4));
		pi->plen = pi->pheader->caplen - UDP_HEADER_LEN -
			(IP_HL(pi->ip4) * 4) - pi->eth_hlen;
		pi->payload = (pi->packet + pi->eth_hlen +
				(IP_HL(pi->ip4) * 4) + UDP_HEADER_LEN);

	} else if (pi->af==AF_INET6) {
		vlog(0x3, "[*] IPv6 PROTOCOL TYPE UDP:\n");
		pi->udph = (udp_header *) (pi->packet + pi->eth_hlen + + IP6_HEADER_LEN);
		pi->plen = pi->pheader->caplen - UDP_HEADER_LEN -
			IP6_HEADER_LEN - pi->eth_hlen;
		pi->payload = (pi->packet + pi->eth_hlen +
				IP6_HEADER_LEN + UDP_HEADER_LEN);
	}
	pi->proto  = IP_PROTO_UDP;
	pi->s_port = pi->udph->src_port;
	pi->d_port = pi->udph->dst_port;

	connection_tracking(pi);

	//if(config.payload)
	//   dump_payload(pi->payload, (config.payload < pi->plen)?config.payload:pi->plen);
	return;
}

static void parse_udp (packetinfo *pi)
{
	update_asset(pi);

#if 0
	udp_guess_direction(pi); // fix DNS server transfers?
	// Check for Passive DNS
	if ( ntohs(pi->s_port) == 53 ||  ntohs(pi->s_port) == 5353 ) {
		// For now - Proof of Concept! - Fix output way
		if(config.cflags & CONFIG_PDNS) {
			static char ip_addr_s[INET6_ADDRSTRLEN];
			u_ntop_src(pi, ip_addr_s);
			dump_dns(pi->payload, pi->plen, stdout, "\n", ip_addr_s, pi->pheader->ts.tv_sec);
		}
	}
	if (IS_COSET(&config, CO_DHCP) && ntohs(pi->s_port) == 68 && ntohs(pi->d_port) == 67) {
		dhcp_fingerprint(pi); /* basic DHCP parsing*/
	}
	// if (IS_COSET(&config,CO_DNS) && (pi->sc == SC_SERVER && ntohs(pi->s_port) == 53)) passive_dns (pi);

	if (IS_CSSET(&config,CS_UDP_SERVICES)) {
		if (pi->af == AF_INET) {

			if (!ISSET_DONT_CHECK_SERVICE(pi)||!ISSET_DONT_CHECK_CLIENT(pi)) {
				// Check for UDP SERVICE
				service_udp4(pi, config.sig_serv_udp);
			}
			// UPD Fingerprinting
			if (IS_COSET(&config,CO_UDP)) fp_udp4(pi, pi->ip4, pi->udph, pi->end_ptr);
		} else if (pi->af == AF_INET6) {
			if (!ISSET_DONT_CHECK_SERVICE(pi)||!ISSET_DONT_CHECK_CLIENT(pi)) {
				service_udp6(pi, config.sig_client_udp);
			}
			/* fp_udp(ip6, ttl, ipopts, len, id, ipflags, df); */
		}
		return;
	} else {
		vlog(0x3, "[*] - NOT CHECKING UDP PACKAGE\n");
		return;
	}
#endif
}

static void parse_ip4(packetinfo *pi)
{
	switch (pi->ip4->ip_p) {
		case IP_PROTO_TCP:
			prepare_tcp(pi);
			if (!pi->our)
				break;
			parse_tcp(pi);

			if (TCP_ISFLAGSET(pi->tcph, (TF_FIN)) || 
					TCP_ISFLAGSET(pi->tcph, (TF_RST)))
				g_prads_conn.remove(pi->cxt);

			break;
		case IP_PROTO_UDP:
			prepare_udp(pi);
			if (!pi->our)
				break;
			parse_udp(pi);
			break;
		case IP_PROTO_ICMP:
			pi->stat->inc_icmp_recv();
			/* Do not support */
			//prepare_icmp(pi);
			//if (!pi->our)
			//    break;
			//parse_icmp(pi);
			break;
		case IP_PROTO_IP4:
			pi->stat->inc_ip4ip_recv();
			/* Do not support */
			//prepare_ip4ip(pi);
			break;
		case IP_PROTO_IP6:
			pi->stat->inc_ip4ip_recv();
			/* Do not support */
			//prepare_ip4ip(pi);
			break;
		case IP_PROTO_GRE:
			pi->stat->inc_gre_recv();
			/* Do not support */
			//prepare_gre(pi);
			//parse_gre(pi);
			break;

		default:
			pi->stat->inc_othert_recv();
			/* Do not support */
			//prepare_other(pi);
			//if (!pi->our)
			//	break;
			//parse_other(pi);
	}
	return;
}

static void set_pkt_end_ptr (packetinfo *pi)
{
	/* Paranoia! */
	if (pi->pheader->len <= SNAPLENGTH) {
		pi->end_ptr = (pi->packet + pi->pheader->len);
	} else {
		pi->end_ptr = (pi->packet + SNAPLENGTH);
	}
	return;
}

/******************* Background functions *********************/

static void print_prads_stats()
{
	//extern uint64_t cxtrackerid; // cxt.c

	MwRef<PRADSStat> stat = g_prads_stat.get(stat_key);
	s6_prads_stat pr_s = stat->get_prads_stat();

	printf("-- prads:\n");
	printf("-- Total packets received from libpcap    :%12u\n", pr_s.got_packets);
	printf("-- Total Ethernet packets received        :%12u\n", pr_s.eth_recv);
	printf("-- Total VLAN packets received            :%12u\n", pr_s.vlan_recv);
	printf("-- Total ARP packets received             :%12u\n", pr_s.arp_recv);
	printf("-- Total IPv4 packets received            :%12u\n", pr_s.ip4_recv);
	printf("-- Total IPv6 packets received            :%12u\n", pr_s.ip6_recv);
	printf("-- Total Other link packets received      :%12u\n", pr_s.otherl_recv);
	printf("-- Total IPinIPv4 packets received        :%12u\n", pr_s.ip4ip_recv);
	printf("-- Total IPinIPv6 packets received        :%12u\n", pr_s.ip6ip_recv);
	printf("-- Total GRE packets received             :%12u\n", pr_s.gre_recv);
	printf("-- Total TCP packets received             :%12u\n", pr_s.tcp_recv);
	printf("-- Total UDP packets received             :%12u\n", pr_s.udp_recv);
	printf("-- Total ICMP packets received            :%12u\n", pr_s.icmp_recv);
	printf("-- Total Other transport packets received :%12u\n", pr_s.othert_recv);
	printf("--\n");
	//printf("-- Total sessions tracked                 :%12lu\n", cxtrackerid);
	printf("-- Total assets detected                  :%12u\n", pr_s.assets);
	printf("-- Total TCP OS fingerprints detected     :%12u\n", pr_s.tcp_os_assets);
	printf("-- Total UDP OS fingerprints detected     :%12u\n", pr_s.udp_os_assets);
	printf("-- Total ICMP OS fingerprints detected    :%12u\n", pr_s.icmp_os_assets);
	printf("-- Total DHCP OS fingerprints detected    :%12u\n", pr_s.dhcp_os_assets);
	printf("-- Total TCP service assets detected      :%12u\n", pr_s.tcp_services);
	printf("-- Total TCP client assets detected       :%12u\n", pr_s.tcp_clients);
	printf("-- Total UDP service assets detected      :%12u\n", pr_s.udp_services);
	printf("-- Total UDP client assets detected       :%12u\n", pr_s.udp_clients);
}

static void print_asset_list() 
{
	MwIter<Asset>* iter = g_asset.get_local_iterator();
	if (!iter) {
		DEBUG_ERR("Cannot create iterator for g_sset");
		return;
	}

	int counter = 0;
	DEBUG_APP("--assets:\n");
	while(iter->next()) {
		const IPKey *ipkey = dynamic_cast<const IPKey*>(iter->key);
		const MwRef<Asset> asset = *(iter->value);
		counter++;
		//time_t last_seen = asset->get_last_seen();
		//DEBUG_APP("asset for " << *ipkey << " last seen: " << last_seen);
	}

	DEBUG_APP("Number of assets " << counter);
	DEBUG_APP("--\n");
}

/******************* PRADS Applications *********************/

time_t tstamp;

static int init (int param) 
{
	update_config(param);
	return 0;
}

static int packet_processing(struct rte_mbuf *mbuf) 
{
	MwRef<PRADSStat> stat = g_prads_stat.get(stat_key);

	stat->inc_got_packets();

	packetinfo pstruct = {0};
	packetinfo *pi = &pstruct;

	unsigned int len = rte_pktmbuf_data_len(mbuf);
	struct pcap_pkthdr pheader = 
	{s6_gettimeofday(false), len, len}; 

	pi->stat = stat;
	pi->our = 1;
	pi->packet = (uint8_t *)rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
	pi->pheader = &pheader;
	set_pkt_end_ptr(pi);
	tstamp = pi->pheader->ts.tv_sec;


	prepare_eth(pi);
	check_vlan(pi);
	parse_eth(pi);

	if (pi->eth_type == ETHERNET_TYPE_IP) {
		prepare_ip4(pi);
		parse_ip4(pi);

	} else if (pi->eth_type == ETHERNET_TYPE_IPV6) {
		pi->stat->inc_ip6_recv();
		/* Do not support */
		//prepare_ip6(pi);
		//parse_ip6(pi);

	} else if (pi->eth_type == ETHERNET_TYPE_ARP) {
		pi->stat->inc_arp_recv();
		/* Do not support */
		//parse_arp(pi);
	} else {
		stat->inc_otherl_recv();
	}

	return 1;
}

static void update_param() 
{
	//TODO: update signature list
	assert(0);
	//update_config(0);
	return;
}

static void remove_timed_out() 
{
	//TODO: priority queue sorted by last accessed time
	assert(0);

	return;
}

static void print_logs() 
{
	print_prads_stats();
	print_asset_list();
	return;
}

Application* create_application() 
{
	Application *app = new Application();
	app->set_init_func(init);
	app->set_packet_func(packet_processing);
	app->set_background_func(print_logs);
	//app->set_background_func(update_param);
	//app->set_background_func(remove_timed_out);

	return app;
}
