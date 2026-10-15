#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include "wap_gateway.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HTTP_LIMIT (1024u*1024u)
void wap_http_cancel(WapHttpContext *c)
{
    HINTERNET request;InterlockedExchange(&c->canceled,1);
    request=InterlockedExchangePointer(&c->active,NULL);if(request)WinHttpCloseHandle(request);
}
int wap_http_fetch(void *user,const char *url,uint8_t **body,size_t *length,char mime[96],unsigned *status)
{
    WapHttpContext *ctx=user;WCHAR wide[2048],host[256],path[2048],type[256];URL_COMPONENTS parts={0};
    HINTERNET session=NULL,connection=NULL,request=NULL;uint8_t *bytes=NULL;size_t n=0;DWORD code=0,size,available,received;int ok=0;
    *body=NULL;*length=0;*status=502;
    if(ctx&&InterlockedCompareExchange(&ctx->canceled,0,0))return 0;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url,-1,wide,2048))return 0;
    parts.dwStructSize=sizeof(parts);parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(wide,0,0,&parts)||(parts.nScheme!=INTERNET_SCHEME_HTTP&&parts.nScheme!=INTERNET_SCHEME_HTTPS)||parts.dwHostNameLength>=256||parts.dwUrlPathLength+parts.dwExtraInfoLength>=2048)return 0;
    memcpy(host,parts.lpszHostName,parts.dwHostNameLength*sizeof(WCHAR));host[parts.dwHostNameLength]=0;
    memcpy(path,parts.lpszUrlPath,parts.dwUrlPathLength*sizeof(WCHAR));memcpy(path+parts.dwUrlPathLength,parts.lpszExtraInfo,parts.dwExtraInfoLength*sizeof(WCHAR));path[parts.dwUrlPathLength+parts.dwExtraInfoLength]=0;
    {WCHAR *fragment=wcschr(path,L'#');if(fragment)*fragment=0;if(!*path)wcscpy(path,L"/");}
    session=WinHttpOpen(L"SGHemu built-in WAP gateway",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)goto done;
    WinHttpSetTimeouts(session,3000,3000,5000,5000);
    {DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1|WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1|WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
     (void)WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols));}
    connection=WinHttpConnect(session,host,parts.nPort,0);if(!connection)goto done;
    request=WinHttpOpenRequest(connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);if(!request)goto done;
    if(ctx){InterlockedExchangePointer(&ctx->active,request);if(InterlockedCompareExchange(&ctx->canceled,0,0))goto done;}
    if(!WinHttpSendRequest(request,L"Accept-Encoding: identity\r\n",(DWORD)-1,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,NULL))goto done;
    size=sizeof(code);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX))goto done;
    *status=code;strcpy(mime,"application/octet-stream");size=sizeof(type);
    if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,type,&size,WINHTTP_NO_HEADER_INDEX)){
        WCHAR *semi=wcschr(type,L';');if(semi)*semi=0;
        if(!WideCharToMultiByte(CP_UTF8,0,type,-1,mime,96,NULL,NULL))goto done;
    }
    bytes=malloc(HTTP_LIMIT+1);if(!bytes)goto done;
    for(;;){if(ctx&&InterlockedCompareExchange(&ctx->canceled,0,0))goto done;
        if(!WinHttpQueryDataAvailable(request,&available))goto done;if(!available)break;
        if(available>HTTP_LIMIT-n)goto done;
        if(!WinHttpReadData(request,bytes+n,available,&received)||!received)goto done;n+=received;
    }
    bytes[n]=0;*body=bytes;*length=n;bytes=NULL;ok=1;
done:
    free(bytes);if(request){if(!ctx||InterlockedCompareExchangePointer(&ctx->active,NULL,request)==request)WinHttpCloseHandle(request);}
    if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);return ok;
}
