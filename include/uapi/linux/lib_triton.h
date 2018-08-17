#ifndef _CPU_FERQ_HELPER_IOCTL_H
#define _CPU_FERQ_HELPER_IOCTL_H
#include <linux/ioctl.h>
#define IOCTL_PATH "triton:io"

/* Feature defines */

#define SCHED_BUSY_SUPPORT

#define MAX_CORES          (4)
#define NUM_CLUSTER	(2)
#define CORES_PER_CLUSTER (MAX_CORES/NUM_CLUSTER)
#if (NUM_CLUSTER > 1)
#define BIT_MAX	(3)
#define BIT_GPU		(2)
#define BIT_BIG		(1)
#define BIT_LITTLE 	(0)
#else
#define BIT_MAX (2)
#define BIT_GPU		(1)
#define BIT_LITTLE 	(0)
#endif
#define MAGIC_NUM	(0x10)

enum sys_state_machine{
	RUNNING,
	FREEZE
};
enum adjust_level {
	CEILING,
	FLOOR,
};
enum sys_cmd_comm {
	COMM_POLICY,
	COMM_ENABLE,
	COMM_ENFORCE
};

enum sys_state_policy {
	NT,
	STB,
	NOM,
	SSTB,
};

struct sys_cmd_perf_level_req {
	int turbo;
	int nominal;
	int svs;
	int effi;
	int perf;
	int freeze;
};
struct sys_cmd_tunables_req {
	int tunable_trigger_rate_ms;
	int tunable_timer_rate_us;
	int tunable_stu_high_res;
	int tunable_sstu_high_res;
	int tunable_ping_exp;
	int tunable_ping_intv;
};
struct sys_cmd_tunables_bmc_req{
	int coeff_b;
	int minturbo;
	int turbo;
	int maxturbo;
};
struct sys_cmd_freq_req {
	unsigned long req_cpuload[NUM_CLUSTER];
	unsigned long each_load[MAX_CORES];
	int req_cluster;
	int req_cpu;
	int req_freq;
};
struct sys_cmd_cluster_info_req {
	int req_total_cores;
	int req_total_clusters;
	int req_core_per_cluster;
};
struct sys_cmd_comm_req {
	int req;
	int val;
};


enum ioctl_req_id {
	REQ_ID_AFREQ,
	REQ_ID_BFREQ,
	REQ_ID_CLUSTER_INFO,
	REQ_ID_COMM,
	REQ_ID_PERF_LEVEL,
	REQ_ID_BASIC_TUNABLE,
	REQ_ID_TUNABLE_BMC,
	REQ_ID_PING,
	REQ_MAX
};
#define IOCTL_AFREQ_REQ \
	_IOR(MAGIC_NUM, REQ_ID_AFREQ, struct sys_cmd_freq_req)

#define IOCTL_BFREQ_REQ \
	_IOR(MAGIC_NUM, REQ_ID_BFREQ, struct sys_cmd_freq_req)

#define IOCTL_COMM_REQ \
	_IOW(MAGIC_NUM, REQ_ID_COMM, struct sys_cmd_comm_req)

#define IOCTL_PERF_LEVEL_REQ \
	_IOW(MAGIC_NUM, REQ_ID_PERF_LEVEL, struct sys_cmd_perf_level_req)

#define IOCTL_BASIC_TUNABLE_REQ \
	_IOW(MAGIC_NUM, REQ_ID_BASIC_TUNABLE, struct sys_cmd_tunables_req)


#define IOCTL_TUNALBE_BMC_REQ \
	_IOW(MAGIC_NUM, REQ_ID_TUNABLE_BMC, struct sys_cmd_tunables_bmc_req)

#define IOCTL_PING_REQ \
	_IOW(MAGIC_NUM, REQ_ID_PING, int)
#endif
