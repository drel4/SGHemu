#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    Machine *m = calloc(1, sizeof(*m));
    unsigned edges = 0, colored = 0, i;
    const char *config = argc > 1 ? argv[1] : "models/c100/c100xeda2-bringup.cfg";
    if (!m || !machine_load(m, config, NULL, NULL)) { fprintf(stderr, "Local ROM pair required: %s\n", config); free(m); return 1; }
    while (m->cpu.instructions < 10000000) if (!machine_step(m, m->bringup)) break;
    for (i = 0; i < m->width * m->height; ++i) {
        uint32_t v = m->pixels[i];
        if (v != 0 && v != 0xffffff) ++colored;
        if (i && v != m->pixels[i - 1]) ++edges;
    }
    if (m->cpu.halted || !m->lcd.enabled || m->lcd.standby || m->lcd.writes < 128 * 128 ||
        colored < 1000 || edges < 500 || !m->intc.events || !m->pmic.transfers) {
        fprintf(stderr, "Boot/LCD regression: PC=%08x on=%d standby=%d colored=%u edges=%u\n",
            m->cpu.r[15], m->lcd.enabled, m->lcd.standby, colored, edges);
        machine_destroy(m); free(m); return 1;
    }
    printf("Guest LCD boot: %s, %u colored pixels, %u edges, %llu guest writes.\n", config, colored, edges, (unsigned long long)m->lcd.writes);
    machine_destroy(m); free(m); return 0;
}
