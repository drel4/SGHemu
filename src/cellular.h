#ifndef SGH_CELLULAR_H
#define SGH_CELLULAR_H
#include "arm7.h"
#include "sim_card.h"
#define CELL_EVENTS 16
#define CELL_EVENT_BYTES 1100
#define CELL_CALLBACK 0xffffffffu
#define CELL_NATIVE_STACK_BYTES 16384u
#define CELL_NATIVE_URL_BYTES 4096u
#define CELL_NATIVE_ALLOC_BYTES (CELL_NATIVE_STACK_BYTES+CELL_NATIVE_URL_BYTES)
#define CELL_IDLE 0
#define CELL_TO_PHONE 1
#define CELL_FROM_PHONE 2
#define CELL_CONNECTED 3
typedef struct {uint32_t message,target,free_ptr;unsigned size;uint8_t data[CELL_EVENT_BYTES];} CellEvent;
typedef struct {
    uint32_t ready,alloc,release,send_ui,dial,accept,disconnect,name_lookup,services;
    uint32_t sms_send,sms_reply,send_message,sr_task;
    uint32_t radio_select,alert_request,poll;
    char network[31],number[33],last_sms_number[33],error[180];
    uint16_t last_sms_text[161];unsigned last_sms_length;
    unsigned enabled,mode,call_state,call_id,outgoing,revision,sms_revision;
    uint64_t sent_sms,received_sms,delivered_events;
    CellEvent events[CELL_EVENTS];unsigned head,count,stage;
    uint32_t name_ptr,data_ptr,native_stack;Arm7 caller;
    int network_pending,boot_ready;
} Cellular;
void cellular_init(Cellular *s);
int cellular_set_network(Cellular *s,Bus *bus,int enabled,const char *name);
int cellular_call(Cellular *s,Bus *bus,const char *number);
int cellular_answer(Cellular *s,int accept);
int cellular_sms(Cellular *s,const char *number,const uint16_t *text,unsigned length,const uint8_t scts[7]);
/* -1: unspecified (normal SMS); 0..3: GSM message class. */
int cellular_sms_class(Cellular *s,const char *number,const uint16_t *text,unsigned length,const uint8_t scts[7],int message_class);
int cellular_queue(Cellular *s,uint32_t target,uint32_t message,const void *bytes,unsigned length);
int cellular_advance(Cellular *s,Arm7 *c,int busy,int sim_present);
int cellular_hle(Cellular *s,Arm7 *c);
#endif
