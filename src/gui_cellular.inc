/* Host controls for the virtual carrier and the far end of outgoing calls. */
static void cell_save_settings(void)
{
    char mode[8];snprintf(mode,sizeof(mode),"%u",app.gsm_mode);CreateDirectoryA("state",NULL);
    WritePrivateProfileStringA("cellular","enabled",app.cell_enabled?"1":"0",app.emulator_settings);
    WritePrivateProfileStringA("cellular","network",app.cell_network,app.emulator_settings);
    WritePrivateProfileStringA("cellular","gsm_mode",mode,app.emulator_settings);
}
static LRESULT CALLBACK cell_editor_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_CREATE){int kind=app.cell_editor_kind;app.cell_editor=window;
        CreateWindowA("STATIC",kind==1?"Network name:":kind==2?"Sender number:":"Caller number:",WS_CHILD|WS_VISIBLE,12,12,350,20,window,NULL,NULL,NULL);
        CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT",kind==1?app.cell_network:"+79990001122",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,12,36,370,24,window,(HMENU)10,NULL,NULL);
        SendMessageA(GetDlgItem(window,10),EM_SETLIMITTEXT,kind==1?30:30,0);
        if(kind==2){CreateWindowA("STATIC","Message (up to 70 characters):",WS_CHILD|WS_VISIBLE,12,72,370,20,window,NULL,NULL,NULL);
            CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,12,96,370,120,window,(HMENU)11,NULL,NULL);
            SendMessageW(GetDlgItem(window,11),EM_SETLIMITTEXT,70,0);}
        CreateWindowA("BUTTON",kind==1?"Apply":kind==2?"Send SMS":"Call phone",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,kind==2?230:78,160,28,window,(HMENU)1,NULL,NULL);
        CreateWindowA("BUTTON","Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,184,kind==2?230:78,100,28,window,(HMENU)2,NULL,NULL);return 0;
    }
    if(message==WM_COMMAND&&LOWORD(wparam)==1){char value[64];int ok=0;GetDlgItemTextA(window,10,value,sizeof(value));emu_lock();
        if(app.cell_editor_kind==1){ok=cellular_set_network(&app.m.cellular,&app.m.bus,app.cell_enabled,value);
            if(ok){strcpy(app.cell_network,value);cell_save_settings();}}
        else if(app.cell_editor_kind==3)ok=cellular_call(&app.m.cellular,&app.m.bus,value);
        else {WCHAR text[71];SYSTEMTIME now;TIME_ZONE_INFORMATION zone;DWORD zone_id;LONG bias;uint8_t timestamp[7];unsigned parts[7],i;GetDlgItemTextW(window,11,text,71);GetLocalTime(&now);zone_id=GetTimeZoneInformation(&zone);bias=zone.Bias+(zone_id==TIME_ZONE_ID_DAYLIGHT?zone.DaylightBias:zone.StandardBias);
            parts[0]=now.wYear%100;parts[1]=now.wMonth;parts[2]=now.wDay;parts[3]=now.wHour;parts[4]=now.wMinute;parts[5]=now.wSecond;parts[6]=(unsigned)((bias<0?-bias:bias)/15);
            for(i=0;i<7;++i)timestamp[i]=(uint8_t)(((parts[i]%10)<<4)|(parts[i]/10));
            if(bias>0)timestamp[6]|=8;
            ok=cellular_sms(&app.m.cellular,value,(const uint16_t *)text,(unsigned)wcslen(text),timestamp);}
        if(ok){app.running=1;app.state="Running";}else MessageBoxA(window,app.m.cellular.error,"SGHemu - Cellular",MB_OK|MB_ICONINFORMATION);
        update_ui();emu_unlock();if(ok)DestroyWindow(window);return 0;
    }
    if(message==WM_COMMAND&&LOWORD(wparam)==2){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){app.cell_editor=NULL;return 0;}
    return DefWindowProcA(window,message,wparam,lparam);
}
static void cell_open_editor(int kind)
{
    if(app.cell_editor){SetForegroundWindow(app.cell_editor);return;}app.cell_editor_kind=kind;
    CreateWindowExA(0,"SGHemuCellular",kind==1?"SGHemu - Network name":kind==2?"SGHemu - Send SMS to phone":"SGHemu - Call phone",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,410,kind==2?310:150,app.window,NULL,GetModuleHandleA(NULL),NULL);
}
static LRESULT CALLBACK call_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_CREATE){app.call_window=window;
        app.call_label=CreateWindowA("STATIC","",WS_CHILD|WS_VISIBLE,12,12,350,65,window,NULL,NULL,NULL);
        CreateWindowA("BUTTON","Accept",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,84,110,28,window,(HMENU)1,NULL,NULL);
        CreateWindowA("BUTTON","Decline",WS_CHILD|WS_VISIBLE|WS_TABSTOP,134,84,110,28,window,(HMENU)2,NULL,NULL);return 0;}
    if((message==WM_COMMAND&&(LOWORD(wparam)==1||LOWORD(wparam)==2))||message==WM_CLOSE){
        int accept=message==WM_COMMAND&&LOWORD(wparam)==1;emu_lock();
        if(!cellular_answer(&app.m.cellular,accept))MessageBoxA(window,app.m.cellular.error,"SGHemu - Call",MB_OK|MB_ICONINFORMATION);
        app.running=1;update_ui();emu_unlock();return 0;
    }
    if(message==WM_DESTROY){app.call_window=app.call_label=NULL;return 0;}return DefWindowProcA(window,message,wparam,lparam);
}
static void cell_tick(void)
{
    Cellular *s=&app.m.cellular;char text[200];
    if(app.cell_revision==s->revision)return;app.cell_revision=s->revision;
    if(!s->outgoing||!s->call_state){if(app.call_window)DestroyWindow(app.call_window);return;}
    if(!app.call_window)CreateWindowExA(0,"SGHemuCall","SGHemu - Incoming call from phone",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,380,160,app.window,NULL,GetModuleHandleA(NULL),NULL);
    snprintf(text,sizeof(text),"%s\r\nDialed number: %s",s->call_state==CELL_CONNECTED?"Call connected":"Incoming call from the emulated phone",s->number);
    SetWindowTextA(app.call_label,text);SetWindowTextA(GetDlgItem(app.call_window,1),s->call_state==CELL_CONNECTED?"Hang up":"Accept");
    ShowWindow(GetDlgItem(app.call_window,2),s->call_state==CELL_CONNECTED?SW_HIDE:SW_SHOW);
}
