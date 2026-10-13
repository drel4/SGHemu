/* Native filesystem inspector, included after the frontend App declaration. */
static void fs_refresh(void)
{
    unsigned i;char status[320];
    if(!app.fs_list)return;
    ListView_DeleteAllItems(app.fs_list);
    if(!userfs_directory(&app.m,&app.fs_directory))SetWindowTextA(app.fs_status,app.fs_directory.error);
    else {
        for(i=0;i<app.fs_directory.count;++i){UserFile *file=&app.fs_directory.files[i];LVITEMA item={0};char text[32],name[13];unsigned j;
            strcpy(name,file->name);for(j=0;name[j];++j)if((unsigned char)name[j]<32||name[j]==127)name[j]='?';
            item.mask=LVIF_TEXT;item.iItem=(int)i;item.pszText=name;ListView_InsertItem(app.fs_list,&item);
            snprintf(text,sizeof(text),"%04X",file->id);ListView_SetItemText(app.fs_list,(int)i,1,text);
            snprintf(text,sizeof(text),"%u",file->type);ListView_SetItemText(app.fs_list,(int)i,2,text);
            snprintf(text,sizeof(text),"%u",file->size);ListView_SetItemText(app.fs_list,(int)i,3,text);
        }
        snprintf(status,sizeof(status),"%u files / %u directory slots; %llu listed bytes (not free-space quota).\r\n"
            "Includes system settings. Import content... adds sounds, pictures or MIDP 1.0 games.\r\nPictures are fitted to 128 x 128; animated images use their first frame.",
            app.fs_directory.count,app.fs_directory.slots,(unsigned long long)app.fs_directory.listed_bytes);
        SetWindowTextA(app.fs_status,status);
    }
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_DIRECTORY),!*app.fs_directory.error);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_BACKUP),app.m.loaded&&app.m.nvram_data!=NULL);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_IMPORT),app.m.loaded&&app.m.import_trigger!=0&&app.storage.active&&!app.content.active);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_JAVA),app.m.loaded&&app.m.java_ready!=0&&app.storage.active&&!app.content.active);
}
static LRESULT CALLBACK filesystem_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_CREATE){LVCOLUMNA column={0};unsigned i;const char *names[]={"Name","File ID","Type","Bytes"};int widths[]={240,90,80,110};
        app.fs_window=window;
        app.fs_list=CreateWindowExA(WS_EX_CLIENTEDGE,WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL,
            12,12,600,250,window,NULL,GetModuleHandleA(NULL),NULL);
        ListView_SetExtendedListViewStyle(app.fs_list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        column.mask=LVCF_TEXT|LVCF_WIDTH;
        for(i=0;i<4;++i){column.pszText=(LPSTR)names[i];column.cx=widths[i];ListView_InsertColumn(app.fs_list,(int)i,&column);}
        app.fs_status=CreateWindowA("STATIC","",WS_CHILD|WS_VISIBLE,12,272,600,70,window,NULL,NULL,NULL);
        CreateWindowA("BUTTON","Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,350,100,28,window,(HMENU)ID_FS_REFRESH,NULL,NULL);
        CreateWindowA("BUTTON","Export directory...",WS_CHILD|WS_VISIBLE|WS_TABSTOP,122,350,150,28,window,(HMENU)ID_FS_DIRECTORY,NULL,NULL);
        CreateWindowA("BUTTON","Back up NVM...",WS_CHILD|WS_VISIBLE|WS_TABSTOP,282,350,150,28,window,(HMENU)ID_FS_BACKUP,NULL,NULL);
        CreateWindowA("BUTTON","Import content...",WS_CHILD|WS_VISIBLE|WS_TABSTOP,442,350,150,28,window,(HMENU)ID_FS_IMPORT,NULL,NULL);
        CreateWindowA("BUTTON","Java games",WS_CHILD|WS_VISIBLE|WS_TABSTOP,442,386,150,28,window,(HMENU)ID_FS_JAVA,NULL,NULL);
        fs_refresh();return 0;
    }
    if(message==WM_COMMAND){unsigned id=LOWORD(wparam);
        if(id==ID_FS_REFRESH){fs_refresh();return 0;}
        if(id==ID_FS_JAVA){
            content_import_release(&app.content);app.running=0;app.state="Paused";update_ui();
            if(!load_model())return 0;
            if(!content_java_menu(&app.content,&app.m)){
                MessageBoxA(window,app.content.error,"SGHemu - Java games",MB_OK|MB_ICONERROR);return 0;}
            app.running=1;app.state="Running - opening Java games";update_ui();fs_refresh();return 0;
        }
        if(id==ID_FS_IMPORT){OPENFILENAMEA dialog={0};char path[1024]={0},status[180];
            if(!app.m.loaded||!app.m.import_trigger||!app.storage.active){
                MessageBoxA(window,"This model has no persistent phone storage or declares no import guest entries.","SGHemu - import content",MB_OK|MB_ICONINFORMATION);
                return 0;
            }
            dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=path;dialog.nMaxFile=sizeof(path);
            dialog.lpstrFilter="Phone content\0*.mmf;*.mid;*.imy;*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.jar\0Java games (MIDP 1.0)\0*.jar\0Pictures\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0Sounds\0*.mmf;*.mid;*.imy\0\0";
            dialog.lpstrTitle="Import content into the phone";
            dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetOpenFileNameA(&dialog))return 0;
            /* A fresh boot is the only reliable way to reach the post-initialization
               trigger; the emulation thread then drives the guest's own calls. */
            content_import_release(&app.content);
            app.state="Paused";update_ui();
            if(!load_model())return 0;
            if(!content_import_open(&app.content,&app.m,path)){
                MessageBoxA(window,app.content.error,"SGHemu - import content",MB_OK|MB_ICONERROR);
                return 0;
            }
            app.running=1;app.state="Running - importing into the phone";
            snprintf(status,sizeof(status),"Import armed for \"%s\"; the guest creates the entry during this boot.",app.content.name);
            SetWindowTextA(app.fs_status,status);
            update_ui();fs_refresh();
            return 0;
        }
        if(id==ID_FS_DIRECTORY||id==ID_FS_BACKUP){OPENFILENAMEA dialog={0};char path[1024]={0};int ok;
            fs_refresh();
            if(id==ID_FS_DIRECTORY&&*app.fs_directory.error)return 0;
            if(id==ID_FS_BACKUP&&!app.m.nvram_data)return 0;
            dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=path;dialog.nMaxFile=sizeof(path);
            dialog.lpstrFilter=id==ID_FS_DIRECTORY?"Directory listing (*.tsv)\0*.tsv\0\0":"NVM backup (*.snv)\0*.snv\0\0";
            dialog.lpstrDefExt=id==ID_FS_DIRECTORY?"tsv":"snv";
            dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetSaveFileNameA(&dialog))return 0;
            ok=id==ID_FS_DIRECTORY?userfs_export_directory(&app.fs_directory,path):nvram_save(&app.m,path);
            if(!ok)MessageBoxA(window,id==ID_FS_BACKUP?app.m.error:"Could not export the directory.","SGHemu - filesystem export",MB_OK|MB_ICONERROR);
            return 0;
        }
    }
    if(message==WM_DESTROY){app.fs_window=app.fs_list=app.fs_status=NULL;return 0;}
    return DefWindowProcA(window,message,wparam,lparam);
}
