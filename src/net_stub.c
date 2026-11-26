/* No host network bridge on Linux. Java sees the same errors as offline on Windows. */
#include "phone_net.h"
#include "java_net_guest.h"

void java_net_advance(JavaNetwork *n, Cellular *cell, Arm7 *cpu, int enabled)
{
    (void)n;
    (void)cell;
    (void)cpu;
    (void)enabled;
}

void phone_net_close(PhoneNetwork *n)
{
    (void)n;
}

void phone_net_set_mode(PhoneNetwork *n, unsigned mode)
{
    (void)n;
    (void)mode;
}

void phone_net_advance(PhoneNetwork *n, Cellular *cell)
{
    (void)n;
    (void)cell;
}

int phone_net_hle(PhoneNetwork *n, Cellular *cell, Arm7 *cpu)
{
    unsigned op = java_net_operation(&n->java, cpu);
    (void)cell;
    if (op == JN_COUNT)
        return 0;
    if (op == JN_SHUTDOWN)
        return returned(cpu, 0);
    return java_net_unavailable(cpu, op);
}
