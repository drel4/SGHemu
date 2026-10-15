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
     app.m.width=2;app.m.height=1;app.m.pixels=live;app.ghost_target=target;app.ghost_pixels=ghost;
     app.ghosting=1;app.m.clock.attached=1;app.m.clock.nanoseconds=1000000;
     live[0]=live[1]=0;app.m.lcd.writes=1;ghost_capture();CHECK(target[0]==0xffffff);
     app.m.clock.nanoseconds=2000000;ghost_capture();CHECK(target[0]==0xffffff);
     live[0]=live[1]=0x123456;app.m.lcd.writes=2;ghost_capture();CHECK(target[0]==0xffffff);
     app.m.clock.nanoseconds=4999999;ghost_capture();CHECK(target[0]==0xffffff);
     app.m.clock.nanoseconds=5000000;ghost_capture();CHECK(target[0]==0x123456&&!app.ghost_pending);
     app.ghost_tick=GetTickCount()-75;ghost_frame();CHECK(ghost[0]!=0xffffff&&ghost[0]!=0x123456);
     app.m.pixels=app.ghost_target=app.ghost_pixels=NULL;app.ghosting=0;}
    printf("GUI profiling/logging lifecycle: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
