/* Two-pane import queue and native phone filesystem. All callers hold emu_mutex. */
static const char *queue_status(int status)
{
    static const char *names[]={"Queued","Starting","Waiting for phone storage","Imported","Failed"};
    return status>=0&&status<5?names[status]:"Unknown";
}
static const char *content_kind(unsigned type)
{
    return type==6?"Sound":type==7?"Picture":type==10?"Java":"System";
}
static void fs_queue_refresh(void)
{
    unsigned i;if(!app.fs_queue)return;ListView_DeleteAllItems(app.fs_queue);
    for(i=0;i<app.import_count;++i){ImportItem *q=&app.imports[i];LVITEMA item={0};char bytes[32];
        const char *name=q->path,*p;for(p=q->path;*p;++p)if(*p=='/'||*p=='\\')name=p+1;
        item.mask=LVIF_TEXT;item.iItem=(int)i;item.pszText=(LPSTR)name;ListView_InsertItem(app.fs_queue,&item);
        ListView_SetItemText(app.fs_queue,(int)i,1,(LPSTR)content_kind(q->type));
        snprintf(bytes,sizeof(bytes),"%u",q->size);ListView_SetItemText(app.fs_queue,(int)i,2,bytes);
        ListView_SetItemText(app.fs_queue,(int)i,3,(LPSTR)(q->status==4?q->error:queue_status(q->status)));
    }
}
static int fs_busy(void)
{
    return app.content.active||(app.import_current<app.import_count&&
        (app.imports[app.import_current].status==1||app.imports[app.import_current].status==2));
}
static void fs_refresh(void)
{
    unsigned i,row=0;char status[320];if(!app.fs_list)return;ListView_DeleteAllItems(app.fs_list);
    if(!userfs_directory(&app.m,&app.fs_directory))SetWindowTextA(app.fs_status,app.fs_directory.error);
    else {
        for(i=0;i<app.fs_directory.count;++i){UserFile *file=&app.fs_directory.files[i];LVITEMA item={0};char text[32],name[13];unsigned j;
            if(!app.fs_system&&file->type!=6&&file->type!=7&&strncmp(file->name,"FFS_JAVA_",9))continue;
            strcpy(name,file->name);for(j=0;name[j];++j)if((unsigned char)name[j]<32||name[j]==127)name[j]='?';
            item.mask=LVIF_TEXT;item.iItem=(int)row;item.pszText=name;ListView_InsertItem(app.fs_list,&item);
            ListView_SetItemText(app.fs_list,(int)row,1,(LPSTR)content_kind(file->type));
            snprintf(text,sizeof(text),"%u",file->size);ListView_SetItemText(app.fs_list,(int)row,2,text);
            snprintf(text,sizeof(text),"%04X",file->id);ListView_SetItemText(app.fs_list,(int)row,3,text);++row;
        }
        snprintf(status,sizeof(status),"%u visible files; %u total records. Add files on the left, then Import queue >>.\r\n"
            "Imports use the phone's own storage. Large Java apps may need Extended Java buffers.\r\n"
            "Java games opens the native menu; importing does not guarantee an app will run.",row,app.fs_directory.count);
        SetWindowTextA(app.fs_status,status);
    }
    fs_queue_refresh();
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_DIRECTORY),!*app.fs_directory.error);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_BACKUP),app.m.loaded&&app.m.nvram_data!=NULL);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_IMPORT),app.import_count&&app.m.import_trigger&&app.storage.active&&!fs_busy()&&!app.import_running);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_JAVA),app.m.java_ready&&app.storage.active&&!fs_busy()&&!app.import_running);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_STOP),app.import_running);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_REMOVE),!app.import_running&&!fs_busy());
}
static uint32_t fs_java_registry(void)
{
    UserDirectory d;unsigned i;if(!userfs_directory(&app.m,&d))return 0;
    for(i=0;i<d.count;++i)if(!strcmp(d.files[i].name,"FFS_JAVA_03"))return d.files[i].id;
    return 0;
}
static void fs_start_next(void)
{
    ImportItem *q;unsigned i;
    if(!app.import_running)return;
    for(i=0;i<app.import_count;++i)if(!app.imports[i].status)break;
    if(i==app.import_count){app.import_running=0;app.state="Running - import queue complete";fs_refresh();return;}
    app.import_current=i;q=&app.imports[i];q->registry_id=fs_java_registry();
    content_import_release(&app.content);app.running=0;
    if(!load_model()||!content_import_open(&app.content,&app.m,q->path)){
        q->status=4;snprintf(q->error,sizeof(q->error),"%.170s",*app.content.error?app.content.error:app.m.error);
        app.import_running=0;fs_refresh();return;
    }
    strcpy(q->name,app.content.name);q->size=app.content.size;q->type=app.content.type;q->status=1;
    q->started=app.m.clock.nanoseconds;q->accepted=0;
    app.running=1;app.state="Running - importing queue";fs_refresh();update_ui();
}
static void fs_tick(void)
{
    ImportItem *q;UserDirectory d;unsigned i;int stored=0;
    if(app.import_current>=app.import_count)return;q=&app.imports[app.import_current];
    if(q->status!=1&&q->status!=2)return;
    if(app.m.clock.nanoseconds-q->started>UINT64_C(300000000000)){
        q->status=4;strcpy(q->error,"Phone did not commit this import within five emulated minutes");
        app.import_running=0;fs_refresh();return;
    }
    if(q->status!=2||app.m.cpu.instructions-q->accepted<8000000)return;
    if(!userfs_directory(&app.m,&d))return;
    for(i=0;i<d.count;++i){UserFile *f=&d.files[i];
        if(q->type==10){if(!strcmp(f->name,"FFS_JAVA_03")&&f->id!=q->registry_id)stored=1;}
        else if(!strcmp(f->name,q->name)&&f->type==q->type&&f->size==q->size)stored=1;
    }
    if(!stored)return;
    if(!save_storage()){app.import_running=0;return;}
    q->status=3;fs_refresh();fs_start_next();
}
static void fs_add_path(const char *path)
{
    ImportItem *q;WIN32_FILE_ATTRIBUTE_DATA data;const char *ext=strrchr(path,'.');
    if(app.import_count>=IMPORT_QUEUE_MAX||strlen(path)>=sizeof(app.imports[0].path))return;
    if(!GetFileAttributesExA(path,GetFileExInfoStandard,&data)||(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||data.nFileSizeHigh)return;
    q=&app.imports[app.import_count++];memset(q,0,sizeof(*q));strcpy(q->path,path);q->size=data.nFileSizeLow;
    q->type=ext&&!_stricmp(ext,".jar")?10:ext&&(!_stricmp(ext,".png")||!_stricmp(ext,".jpg")||!_stricmp(ext,".jpeg")||!_stricmp(ext,".gif")||!_stricmp(ext,".bmp"))?7:6;
}
static void fs_add_files(HWND window)
{
    OPENFILENAMEA dialog={0};char *paths=calloc(1,65536);char full[1024],*p;if(!paths)return;
    dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=paths;dialog.nMaxFile=65536;
    dialog.lpstrFilter="Phone content\0*.mmf;*.mid;*.imy;*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.jar\0All files\0*.*\0\0";
    dialog.lpstrTitle="Add content to the import list";dialog.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameA(&dialog)){
        p=paths+strlen(paths)+1;
        if(!*p)fs_add_path(paths);
        else while(*p){int n=snprintf(full,sizeof(full),"%s\\%s",paths,p);if(n>0&&(size_t)n<sizeof(full))fs_add_path(full);p+=strlen(p)+1;}
    }
    free(paths);fs_refresh();
}
static void fs_layout(HWND window)
{
    RECT r;int w,h,pane,i;GetClientRect(window,&r);w=r.right;h=r.bottom;pane=(w-36)/2;
    MoveWindow(app.fs_left_label,12,10,pane,20,TRUE);MoveWindow(app.fs_right_label,24+pane,10,pane,20,TRUE);
    MoveWindow(app.fs_queue,12,34,pane,h-162,TRUE);MoveWindow(app.fs_list,24+pane,34,pane,h-162,TRUE);
    MoveWindow(app.fs_status,12,h-118,w-24,62,TRUE);
    {const int ids[]={ID_FS_ADD,ID_FS_REMOVE,ID_FS_IMPORT,ID_FS_STOP,ID_FS_REFRESH,ID_FS_JAVA,ID_FS_DIRECTORY,ID_FS_BACKUP};
     const int widths[]={80,80,136,60,70,90,110,110};int x=12;
     for(i=0;i<8;++i){MoveWindow(GetDlgItem(window,ids[i]),x,h-44,widths[i],28,TRUE);x+=widths[i]+6;}}
    MoveWindow(GetDlgItem(window,ID_FS_SYSTEM),24+pane,h-142,pane,20,TRUE);
}
static LRESULT CALLBACK filesystem_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_CREATE){LVCOLUMNA col={0};unsigned i;const char *left[]={"Import list","Type","Bytes","Status"},*right[]={"Phone file","Type","Bytes","ID"};int widths[]={180,70,70,150};
        app.fs_window=window;
        app.fs_left_label=CreateWindowA("STATIC","Import list (bulk loading)",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,NULL,NULL);
        app.fs_right_label=CreateWindowA("STATIC","Phone user filesystem",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,NULL,NULL);
        app.fs_queue=CreateWindowExA(WS_EX_CLIENTEDGE,WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT,0,0,0,0,window,NULL,NULL,NULL);
        app.fs_list=CreateWindowExA(WS_EX_CLIENTEDGE,WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL,0,0,0,0,window,NULL,NULL,NULL);
        ListView_SetExtendedListViewStyle(app.fs_queue,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        ListView_SetExtendedListViewStyle(app.fs_list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
        col.mask=LVCF_TEXT|LVCF_WIDTH;
        for(i=0;i<4;++i){col.pszText=(LPSTR)left[i];col.cx=widths[i];ListView_InsertColumn(app.fs_queue,(int)i,&col);col.pszText=(LPSTR)right[i];ListView_InsertColumn(app.fs_list,(int)i,&col);}
        app.fs_status=CreateWindowA("STATIC","",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,NULL,NULL);
        {const char *labels[]={"Add files...","Remove","Import queue >>","Stop","Refresh","Java games","Export list...","Back up NVM..."};
         const int ids[]={ID_FS_ADD,ID_FS_REMOVE,ID_FS_IMPORT,ID_FS_STOP,ID_FS_REFRESH,ID_FS_JAVA,ID_FS_DIRECTORY,ID_FS_BACKUP};
         for(i=0;i<8;++i)CreateWindowA("BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,(HMENU)(UINT_PTR)ids[i],NULL,NULL);}
        CreateWindowA("BUTTON","Show system records",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,0,0,0,0,window,(HMENU)ID_FS_SYSTEM,NULL,NULL);
        SendMessageA(GetDlgItem(window,ID_FS_SYSTEM),BM_SETCHECK,app.fs_system?BST_CHECKED:BST_UNCHECKED,0);
        fs_layout(window);fs_refresh();DragAcceptFiles(window,TRUE);return 0;
    }
    if(message==WM_SIZE){fs_layout(window);return 0;}
    if(message==WM_GETMINMAXINFO){MINMAXINFO *info=(MINMAXINFO *)lparam;info->ptMinTrackSize.x=900;info->ptMinTrackSize.y=420;return 0;}
    if(message==WM_DROPFILES){HDROP drop=(HDROP)wparam;UINT n=DragQueryFileA(drop,0xffffffffu,NULL,0),i;char path[1024];
        for(i=0;i<n;++i)if(DragQueryFileA(drop,i,path,sizeof(path)))fs_add_path(path);DragFinish(drop);fs_refresh();return 0;}
    if(message==WM_COMMAND){unsigned id=LOWORD(wparam);
        if(id==ID_FS_ADD){fs_add_files(window);return 0;}
        if(id==ID_FS_REMOVE&&!app.import_running&&!fs_busy()){int i;
            for(i=(int)app.import_count-1;i>=0;--i)if(ListView_GetItemState(app.fs_queue,i,LVIS_SELECTED)&LVIS_SELECTED){
                memmove(&app.imports[i],&app.imports[i+1],(app.import_count-(unsigned)i-1)*sizeof(app.imports[0]));--app.import_count;}
            app.import_current=app.import_count;fs_refresh();return 0;}
        if(id==ID_FS_SYSTEM){app.fs_system=SendMessageA(GetDlgItem(window,id),BM_GETCHECK,0,0)==BST_CHECKED;fs_refresh();return 0;}
        if(id==ID_FS_REFRESH){fs_refresh();return 0;}
        if(id==ID_FS_STOP){app.import_running=0;fs_refresh();SetWindowTextA(app.fs_status,"Queue stopped. The active phone write is allowed to finish; pending files remain queued.");return 0;}
        if(id==ID_FS_IMPORT&&!fs_busy()&&!app.import_running){app.import_running=1;fs_start_next();return 0;}
        if(id==ID_FS_JAVA&&!app.import_running&&!fs_busy()){
            content_import_release(&app.content);app.running=0;app.state="Paused";update_ui();
            if(!load_model())return 0;
            if(!content_java_menu(&app.content,&app.m)){MessageBoxA(window,app.content.error,"SGHemu - Java games",MB_OK|MB_ICONERROR);return 0;}
            app.running=1;app.state="Running - opening Java games";update_ui();fs_refresh();return 0;
        }
        if(id==ID_FS_DIRECTORY||id==ID_FS_BACKUP){OPENFILENAMEA dialog={0};char path[1024]={0};int ok;fs_refresh();
            if(id==ID_FS_DIRECTORY&&*app.fs_directory.error)return 0;if(id==ID_FS_BACKUP&&!app.m.nvram_data)return 0;
            dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=path;dialog.nMaxFile=sizeof(path);
            dialog.lpstrFilter=id==ID_FS_DIRECTORY?"Directory listing (*.tsv)\0*.tsv\0\0":"NVM backup (*.snv)\0*.snv\0\0";
            dialog.lpstrDefExt=id==ID_FS_DIRECTORY?"tsv":"snv";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetSaveFileNameA(&dialog))return 0;ok=id==ID_FS_DIRECTORY?userfs_export_directory(&app.fs_directory,path):nvram_save(&app.m,path);
            if(!ok)MessageBoxA(window,id==ID_FS_BACKUP?app.m.error:"Could not export the directory.","SGHemu - filesystem export",MB_OK|MB_ICONERROR);return 0;
        }
    }
    if(message==WM_DESTROY){app.fs_window=app.fs_list=app.fs_status=app.fs_queue=app.fs_left_label=app.fs_right_label=NULL;return 0;}
    return DefWindowProcA(window,message,wparam,lparam);
}
