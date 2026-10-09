/* Optional real-image RE regression. Requires local proprietary firmware. */
#include "machine.h"
#include <stdio.h>
int main(void)
{
    Machine m = {0};
    FILE *log = fopen("build/identity-test.log", "w");
    int result = 1;
    if (!log || !machine_load(&m, "models/c100-lab.cfg", NULL, log)) goto done;
    while (m.cpu.instructions < 3100000 && m.cpu.r[15] != 0x0115c828) if (!machine_step(&m, 1)) break;
    printf("Identity experiment: PC=%08x R0=%u R4=%u cache=%u after %llu instructions\n",
        m.cpu.r[15], m.cpu.r[0], m.cpu.r[4], bus_read(&m.bus, 0x0040cd7c, 1), (unsigned long long)m.cpu.instructions);
    if (m.cpu.r[15] == 0x0115c828 && m.cpu.r[0] == 1 && m.cpu.r[4] == 3 &&
        bus_read(&m.bus, 0x0040cd7c, 1) == 3 && bus_read(&m.bus, 0x00676ee0, 4) == 0x01217d01) {
        puts("PASS: firmware selected class 3 and populated its driver table without patched instructions."); result = 0;
    }
done:
    machine_destroy(&m); if (log) fclose(log); return result;
}
