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
            "Read-only phone directory, including system settings. MMF/picture/JAR import is not implemented yet.",
            app.fs_directory.count,app.fs_directory.slots,(unsigned long long)app.fs_directory.listed_bytes);
        SetWindowTextA(app.fs_status,status);
    }
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_DIRECTORY),!*app.fs_directory.error);
    EnableWindow(GetDlgItem(app.fs_window,ID_FS_BACKUP),app.m.loaded&&app.m.nvram_data!=NULL);
}
static LRESULT CALLBACK filesystem_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_CREATE){LVCOLUMNA column={0};unsigned i;const char *names[]={"Name","File ID","Type","Bytes"};int widths[]={240,90,80,110};
        app.fs_window=window;
        app.fs_list=CreateWindowExA(WS_EX_CLIENTEDGE,WC_LISTVIEWA,"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL,
            12,12,536,250,window,NULL,GetModuleHandleA(NULL),NULL);
        ListView_SetExtendedListViewStyle(app.fs_list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        column.mask=LVCF_TEXT|LVCF_WIDTH;
        for(i=0;i<4;++i){column.pszText=(LPSTR)names[i];column.cx=widths[i];ListView_InsertColumn(app.fs_list,(int)i,&column);}
        app.fs_status=CreateWindowA("STATIC","",WS_CHILD|WS_VISIBLE,12,272,536,70,window,NULL,NULL,NULL);
        CreateWindowA("BUTTON","Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,350,100,28,window,(HMENU)ID_FS_REFRESH,NULL,NULL);
        CreateWindowA("BUTTON","Export directory...",WS_CHILD|WS_VISIBLE|WS_TABSTOP,122,350,150,28,window,(HMENU)ID_FS_DIRECTORY,NULL,NULL);
        CreateWindowA("BUTTON","Back up NVM...",WS_CHILD|WS_VISIBLE|WS_TABSTOP,282,350,150,28,window,(HMENU)ID_FS_BACKUP,NULL,NULL);
        fs_refresh();return 0;
    }
    if(message==WM_COMMAND){unsigned id=LOWORD(wparam);
        if(id==ID_FS_REFRESH){fs_refresh();return 0;}
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
