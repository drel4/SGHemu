#ifndef SGH_WAP_GATEWAY_H
#define SGH_WAP_GATEWAY_H
#include <stddef.h>
#include <stdint.h>
#define WAP_TRANSACTIONS 8
#define WAP_REPLY_MAX 65536
#define WAP_SEGMENT 1300
typedef int (*WapFetch)(void *user,const char *url,uint8_t **body,size_t *length,char mime[96],unsigned *status);
typedef int (*WapEmit)(void *user,const uint8_t *bytes,size_t length);
typedef struct {uint16_t tid;uint8_t *reply;size_t size;unsigned packets,first,end,retries,active,complete;uint64_t due,expires;uint8_t *request;size_t request_size;} WapTransaction;
typedef struct {WapTransaction transactions[WAP_TRANSACTIONS];uint32_t session;unsigned malformed;char error[128];} WapGateway;
void wap_gateway_init(WapGateway *g);
void wap_gateway_close(WapGateway *g);
int wap_gateway_receive(WapGateway *g,const uint8_t *data,size_t length,uint64_t now,WapFetch fetch,void *fetch_user,WapEmit emit,void *emit_user);
int wap_gateway_tick(WapGateway *g,uint64_t now,WapEmit emit,void *user);
typedef struct {void *volatile active;volatile long canceled;} WapHttpContext;
void wap_http_cancel(WapHttpContext *context);
int wap_http_fetch(void *unused,const char *url,uint8_t **body,size_t *length,char mime[96],unsigned *status);
#endif
