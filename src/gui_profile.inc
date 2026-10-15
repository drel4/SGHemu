/* All ordinary fields are protected by emu_mutex. GUI skip counts are atomic.
   PC samples describe guest hotspots, not host function timings. */
#define PROFILE_BUCKETS 4096u
#define PROFILE_PROBES 8u
typedef struct {
    FILE *file;LONGLONG frequency,last;uint64_t instructions,ns,cpu;
    uint64_t slices,busy_ticks,max_slice,gui_wait,max_gui_wait;
    uint64_t tx,rx,net_calls,net_wait;Ma5HwProfile audio;
    uint32_t pc[PROFILE_BUCKETS];uint64_t hits[PROFILE_BUCKETS],samples,dropped;
} GuiProfile;
static GuiProfile gui_profile;
static volatile LONG profile_enabled,profile_skipped;
static uint64_t profile_cpu_time(void)
{
    FILETIME creation,exit,kernel,user;ULARGE_INTEGER k,u;
    HANDLE thread=emu_thread;if(!thread)return 0;
    if(!GetThreadTimes(thread,&creation,&exit,&kernel,&user))return 0;
    k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;
}
static void profile_emit(int force)
{
    GuiProfile *p=&gui_profile;LARGE_INTEGER now;Ma5HwProfile audio;uint64_t cpu;double seconds,ms;unsigned i,j;
    if(!p->file)return;QueryPerformanceCounter(&now);
    if(!force&&now.QuadPart-p->last<p->frequency)return;
    seconds=(double)(now.QuadPart-p->last)/p->frequency;if(seconds<=0)return;
    ms=1000.0/p->frequency;audio=ma5_hw_profile(app.m.ma3.hw);cpu=profile_cpu_time();
    fprintf(p->file,"wall_s=%.3f running=%d arm_hz=%u instructions=%llu mips=%.3f emu_s=%.3f speed_pct=%.1f cpu_ms=%.3f slices=%llu slice_ms=%.3f max_slice_ms=%.3f gui_wait_ms=%.3f max_gui_wait_ms=%.3f timer_skips=%ld audio_commands=%llu audio_generate_wait_ms=%.3f audio_other_wait_ms=%.3f net_locks=%llu net_lock_wait_ms=%.3f tx=%llu rx=%llu pc_samples=%llu pc_dropped=%llu\n",
        seconds,app.running,app.m.clock.arm_hz,(unsigned long long)(app.m.cpu.instructions-p->instructions),
        (app.m.cpu.instructions-p->instructions)/seconds/1000000.0,
        (app.m.clock.nanoseconds-p->ns)/1000000000.0,
        (app.m.clock.nanoseconds-p->ns)/seconds/10000000.0,
        cpu>=p->cpu?(cpu-p->cpu)/10000.0:0.0,
        (unsigned long long)p->slices,p->busy_ticks*ms,p->max_slice*ms,p->gui_wait*ms,p->max_gui_wait*ms,
        InterlockedExchange(&profile_skipped,0),(unsigned long long)(audio.commands-p->audio.commands),
        (audio.generate_ticks-p->audio.generate_ticks)*ms,(audio.other_ticks-p->audio.other_ticks)*ms,
        (unsigned long long)(app.m.network.lock_calls-p->net_calls),(app.m.network.lock_wait_ticks-p->net_wait)*ms,
        (unsigned long long)(app.m.network.transmitted-p->tx),(unsigned long long)(app.m.network.received-p->rx),
        (unsigned long long)p->samples,(unsigned long long)p->dropped);
    fputs("guest_pc_samples",p->file);
    for(j=0;j<8;++j){unsigned best=0;for(i=1;i<PROFILE_BUCKETS;++i)if(p->hits[i]>p->hits[best])best=i;
        if(!p->hits[best])break;fprintf(p->file," %08x:%llu",p->pc[best],(unsigned long long)p->hits[best]);p->hits[best]=0;}
    fputc('\n',p->file);fflush(p->file);
    p->last=now.QuadPart;p->instructions=app.m.cpu.instructions;p->ns=app.m.clock.nanoseconds;p->cpu=cpu;
    p->audio=audio;p->net_calls=app.m.network.lock_calls;p->net_wait=app.m.network.lock_wait_ticks;
    p->tx=app.m.network.transmitted;p->rx=app.m.network.received;
    p->slices=p->busy_ticks=p->max_slice=p->gui_wait=p->max_gui_wait=p->samples=p->dropped=0;
    memset(p->hits,0,sizeof(p->hits));
}
static void profile_close(void)
{
    InterlockedExchange(&profile_enabled,0);
    if(gui_profile.file){profile_emit(1);fclose(gui_profile.file);gui_profile.file=NULL;}
    ma5_hw_profile_enable(app.m.ma3.hw,0);app.m.network.profiling=0;
}
static void profile_start(FILE *file)
{
    LARGE_INTEGER frequency,now;GuiProfile *p=&gui_profile;memset(p,0,sizeof(*p));
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&now);
    p->file=file;p->frequency=frequency.QuadPart;p->last=now.QuadPart;p->cpu=profile_cpu_time();
    p->instructions=app.m.cpu.instructions;p->ns=app.m.clock.nanoseconds;
    p->tx=app.m.network.transmitted;p->rx=app.m.network.received;
    p->net_calls=app.m.network.lock_calls;p->net_wait=app.m.network.lock_wait_ticks;
    ma5_hw_profile_enable(app.m.ma3.hw,1);app.m.network.profiling=1;
    InterlockedExchange(&profile_skipped,0);InterlockedExchange(&profile_enabled,1);
    fputs("# SGHemu performance profile v1; interval approximately 1s\n"
          "# slice wall and audio waits overlap; cpu_ms is CPU thread kernel+user time.\n"
          "# Guest PC sampled once per 1024 slice instructions; counts are not cycles.\n",file);
    fprintf(file,"# model=%s clock_hz=%u audio_block=%u\n",app.m.name,app.m.clock.arm_hz,app.audio_block);
#ifdef NDEBUG
    fputs("# build=Release x86 /O2 /GL /LTCG /MT /DNDEBUG\n",file);
#else
    fputs("# build=Debug x86\n",file);
#endif
    fflush(file);
}
static void profile_sample(uint32_t pc)
{
    GuiProfile *p=&gui_profile;unsigned i,index=((pc>>1)*2654435761u)>>20;
    ++p->samples;
    for(i=0;i<PROFILE_PROBES;++i){unsigned slot=(index+i)&(PROFILE_BUCKETS-1);
        if(!p->hits[slot]||p->pc[slot]==pc){p->pc[slot]=pc;++p->hits[slot];return;}}
    ++p->dropped;
}
