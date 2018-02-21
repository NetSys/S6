#include <unistd.h>

#include <csignal>
#include <iostream>

#include <rte_eal.h>
#include <rte_lcore.h>

#include "../src/application.hh"
#include "../src/dpdk.hh"
#include "../src/log.hh"
#include "../src/stub_factory.hh"
#include "../src/tcp_controlbus.hh"
#include "../src/worker.hh"
#include "../src/worker_config.hh"

#define RPC_WORKER_ID MAX_PWORKER_CNT

/* FIXME: BETTER INTERFACE
 * For efficient memory allocation structure
 * -1: use normall malloc
 * > 0: use efficient malloc
 */
int MAX_SWOBJ_SIZE = -1;
int MAX_MWOBJ_SIZE = -1;
int MAX_FLOWS = -1;
int MAX_SHARED_OBJS = -1;

Worker *worker = nullptr;

static void show_usage(char *prog_name) {
  DEBUG_ERR("Usage: " << prog_name
                      << " Options\n"
                         "-h help Showing this message \n"
                         "-d <worker_id> \n"
                         "-n <node_id> \n"
                         "-i <vdev string (e.g., \"--vdev=virtio_user..\")> \n"
                         "-C <control network ip:port> \n"
                         "-m <management socket with controller> \n"
                         "[-b background worker (default: packet worker)] \n"
                         "[-c <core id>] \n");
  return;
}

void signal_handler(int signo) {
  if (signo == SIGTERM) {
    if (worker)
      worker->reserve_quit();
    else
      exit(EXIT_FAILURE);
  }
}

void dump_maps() {
  for (int i = 0; i < num_dmap; i++) {
    std::cout << i << ' ' << __global_dobj_type[i] << ' '
              << __global_dobj_size[i] << ' ' << __global_dobj_name[i]
              << std::endl;
  }

  exit(EXIT_SUCCESS);
}

// start worker process
int main(int argc, char *argv[]) {
  uint16_t w_id = -1;
  uint16_t n_id = -1;
  unsigned core = 0;

  char *vport = "";
  WorkerAddress *mng_addr = NULL;
  WorkerAddress *state_addr = NULL;
  bool with_controller = false;

  WorkerType w_type = PACKET_WORKER;

  int opt;

  // load cmd options
  while ((opt = getopt(argc, argv, "bc:d:Dhi:m:n:s:")) != -1) {
    switch (opt) {
      case 'b':
        w_type = BACKGROUND_WORKER;
        break;
      case 'c':
        core = atoi(optarg);
        break;
      case 'd':
        w_id = atoi(optarg);
        break;
      case 'D':
        dump_maps();
        break;
      case 'h':
        show_usage(argv[0]);
        exit(EXIT_FAILURE);
      case 'i':
        vport = optarg;
        break;
      case 'm':
        mng_addr = parse_worker_address(optarg);
        if (!mng_addr) {
          DEBUG_ERR("incorrect mng channel format");
          exit(EXIT_FAILURE);
        }
        with_controller = true;
        break;
      case 'n':
        n_id = atoi(optarg);
        break;
      case 's':
        state_addr = parse_worker_address(optarg);
        if (!state_addr) {
          DEBUG_ERR("incorrect state channel format");
          exit(EXIT_FAILURE);
        }
        break;
      default:
        DEBUG_ERR("unknown option: " << opt);
        show_usage(argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  // check configuration is correct
  if (w_id < 0) {
    DEBUG_ERR("worker id should be specified (-w)");
    show_usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  if (state_addr == NULL) {
    DEBUG_ERR("state channel should be specified (-s)");
    show_usage(argv[0]);
    exit(EXIT_FAILURE);
  }

  if (signal(SIGTERM, signal_handler) == SIG_ERR) {
    return 0;
  }

  if (w_type == PACKET_WORKER) {
    // Initiate DPDK
    int rte_argc = 0;
    const char *rte_argv[MAX_RTE_ARGV];

    char file_prefix[100];
    sprintf(file_prefix, "%s%2d", "--file-prefix=s6", w_id);

    rte_argv[rte_argc++] = argv[0];
    rte_argv[rte_argc++] = "-l";
    rte_argv[rte_argc++] = "0";
    rte_argv[rte_argc++] = "-m";
    rte_argv[rte_argc++] = "1024";
    rte_argv[rte_argc++] = file_prefix;
    rte_argv[rte_argc++] = "--no-pci";
    rte_argv[rte_argc++] = vport;
    rte_argv[rte_argc] = nullptr;

    init_dpdk(rte_argc, rte_argv);
  }

  // Core affinitize
  if (core >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    rte_thread_set_affinity(&set);
  }

  // Create worker configuration
  WorkerConfig wconfig;
  wconfig.id = w_id;
  wconfig.node_id = n_id;
  wconfig.type = w_type;

  for (int i = 0; i < MAX_WORKER_CNT; i++) {
    wconfig.pong_received_from[i] = false;
  }

  wconfig.mng_addr = mng_addr;
  wconfig.state_addr = state_addr;

  wconfig.max_swobj_size = MAX_SWOBJ_SIZE;
  wconfig.max_expected_flows = MAX_FLOWS;
  wconfig.max_mwobj_size = MAX_MWOBJ_SIZE;
  wconfig.max_expected_shared_objs = MAX_SHARED_OBJS;

  worker = new Worker(&wconfig, true, new TCPControlBus());

  if (w_type == BACKGROUND_WORKER)
    worker->set_remote_serving();

  if (!with_controller) {
    // FIXME load configuration from somewhere else
    ActiveWorkers *active_workers = new ActiveWorkers();
    active_workers->pworker_cnt = 2;
    active_workers->state_addrs[RPC_WORKER_ID] =
        *WorkerAddress::CreateWorkerAddress("127.0.0.1:1000");
    active_workers->state_addrs[0] =
        *WorkerAddress::CreateWorkerAddress("127.0.0.1:1001");

    worker->set_active_workers(active_workers);
  }

  DEBUG_INFO("==================================================");
  DEBUG_INFO("Worker id: " << wconfig.id);
  DEBUG_INFO("Node id: " << wconfig.node_id);
  DEBUG_INFO("Worker type: " << ((wconfig.type == PACKET_WORKER)
                                     ? "PACKET_WORKER"
                                     : "BACKGROUND_WORKER"));
  DEBUG_INFO("State Channel network address: " << *state_addr);
  if (core >= 0)
    DEBUG_INFO("Core id: " << core);
  DEBUG_INFO("=================================================");

  Application *app = create_application();
  worker->set_application(app, w_type);

  worker->run(with_controller);

  return 0;
}
