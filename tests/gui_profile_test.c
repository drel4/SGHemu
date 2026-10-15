/* Headless logging lifecycle test. No firmware, DLL, window or user settings. */
#define WinMain SGHemuTestWinMain
#include "../src/gui.c"
#undef WinMain
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int main(void)
{
    char root[1024],scratch[1200],missing[1300],text[8192];FILE *f,*original;size_t n;unsigned i;long marker;
    if(!GetCurrentDirectoryA(sizeof(root),root))return 2;
    snprintf(scratch,sizeof(scratch),"%s\\local\\profile-tests",root);
    CreateDirectoryA("local",NULL);CreateDirectoryA(scratch,NULL);if(!SetCurrentDirectoryA(scratch))return 2;
    CreateDirectoryA("build",NULL);snprintf(missing,sizeof(missing),"%s\\no-build-directory",scratch);CreateDirectoryA(missing,NULL);
    strcpy(app.m.name,"profile-fixture");app.m.clock.arm_hz=26000000;
    CHECK(apply_logging(1));CHECK(app.logging&&profile_enabled&&gui_profile.file&&app.m.network.profiling);
    original=gui_profile.file;marker=ftell(original);CHECK(marker>=0);
    for(i=0;i<20;++i)profile_sample(0x01151010);
    profile_sample(0x01222bb4);app.m.cpu.instructions=1000000;app.m.clock.nanoseconds=1000000000;
    gui_profile.last-=gui_profile.frequency;profile_emit(0);
    CHECK(SetCurrentDirectoryA(missing));CHECK(!apply_logging(1));
    CHECK(gui_profile.file==original&&app.logging&&profile_enabled);
    CHECK(SetCurrentDirectoryA(scratch));CHECK(apply_logging(0));
    CHECK(!app.logging&&!profile_enabled&&!gui_profile.file&&!app.log&&!app.m.ma3.trace&&!app.m.network.profiling);
    f=fopen("build/gui-profile.log","rb");CHECK(f!=NULL);if(f){CHECK(fseek(f,marker,SEEK_SET)==0);n=fread(text,1,sizeof(text)-1,f);text[n]=0;fclose(f);
        CHECK(strstr(text,"instructions=1000000")!=NULL);CHECK(strstr(text,"speed_pct=")!=NULL);
        CHECK(strstr(text,"guest_pc_samples 01151010:20 01222bb4:1")!=NULL);
        CHECK(strstr(text,"audio_generate_wait_ms=")!=NULL&&strstr(text,"net_lock_wait_ms=")!=NULL);}
    CHECK(apply_logging(1));CHECK(ftell(gui_profile.file)>marker);CHECK(apply_logging(0));CHECK(SetCurrentDirectoryA(root));
    /* A clear followed by a redraw must not become a ghosting target. */
    {uint32_t live[2]={0xffffff,0xffffff},target[2]={0xffffff,0xffffff},ghost[2]={0xffffff,0xffffff};
     uint32_t published[2]={0xffffff,0xffffff},display[2]={0xffffff,0xffffff};
     app.m.width=2;app.m.height=1;app.m.pixels=live;app.ghost_target=target;app.ghost_pixels=ghost;
     app.ghost_published=published;app.display_pixels=display;app.display_width=2;app.display_height=1;
     app.ghosting=1;app.m.clock.attached=1;app.m.clock.nanoseconds=1000000;
     live[0]=live[1]=0;app.m.lcd.writes=1;ghost_capture();CHECK(target[0]==0xffffff);
     app.m.clock.nanoseconds=2000000;ghost_capture();CHECK(target[0]==0xffffff);
     live[0]=live[1]=0x123456;app.m.lcd.writes=2;ghost_capture();CHECK(target[0]==0xffffff);
     app.m.clock.nanoseconds=4999999;ghost_capture();CHECK(target[0]==0xffffff);
     app.m.clock.nanoseconds=5000000;ghost_capture();CHECK(published[0]==0x123456&&target[0]==0xffffff&&!app.ghost_pending);
     app.ghost_tick=GetTickCount()-75;presentation_tick(0);CHECK(target[0]==0x123456&&ghost[0]!=0xffffff&&ghost[0]!=0x123456);
     CHECK(display[0]==ghost[0]&&!app.ghost_published_ready);
     app.ghost_tick=GetTickCount()-75;presentation_tick(0);CHECK(display[0]!=0xffffff&&display[0]!=0x123456);
     /* No CPU mutex or live guest buffer is needed for raw presentation either. */
     app.ghosting=0;published[0]=0xabcdef;app.ghost_published_ready=1;app.m.pixels=NULL;
     presentation_tick(0);CHECK(display[0]==0xabcdef&&target[0]==0xabcdef);
     app.m.pixels=live;
     for(i=0;i<40;++i){live[0]=i;app.m.clock.nanoseconds=10000000+(uint64_t)i*1000000;
         ++app.m.lcd.writes;ghost_capture();}
     CHECK(published[0]>=33&&app.ghost_published_ready); /* continuous redraw still publishes */
     app.m.pixels=app.ghost_target=app.ghost_pixels=app.ghost_published=app.display_pixels=NULL;
     app.display_width=app.display_height=0;app.ghosting=0;}
    /* Hidden native controls: no firmware/DLL, no user settings or desktop input. */
    {INITCOMMONCONTROLSEX common={sizeof(common),ICC_BAR_CLASSES};char state[80];RECT key,bar;
     InitCommonControlsEx(&common);app.window=CreateWindowExA(0,"STATIC","SGHemu status fixture",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,300,650,NULL,NULL,GetModuleHandleA(NULL),NULL);
     CHECK(app.window!=NULL);app.status=CreateWindowExA(0,STATUSCLASSNAMEA,"",WS_CHILD|WS_VISIBLE,
        0,0,0,0,app.window,NULL,NULL,NULL);CHECK(app.status!=NULL);
     for(i=0;i<SGH_MAX_KEYS;++i)app.buttons[i]=CreateWindowA("BUTTON","Key",WS_CHILD|WS_VISIBLE,
        0,0,0,0,app.window,NULL,NULL,NULL);
     SetMenu(app.window,make_menu());app.m.loaded=1;app.m.width=app.m.height=128;app.state="Paused";
     for(i=1;i<=3;++i){app.zoom=i;layout();fit_window();GetWindowRect(app.buttons[20],&key);GetWindowRect(app.status,&bar);
        CHECK(key.bottom<=bar.top&&bar.bottom>bar.top);}
     app.running=0;status_refresh(1);SendMessageA(app.status,SB_GETTEXTA,0,(LPARAM)state);CHECK(!strcmp(state,"Paused"));
     app.running=1;status_refresh(1);SendMessageA(app.status,SB_GETTEXTA,0,(LPARAM)state);CHECK(!strcmp(state,"Running"));
     app.m.cpu.halted=1;status_refresh(1);SendMessageA(app.status,SB_GETTEXTA,0,(LPARAM)state);CHECK(!strcmp(state,"Stopped (CPU)"));
     command(ID_MA_WATER);CHECK(app.ma_water&&app.m.ma3.water);
     CHECK(GetMenuState(GetMenu(app.window),ID_MA_WATER,MF_BYCOMMAND)&MF_CHECKED);
     SendMessageA(app.status,SB_GETTEXTA,0,(LPARAM)state);CHECK(strstr(state,"Water")!=NULL);
     command(ID_MA_WATER);CHECK(!app.ma_water&&!app.m.ma3.water);
     app.m.loaded=0;status_refresh(1);SendMessageA(app.status,SB_GETTEXTA,0,(LPARAM)state);CHECK(!strcmp(state,"No firmware"));
     DestroyMenu(GetMenu(app.window));SetMenu(app.window,NULL);DestroyWindow(app.window);app.status=app.window=NULL;}
    printf("GUI profiling/logging/status lifecycle: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
