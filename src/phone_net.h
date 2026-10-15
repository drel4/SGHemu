#ifndef SGH_PHONE_NET_H
#define SGH_PHONE_NET_H
#include "cellular.h"
typedef struct PhoneNet PhoneNet;
typedef struct {
    uint32_t txn_start,send_pdu,recv_pdu,bearer_state,post_event,data_arrived,wap_base,driver_base;
    PhoneNet *runtime;unsigned mode,opened[2];uint64_t transmitted,received;
    uint64_t state_queries,start_requests;unsigned last_bearer;
    int profiling;uint64_t lock_calls,lock_wait_ticks;
    FILE *trace; /* Optional packet trace; caller owns it, CPU thread only. */
    char error[160];
} PhoneNetwork;
void phone_net_close(PhoneNetwork *n);
void phone_net_set_mode(PhoneNetwork *n,unsigned mode);
void phone_net_advance(PhoneNetwork *n,Cellular *cell);
int phone_net_hle(PhoneNetwork *n,Cellular *cell,Arm7 *cpu);
#endif
