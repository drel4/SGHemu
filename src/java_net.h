#ifndef SGH_JAVA_NET_H
#define SGH_JAVA_NET_H
#include "cellular.h"
enum {JN_OPEN,JN_SHUTDOWN,JN_SOCKET,JN_CONNECT,JN_CLOSE,JN_WRITE,JN_READ,
    JN_AVAILABLE,JN_LOOKUP,JN_NOTIFY,JN_LOCAL_IP,JN_REMOTE_IP,JN_LOCAL_PORT,JN_REMOTE_PORT,JN_BIND,JN_COUNT};
typedef struct JavaNetRuntime JavaNetRuntime;
typedef struct {
    uint32_t entry[JN_COUNT],first,last;JavaNetRuntime *runtime;
    uint64_t next_poll,lookups,connects,sent,received;unsigned serial;
    char error[160];
} JavaNetwork;
static inline int java_net_may_call(const JavaNetwork *n,uint32_t pc)
{return n->first&&pc>=n->first&&pc<=n->last;}
void java_net_close(JavaNetwork *n);
void java_net_advance(JavaNetwork *n,Cellular *cell,Arm7 *cpu,int enabled);
int java_net_hle(JavaNetwork *n,Cellular *cell,Arm7 *cpu,int enabled);
#endif
