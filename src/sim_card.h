#ifndef SGH_SIM_CARD_H
#define SGH_SIM_CARD_H
#include <stdint.h>
#define SIM_FILES 32
typedef struct { uint16_t id,parent,size; uint8_t record; uint8_t data[2048]; } SimFile;
typedef struct {
    SimFile files[SIM_FILES]; unsigned count;
    uint16_t directory,selected,record;
    uint8_t command[261],response[1024];
    unsigned command_size,input_size,response_head,response_size;
    uint8_t metadata[34]; unsigned metadata_size;
    uint64_t commands;
    char imsi[16],iccid[21],spn[17],error[128];
    int overflow;
    int dirty;
} SimCard;
void sim_card_init(SimCard *c);
int sim_card_load(SimCard *c,const char *path);
void sim_card_reset(SimCard *c);
void sim_card_send(SimCard *c,uint8_t byte);
int sim_card_receive(SimCard *c,uint8_t *byte);
int sim_card_save_storage(SimCard *c,const char *path);
int sim_card_load_storage(SimCard *c,const char *path);
/* Store a single UCS-2 SMS-DELIVER in an unread EF_SMS record; return its 1-based index. */
unsigned sim_card_deliver_sms(SimCard *c,const char *sender,const uint16_t *text,unsigned length,const uint8_t scts[7]);
#endif
