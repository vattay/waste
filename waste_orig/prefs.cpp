/*
    WASTE - prefs.cpp (Preferences Dialogs)
    Copyright (C) 2003 Nullsoft, Inc.

    WASTE is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    WASTE  is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with WASTE; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "main.h"
#include "resource.h"
#include "sha.h"

#include "childwnd.h"
#include "d_chat.h"

#include "prefs.h"
#include "util.h"
#include "srchwnd.h"
#include "netq.h"
#include "netkern.h"

static COLORREF custcolors[16];

static int CALLBACK WINAPI BrowseCallbackProc( HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg==BFFM_INITIALIZED)
	{
		char buf[1024];
		if (lpData) GetWindowText((HWND)lpData,buf,sizeof(buf));
    SetWindowText(hwnd,"Choose directory");
		SendMessage(hwnd,BFFM_SETSELECTION,(WPARAM)1,(LPARAM)buf);
	}
	return 0;
}

static void getProfilePath(char *tmp)
{
  GetModuleFileName(g_hInst,tmp,1024);
  char *p=tmp;
  while (*p) p++;
  while (p >= tmp && *p != '\\') p--;
  p[1]=0;
}


///////////////////////////////////// prefs ////////////////////////////////
HWND prefs_hwnd,prefs_cur_wnd;	
static int prefs_last_page;


static HTREEITEM lp_v=NULL;
static HTREEITEM _additem(HWND hwnd, HTREEITEM h, char *str, int children, int data)
{
	HTREEITEM h2;
	TV_INSERTSTRUCT is={h,TVI_LAST,{TVIF_PARAM|TVIF_TEXT|TVIF_CHILDREN,0,0,0,str,strlen(str),0,0,children?1:0,data}};
	h2=TreeView_InsertItem(hwnd,&is);
	if (prefs_last_page == data) lp_v=h2;
	return h2;
}

static BOOL CALLBACK Pref_FilesProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {  
    if (g_config->ReadInt("aotransfer",1))
      CheckDlgButton(hwndDlg,IDC_CHECK4,BST_CHECKED);
    else EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK5),0);
    if (g_config->ReadInt("aotransfer_btf",1))
      CheckDlgButton(hwndDlg,IDC_CHECK5,BST_CHECKED);
    if (g_config->ReadInt("aoupload",1))
      CheckDlgButton(hwndDlg,IDC_CHECK6,BST_CHECKED);
    else EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK7),0);
    if (g_config->ReadInt("aoupload_btf",1))
      CheckDlgButton(hwndDlg,IDC_CHECK7,BST_CHECKED);
    if (g_config->ReadInt("aorecv",1))
      CheckDlgButton(hwndDlg,IDC_CHECK8,BST_CHECKED);
    else EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK9),0);
    if (g_config->ReadInt("aorecv_btf",0))
      CheckDlgButton(hwndDlg,IDC_CHECK9,BST_CHECKED);  

    if (g_config->ReadInt("nickonxfers",1))
      CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);  
    if (g_config->ReadInt("directxfers",0))
      CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);  
      
  }
  if (uMsg == WM_COMMAND)
  {
    int a;
    switch (LOWORD(wParam))
    {
      case IDC_CHECK1:
        g_config->WriteInt("nickonxfers",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1));
      break;
      case IDC_CHECK2:
        g_config->WriteInt("directxfers",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK2));
      break;
      case IDC_CHECK4:
        g_config->WriteInt("aotransfer",a=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK4));
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK5),a);
      break;      
      case IDC_CHECK5:
        g_config->WriteInt("aotransfer_btf",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK5));
      break;
      case IDC_CHECK6:
        g_config->WriteInt("aoupload",a=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK6));
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK7),a);
      break;      
      case IDC_CHECK7:
        g_config->WriteInt("aoupload_btf",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK7));
      break;
      case IDC_CHECK8:
        g_config->WriteInt("aorecv",a=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK8));
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK9),a);
      break;      
      case IDC_CHECK9:
        g_config->WriteInt("aorecv_btf",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK9));
      break;
      
    }
  }
  return 0;
}

static BOOL CALLBACK Pref_ThrottleProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SetDlgItemInt(hwndDlg,IDC_LIMITOB,g_throttle_send,FALSE);
    SetDlgItemInt(hwndDlg,IDC_LIMITIB,g_throttle_recv,FALSE);
    if (g_throttle_flag&1)
      CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);
    else
    {
      EnableWindow(GetDlgItem(hwndDlg,IDC_LIMITIB),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO1),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO2),0);
    }
    if (g_throttle_flag&2)
      CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    else 
    {
      EnableWindow(GetDlgItem(hwndDlg,IDC_LIMITOB),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO3),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO4),0);
    }
    CheckDlgButton(hwndDlg,(g_throttle_flag&4)?IDC_RADIO2:IDC_RADIO1,BST_CHECKED);
    CheckDlgButton(hwndDlg,(g_throttle_flag&8)?IDC_RADIO4:IDC_RADIO3,BST_CHECKED);
    if (g_throttle_flag&16) CheckDlgButton(hwndDlg,IDC_SAT_INC,BST_CHECKED);
    if (g_throttle_flag&32) CheckDlgButton(hwndDlg,IDC_SAT_OUT,BST_CHECKED);
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_CHECK2:
      case IDC_CHECK3:
      case IDC_RADIO1:
      case IDC_RADIO2:
      case IDC_RADIO3:
      case IDC_RADIO4:
      case IDC_SAT_INC:
      case IDC_SAT_OUT:
        {
          int otf=g_throttle_flag;
          g_throttle_flag=(IsDlgButtonChecked(hwndDlg,IDC_CHECK2)?1:0)|
                          (IsDlgButtonChecked(hwndDlg,IDC_CHECK3)?2:0)|
                          (IsDlgButtonChecked(hwndDlg,IDC_RADIO2)?4:0)|
                          (IsDlgButtonChecked(hwndDlg,IDC_RADIO4)?8:0)|
                          (IsDlgButtonChecked(hwndDlg,IDC_SAT_INC)?16:0)|
                          (IsDlgButtonChecked(hwndDlg,IDC_SAT_OUT)?32:0);
          EnableWindow(GetDlgItem(hwndDlg,IDC_LIMITIB),g_throttle_flag&1);
          EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO1),g_throttle_flag&1);
          EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO2),g_throttle_flag&1);
          EnableWindow(GetDlgItem(hwndDlg,IDC_LIMITOB),(g_throttle_flag&2)>>1);
          EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO3),(g_throttle_flag&2)>>1);
          EnableWindow(GetDlgItem(hwndDlg,IDC_RADIO4),(g_throttle_flag&2)>>1);
          if ((otf&48)!=(g_throttle_flag&48))
          {
            // rebroadcast our new prefs
            RebroadcastCaps(g_mql);
          }
          g_config->WriteInt("throttleflag",g_throttle_flag);
        }
      break;
      case IDC_LIMITOB:
        if (HIWORD(wParam) == EN_CHANGE)
        {
          BOOL t;
          int a=GetDlgItemInt(hwndDlg,IDC_LIMITOB,&t,FALSE);
          if (t) g_config->WriteInt("throttlesend",g_throttle_send=a);
        }
      break;
      case IDC_LIMITIB:
        if (HIWORD(wParam) == EN_CHANGE)
        {
          BOOL t;
          int a=GetDlgItemInt(hwndDlg,IDC_LIMITIB,&t,FALSE);
          if (t) g_config->WriteInt("throttlerecv",g_throttle_recv=a);
        }
      break;
    }    
  }
  return 0;
}

static BOOL CALLBACK Pref_ProfilesProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SetDlgItemText(hwndDlg,IDC_EDIT1,g_profile_name);
    if (g_appendprofiletitles)
      CheckDlgButton(hwndDlg,IDC_CHECK6,BST_CHECKED);
    if (GetPrivateProfileInt("config","showprofiles",0,g_config_mainini))
      CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
  }      
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_BUTTON1:
        g_config->Flush();
        {
          PROCESS_INFORMATION ProcInfo={0,};
          STARTUPINFO StartUp={0,};
          StartUp.cb=sizeof(StartUp);
          char tmp[1024];
          tmp[0]='\"';
          GetModuleFileName(g_hInst,tmp+1,sizeof(tmp)-32);
          strcat(tmp,"\" /profile=\"\"");
          CreateProcess(NULL,tmp,NULL,NULL,FALSE,0,NULL,NULL,&StartUp, &ProcInfo);
        }
      break;
      case IDC_CHECK1:
        WritePrivateProfileString("config","showprofiles",IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?"1":"0",g_config_mainini);
      break;
      case IDC_CHECK6:
        g_config->WriteInt("appendpt",g_appendprofiletitles=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK6));
        SetWndTitle(GetParent(hwndDlg),APP_NAME " Preferences");
        SendMessage(g_mainwnd,WM_USER_TITLEUPDATE,0,0);
      break;      
    }
  }
  return 0;
}

static BOOL CALLBACK Pref_DisplayProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    if (g_config->ReadInt("confirmquit",1))      
      CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    if (g_config->ReadInt("systray",1))
      CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
    if (g_config->ReadInt("toolwindow",0))
      CheckDlgButton(hwndDlg,IDC_CHECK10,BST_CHECKED);   
    if (g_config->ReadInt("systray_hide",1))
      CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);
    if (g_config->ReadInt("srchcb_use",0))
      CheckDlgButton(hwndDlg,IDC_CHECK7,BST_CHECKED);

    if (g_search_showfull==1)
      CheckDlgButton(hwndDlg,IDC_SRF,BST_CHECKED);    
    else if (g_search_showfull==2)
      CheckDlgButton(hwndDlg,IDC_SRP,BST_CHECKED);    
    else 
      CheckDlgButton(hwndDlg,IDC_SRN,BST_CHECKED);    

    if (!g_search_showfullbytes)
      CheckDlgButton(hwndDlg,IDC_SFB,BST_CHECKED);    

  
    if (g_extrainf)
      CheckDlgButton(hwndDlg,IDC_CHECK5,BST_CHECKED);

    if (g_config->ReadInt("hideallonmin",0))
      CheckDlgButton(hwndDlg,IDC_CHECK6,BST_CHECKED);
    
    
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_BUTTON4:
      case IDC_BUTTON7:
        {
				  CHOOSECOLOR cs;
				  cs.lStructSize = sizeof(cs);
				  cs.hwndOwner = hwndDlg;
				  cs.hInstance = 0;
          cs.rgbResult=g_config->ReadInt(LOWORD(wParam) == IDC_BUTTON4 ? "mul_bgc" : "mul_color",LOWORD(wParam) == IDC_BUTTON4 ? 0xffffff : 0);
				  cs.lpCustColors = custcolors;
				  cs.Flags = CC_RGBINIT|CC_FULLOPEN;
				  if (ChooseColor(&cs))
				  {
            g_config->WriteInt(LOWORD(wParam) == IDC_BUTTON4 ? "mul_bgc" : "mul_color",cs.rgbResult);
            SendMessage(g_mainwnd,WM_USER_TITLEUPDATE,0,0);
            InvalidateRect(hwndDlg,NULL,FALSE);
				  }
        }
      break;
      case IDC_CHECK7:
        g_config->WriteInt("srchcb_use",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK7));
      break;
      case IDC_CHECK1:
        {
          int s=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1);
          g_config->WriteInt("systray",s);
          if (!s != !systray_state)
          {
            if (s) systray_add(g_mainwnd,g_hSmallIcon);
            else systray_del(g_mainwnd);
          }
        }
      break;
      case IDC_CHECK10:
        {
          int s=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK10);
          g_config->WriteInt("toolwindow",s);
          toolWindowSet(s);
        }
      break;
      case IDC_CHECK2:
        g_config->WriteInt("systray_hide",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK2));
      break;
      case IDC_CHECK6:
        g_config->WriteInt("hideallonmin",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK6));
      break;
      case IDC_CHECK3:
        g_config->WriteInt("confirmquit",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK3));
      break;      
      case IDC_SFB:
        g_search_showfullbytes = !IsDlgButtonChecked(hwndDlg,IDC_SFB);
        g_config->WriteInt("search_showfullb",g_search_showfullbytes);
        Search_Resort();
      break;
      case IDC_SRF:
      case IDC_SRP:
      case IDC_SRN:
        if (IsDlgButtonChecked(hwndDlg,IDC_SRF)) g_search_showfull=1;
        else if (IsDlgButtonChecked(hwndDlg,IDC_SRP)) g_search_showfull=2;
        else g_search_showfull=0;
        g_config->WriteInt("search_showfull",g_search_showfull);
        Search_Resort();
      break;      
      case IDC_CHECK5:
        g_config->WriteInt("extrainf",g_extrainf=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK5));
      break;      
    }
  }
  if (uMsg == WM_DRAWITEM)
  {
		DRAWITEMSTRUCT *di=(DRAWITEMSTRUCT *)lParam;
		if (di->CtlID == IDC_BUTTON4 || di->CtlID == IDC_BUTTON7)
		{
      int color=g_config->ReadInt(di->CtlID == IDC_BUTTON4 ? "mul_bgc" : "mul_color",
                                  di->CtlID == IDC_BUTTON4 ? 0xffffff : 0);
	    HBRUSH hBrush,hOldBrush;
	    LOGBRUSH lb={BS_SOLID,color,0};
	    hBrush = CreateBrushIndirect(&lb);
	    hOldBrush=(HBRUSH)SelectObject(di->hDC,hBrush);
	    Rectangle(di->hDC,di->rcItem.left,di->rcItem.top,di->rcItem.right,di->rcItem.bottom);
	    SelectObject(di->hDC,hOldBrush);
	    DeleteObject(hBrush);
    }
	}
  return 0;
}


static char acedit_text[32];
static int acedit_allow;


static BOOL CALLBACK ACeditProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SendDlgItemMessage(hwndDlg,IDC_EDIT1,EM_LIMITTEXT,sizeof(acedit_text)-1,0);
    SetDlgItemText(hwndDlg,IDC_EDIT1,acedit_text);
    CheckDlgButton(hwndDlg,acedit_allow?IDC_RADIO1:IDC_RADIO2,BST_CHECKED);
  }
  if (uMsg == WM_CLOSE) EndDialog(hwndDlg,0);
  if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDOK)
    {
      acedit_allow=!!IsDlgButtonChecked(hwndDlg,IDC_RADIO1);
      GetDlgItemText(hwndDlg,IDC_EDIT1,acedit_text,sizeof(acedit_text));
      acedit_text[sizeof(acedit_text)-1]=0;
      char tmp[32];
      strcpy(tmp,acedit_text);
      if (tmp[0] && strstr(tmp,"/"))
      {
        char *p=strstr(tmp,"/");
        *p++=0;
        if (*p <= '9' && *p >= '0')
        {
          if (inet_addr(tmp) != INADDR_NONE)
            EndDialog(hwndDlg,1);
        }

      }     
    }
    else if (LOWORD(wParam) == IDCANCEL)
    {
      EndDialog(hwndDlg,0);
    }

  }
  return 0;
}

static void editAC(HWND hwndDlg, int idx, W_ListView *lv) // return 1 on 
{
  if (idx >= 0)
  {
    lv->GetText(idx,0,acedit_text,sizeof(acedit_text));
    acedit_allow = acedit_text[0]!='D';

    lv->GetText(idx,1,acedit_text,sizeof(acedit_text));
  }
  else 
  {
    strcpy(acedit_text,"0.0.0.0/0");
    acedit_allow=1;
  }
  if (DialogBox(g_hInst,MAKEINTRESOURCE(IDD_ACEDIT),hwndDlg,ACeditProc))
  {
    if (idx >= 0)
    {
      lv->SetItemText(idx,0,acedit_allow?"Allow":"Deny");
      lv->SetItemText(idx,1,acedit_text);
    }
    else
    {
      int p=lv->InsertItem(lv->GetCount(),acedit_allow?"Allow":"Deny",0);
      lv->SetItemText(p,1,acedit_text);
    }
    updateACList(lv);
  }
}

static BOOL CALLBACK Pref_Network2Proc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  static W_ListView *lv;
  if (uMsg == WM_INITDIALOG)
  {
    if (g_forceip) CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
    else
    {
      EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT1),0);
    }
    struct in_addr in;
    in.s_addr=g_forceip_addr;
    char *t=inet_ntoa(in);
    if (t && in.s_addr != INADDR_NONE) SetDlgItemText(hwndDlg,IDC_EDIT1,t);
    else SetDlgItemText(hwndDlg,IDC_EDIT1,"0.0.0.0");

    delete lv;
    lv=new W_ListView;
    lv->setwnd(GetDlgItem(hwndDlg,IDC_LIST1));
    lv->AddCol("Type",80);
    lv->AddCol("Address/Mask",140);

    if (g_config->ReadInt("ac_use",0))
      CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    int n=g_config->ReadInt("ac_cnt",0);
    int x;
    int p=0;
    for (x = 0; x < n; x ++)
    {
      char buf[64];
      sprintf(buf,"ac_%d",x);
      char *t=g_config->ReadString(buf,"");
      if (*t == 'A' || *t == 'D')
      {
        int a=lv->InsertItem(p,*t == 'A' ? "Allow" : "Deny",0);
        lv->SetItemText(a,1,t+1);
        p++;
      }

    }
  }
  if (uMsg == WM_DESTROY)
  {
    delete lv;
    lv=0;
  }
  if (uMsg == WM_NOTIFY)
  {
    LPNMHDR l=(LPNMHDR)lParam;
    if (l->idFrom==IDC_LIST1)
    {
      if (l->code == NM_DBLCLK)
        SendMessage(hwndDlg,WM_COMMAND,IDC_EDIT,0);
      else
      {
        int s=!!ListView_GetSelectedCount(l->hwndFrom);
        EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT),s);
        EnableWindow(GetDlgItem(hwndDlg,IDC_REMOVE),s);
        EnableWindow(GetDlgItem(hwndDlg,IDC_MOVEUP),s);
        EnableWindow(GetDlgItem(hwndDlg,IDC_MOVEDOWN),s);
      }
    }
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_EDIT1:
        if (HIWORD(wParam) == EN_CHANGE) 
        {
          char buf[32];
          GetDlgItemText(hwndDlg,IDC_EDIT1,buf,32);
          g_forceip_addr=inet_addr(buf);
          g_config->WriteInt("forceip_addr",g_forceip_addr);
        }
      return 0;
      case IDC_CHECK1:
        g_forceip=IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?1:0;
        g_config->WriteInt("forceip",g_forceip);
        EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT1),!!g_forceip);
      return 0;
      case IDC_EDIT:
      {
        int x;
        for (x = 0; x < lv->GetCount() && !lv->GetSelected(x); x ++);
        if (x < lv->GetCount())
        {
          editAC(hwndDlg,x,lv);
        }
      }
      return 0;
      case IDC_ADD:
        editAC(hwndDlg,-1,lv);
      return 0;
      case IDC_REMOVE:
      {
        int x;
        for (x = 0; x < lv->GetCount() && !lv->GetSelected(x); x ++);
        if (x < lv->GetCount())
        {
          lv->DeleteItem(x);
          updateACList(lv);
        }
      }
      return 0;
      case IDC_MOVEUP:
      case IDC_MOVEDOWN:
        {
          int x;
          for (x = 0; x < lv->GetCount() && !lv->GetSelected(x); x ++);
          if (x < lv->GetCount() && (LOWORD(wParam) == IDC_MOVEDOWN || x) &&
              (LOWORD(wParam) == IDC_MOVEUP || x < lv->GetCount()-1))
          {
            char buf[4][256];
            int a;
            if (LOWORD(wParam) == IDC_MOVEUP) x--;
            for (a = 0; a < 4; a ++) lv->GetText(x+(a&1),a/2,buf[a],256);
            for (a = 0; a < 4; a ++) lv->SetItemText(x+!(a&1),a/2,buf[a]);

            if (LOWORD(wParam) == IDC_MOVEUP) lv->SetSelected(x);
            else lv->SetSelected(x+1);
            updateACList(lv);
          }
        }
      break;
      case IDC_CHECK3:
        g_config->WriteInt("ac_use",g_use_accesslist=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK3));
      break;
    }
  }
  return 0;
}

static BOOL CALLBACK Pref_AboutProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SetDlgItemText(hwndDlg,IDC_TEXT1,g_nameverstr);
  }
  return 0;
}

static BOOL CALLBACK Pref_IdentProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {   
    SetDlgItemText(hwndDlg,IDC_EDIT1,g_regnick);
    if (!g_regnick[0])
      SetDlgItemText(hwndDlg,IDC_NICKSTATUS,"No nickname");
    else
    {
      char buf[512];
      sprintf(buf,"Current nickname: %s\n",g_regnick);
      SetDlgItemText(hwndDlg,IDC_NICKSTATUS,buf);
    }
    SetDlgItemText(hwndDlg,IDC_EDIT2,g_config->ReadString("userinfo",APP_NAME " User"));
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_EDIT2:
        if (HIWORD(wParam) == EN_CHANGE)
        {
          char buf[256];
          GetDlgItemText(hwndDlg,IDC_EDIT2,buf,sizeof(buf));
          buf[255]=0;
          g_config->WriteString("userinfo",buf);
        }
      break;
      case IDC_BUTTON2:
        {
          char buf[32];
          GetDlgItemText(hwndDlg,IDC_EDIT1,buf,32);
          buf[31]=0;
          if (stricmp(buf,g_regnick))
          {
            char oldnick[32];
            strcpy(oldnick,g_regnick);
            strcpy(g_regnick,buf);
            if (g_regnick[0] == '#' || g_regnick[0] == '&') g_regnick[0]=0;
            g_config->WriteString("nick",g_regnick);
            if (!g_regnick[0])
              SetDlgItemText(hwndDlg,IDC_NICKSTATUS,"No nickname");
            else
            {
              char buf[512];
              sprintf(buf,"Current nickname: %s\n",g_regnick);
              SetDlgItemText(hwndDlg,IDC_NICKSTATUS,buf);
              // send nick change notification
              chat_sendNickChangeFrom(oldnick);
            }
          }
        }
      break;
    }
  }
  return 0;
}

static BOOL CALLBACK Pref_ChatProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {   
    int f;
    if (g_config->ReadInt("allowpriv",1))
      CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
    f=g_config->ReadInt("gayflash",64);
    if (f&1) CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);
    if (f&2) CheckDlgButton(hwndDlg,IDC_CHECK9,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_EDIT5,f/4,FALSE);


    f=g_config->ReadInt("gayflashp",65);
    if (f&1) CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    if (f&2) CheckDlgButton(hwndDlg,IDC_CHECK8,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_EDIT4,f/4,FALSE);


    if (g_chat_timestamp&1)
      CheckDlgButton(hwndDlg,IDC_CHECK4,BST_CHECKED);
    if (g_chat_timestamp&2)
      CheckDlgButton(hwndDlg,IDC_CHECK5,BST_CHECKED);
    if (g_chat_timestamp&4)
      CheckDlgButton(hwndDlg,IDC_CHECK10,BST_CHECKED);
    if (g_config->ReadInt("cwhmin",0))
      CheckDlgButton(hwndDlg,IDC_CHECK6,BST_CHECKED);    
      
    SendDlgItemMessage(hwndDlg,IDC_EDIT2,EM_LIMITTEXT,16,0);
    SendDlgItemMessage(hwndDlg,IDC_EDIT1,EM_LIMITTEXT,32,0);

    if (g_config->ReadInt("limitchat",1))
      CheckDlgButton(hwndDlg,IDC_CHECK7,BST_CHECKED);
    else
      EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT2),0);
    SetDlgItemInt(hwndDlg,IDC_EDIT2,g_config->ReadInt("limitchatn",64),FALSE);

    uMsg=WM_USER+500;
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_BUTTON3: // chat font
        {
				  LOGFONT lf={0,};
				  CHOOSEFONT cf={sizeof(cf),0,0,0,0,
					  CF_EFFECTS|CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT,
					  0,};
				  cf.hwndOwner=hwndDlg;
				  cf.lpLogFont=&lf;
          char *p=g_config->ReadString("cfont_face","Fixedsys");
          if (*p)
            safe_strncpy(lf.lfFaceName,p,sizeof(lf.lfFaceName));
          int effects=g_config->ReadInt("cfont_fx",0);
          cf.rgbColors=g_config->ReadInt("cfont_color",0);
          if (cf.rgbColors & 0xFF000000)
            cf.rgbColors=0xFFFFFF;

          lf.lfHeight=(g_config->ReadInt("cfont_yh",180))/15;
				  lf.lfItalic=(effects&CFE_ITALIC)?1:0;
				  lf.lfWeight=(effects&CFE_BOLD)?FW_BOLD:FW_NORMAL;
				  lf.lfUnderline=(effects&CFE_UNDERLINE)?1:0;
				  lf.lfStrikeOut=(effects&CFE_STRIKEOUT)?1:0;
				  lf.lfCharSet=DEFAULT_CHARSET;
				  lf.lfOutPrecision=OUT_DEFAULT_PRECIS;
				  lf.lfClipPrecision=CLIP_DEFAULT_PRECIS;
				  lf.lfQuality=DEFAULT_QUALITY;
				  lf.lfPitchAndFamily=FIXED_PITCH;
				  
				  if (ChooseFont(&cf))
				  {
					  effects=0;
					  if (lf.lfItalic) effects |= CFE_ITALIC;
					  if (lf.lfUnderline) effects |= CFE_UNDERLINE;
					  if (lf.lfStrikeOut) effects |= CFE_STRIKEOUT;
					  if (lf.lfWeight!=FW_NORMAL)effects |= CFE_BOLD;
            g_config->WriteInt("cfont_fx",effects);
            g_config->WriteInt("cfont_yh",cf.iPointSize*2);
            g_config->WriteInt("cfont_color",cf.rgbColors);										
            g_config->WriteString("cfont_face",lf.lfFaceName);
            SendMessage(g_mainwnd,WM_USER_TITLEUPDATE,0,0);
            uMsg = WM_USER+500;
				  }
        }
      break;
      case IDC_BUTTON5: // chat bg color
			  {
          char *p=g_config->ReadString("cfont_face","Fixedsys");
          if (*p)
          {
            int c=g_config->ReadInt("cfont_bgc",0xFFFFFF);
				    CHOOSECOLOR cs;
				    cs.lStructSize = sizeof(cs);
				    cs.hwndOwner = hwndDlg;
				    cs.hInstance = 0;
				    cs.rgbResult=c;
				    cs.lpCustColors = custcolors;
				    cs.Flags = CC_RGBINIT|CC_FULLOPEN;
				    if (ChooseColor(&cs))
				    {
					    g_config->WriteInt("cfont_bgc",cs.rgbResult);
              SendMessage(g_mainwnd,WM_USER_TITLEUPDATE,0,0);
              uMsg = WM_USER+500;
				    }
          }
			  }
      break;
      case IDC_EDIT2:
        if (HIWORD(wParam) == EN_CHANGE)
        {
          BOOL t;
          int a=GetDlgItemInt(hwndDlg,IDC_EDIT2,&t,FALSE);
          if (t) g_config->WriteInt("limitchatn",a);
        }
      break;
      case IDC_CHECK1:
        g_config->WriteInt("allowpriv",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1));
      break;      
      case IDC_EDIT5:
        if (HIWORD(wParam) != EN_CHANGE) return 0;
      case IDC_CHECK9:
      case IDC_CHECK2:
        {
          BOOL t;
          g_config->WriteInt("gayflash",
            (IsDlgButtonChecked(hwndDlg,IDC_CHECK2)?1:0) |
            (IsDlgButtonChecked(hwndDlg,IDC_CHECK9)?2:0) |
            GetDlgItemInt(hwndDlg,IDC_EDIT5,&t,FALSE)*4
            );
        }
      break;
      case IDC_EDIT4:
        if (HIWORD(wParam) != EN_CHANGE) return 0;
      case IDC_CHECK8:
      case IDC_CHECK3:
        {
          BOOL t;
          g_config->WriteInt("gayflashp",
            (IsDlgButtonChecked(hwndDlg,IDC_CHECK3)?1:0) |
            (IsDlgButtonChecked(hwndDlg,IDC_CHECK8)?2:0) |
            GetDlgItemInt(hwndDlg,IDC_EDIT4,&t,FALSE)*4
            );
        }
      break;
      case IDC_CHECK10:
      case IDC_CHECK4:
      case IDC_CHECK5:
        g_chat_timestamp=(IsDlgButtonChecked(hwndDlg,IDC_CHECK4)?1:0) | (IsDlgButtonChecked(hwndDlg,IDC_CHECK5)?2:0)
            | (IsDlgButtonChecked(hwndDlg,IDC_CHECK10)?4:0);
        g_config->WriteInt("chat_timestamp",g_chat_timestamp);
      break;
      case IDC_CHECK6:
        g_config->WriteInt("cwhmin",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK6));
      break;
      case IDC_CHECK7:
        {
          int a;
          g_config->WriteInt("limitchat",a=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK7));
          EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT2),a);
        }
      break;
    }
  }
  if (uMsg == WM_DRAWITEM)
  {
		DRAWITEMSTRUCT *di=(DRAWITEMSTRUCT *)lParam;
		if (di->CtlID == IDC_BUTTON4 || di->CtlID == IDC_BUTTON6)
		{
      char *p=g_config->ReadString("cfont_face","FixedSys");
      int color=0;
      if (*p) 
      {
        color=g_config->ReadInt(di->CtlID == IDC_BUTTON4 ? "cfont_bgc" : "cfont_color",
                                di->CtlID == IDC_BUTTON4 ? 0xffffff : 0);
	      HBRUSH hBrush,hOldBrush;
	      LOGBRUSH lb={BS_SOLID,color,0};
	      hBrush = CreateBrushIndirect(&lb);
	      hOldBrush=(HBRUSH)SelectObject(di->hDC,hBrush);
	      Rectangle(di->hDC,di->rcItem.left,di->rcItem.top,di->rcItem.right,di->rcItem.bottom);
	      SelectObject(di->hDC,hOldBrush);
	      DeleteObject(hBrush);
      }
    }
	}

  if (uMsg == WM_USER+500)
  {
    char buf[512];
    char *p=g_config->ReadString("cfont_face","FixedSys");
    if (!*p) strcpy(buf,"(Windows Default)");
    else
    {
      sprintf(buf,"%s @ %dpt",p,g_config->ReadInt("cfont_yh",180)/20);       
    }
    
    SetDlgItemText(hwndDlg,IDC_CHATFONTTEXT,buf);
    InvalidateRect(hwndDlg,NULL,FALSE);
  }
  return 0;
}


static BOOL CALLBACK Pref_RecvProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {   
    SetDlgItemText(hwndDlg,IDC_EDIT1,g_config->ReadString("downloadpath",""));

    int a=g_config->ReadInt("dlppath",1|32|4|16);
    if (a&4)
      CheckDlgButton(hwndDlg,IDC_CHECK5,BST_CHECKED);
    if (a&16)
      CheckDlgButton(hwndDlg,IDC_CHECK6,BST_CHECKED);
    if (a&32)
      CheckDlgButton(hwndDlg,IDC_CHECK14,BST_CHECKED);
    if (a&1)
      CheckDlgButton(hwndDlg,IDC_CHECK11,BST_CHECKED);
    else EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK14),0);

    if (a&2)
      CheckDlgButton(hwndDlg,IDC_CHECK12,BST_CHECKED);
    else EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK5),0);

    if (a&8)
      CheckDlgButton(hwndDlg,IDC_CHECK13,BST_CHECKED);
    else EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK6),0);

    if (g_config->ReadInt("accept_uploads",1)&4) CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    if (g_config->ReadInt("accept_uploads",1)&2) CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);
    if (g_config->ReadInt("accept_uploads",1)&1) CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
    else 
    {
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK2),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK3),0);
    }
    SetDlgItemInt(hwndDlg,IDC_EDIT2,g_max_simul_dl_host&0x7FFFFFFF,FALSE);
    if (g_max_simul_dl_host&0x80000000)
      EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT2),0);
    else
      CheckDlgButton(hwndDlg,IDC_CHECK4,BST_CHECKED);
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_CHECK4:
        g_max_simul_dl_host&=0x7FFFFFFF;
        if (!IsDlgButtonChecked(hwndDlg,IDC_CHECK4))
        {
          EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT2),0);
          g_max_simul_dl_host|=0x80000000;
        }
        else EnableWindow(GetDlgItem(hwndDlg,IDC_EDIT2),1);
        g_config->WriteInt("recv_maxdl_host",g_max_simul_dl_host);
      break;
      case IDC_EDIT2:
        if (HIWORD(wParam) == EN_CHANGE)
        {
          BOOL t=0;
          int inter=GetDlgItemInt(hwndDlg,IDC_EDIT2,&t,FALSE);
          if (inter<0)inter=0;
          if (t) 
          {
            g_max_simul_dl_host&=0x80000000;
            g_max_simul_dl_host|=inter;
            g_config->WriteInt("recv_maxdl_host",g_max_simul_dl_host);
          }
        }
      break;
      case IDC_CHECK5:
      case IDC_CHECK6:
      case IDC_CHECK11:
      case IDC_CHECK12:
      case IDC_CHECK13:
      case IDC_CHECK14:

        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK14),IsDlgButtonChecked(hwndDlg,IDC_CHECK11)?1:0);
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK5),IsDlgButtonChecked(hwndDlg,IDC_CHECK12)?1:0);
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK6),IsDlgButtonChecked(hwndDlg,IDC_CHECK13)?1:0);

        g_config->WriteInt("dlppath",(IsDlgButtonChecked(hwndDlg,IDC_CHECK11)?1:0) | 
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK12) ? 2 : 0) |
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK5) ? 4 : 0) |
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK13) ? 8 : 0) |
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK6) ? 16 : 0) |
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK14) ? 32 : 0)
          );
      break;
      case IDC_CHECK1:
      case IDC_CHECK2:
      case IDC_CHECK3:
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK2),IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?1:0);
        EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK3),IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?1:0);
        g_config->WriteInt("accept_uploads",
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?1:0)|
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK2)?2:0)|
          (IsDlgButtonChecked(hwndDlg,IDC_CHECK3)?4:0));
      break;      
      case IDC_BROWSESAVE:
      {
        BROWSEINFO bi={0,};
			  ITEMIDLIST *idlist;
			  char name[1024];
			  GetDlgItemText(hwndDlg,IDC_EDIT1,name,sizeof(name));
			  bi.hwndOwner = hwndDlg;
			  bi.pszDisplayName = name;
			  bi.lpfn=BrowseCallbackProc;
			  bi.lParam=(LPARAM)GetDlgItem(hwndDlg,IDC_EDIT1);
			  bi.lpszTitle = "Select a directory:";
			  bi.ulFlags = BIF_RETURNONLYFSDIRS;
			  idlist = SHBrowseForFolder( &bi );
			  if (idlist) 
        {
				  SHGetPathFromIDList( idlist, name );        
	        IMalloc *m;
	        SHGetMalloc(&m);
	        m->Free(idlist);
				  SetDlgItemText(hwndDlg,IDC_EDIT1,name);
          g_config->WriteString("downloadpath",name);
			  }
      }
      return 0;
      case IDC_EDIT1:
        if (HIWORD(wParam) == EN_CHANGE)
        {
			    char name[1024];
			    GetDlgItemText(hwndDlg,IDC_EDIT1,name,sizeof(name));
          g_config->WriteString("downloadpath",name);
        }
      return 0;
    }
  }
  return 0;
}


static void PS_EnableWnds(HWND hwndDlg)
{
  static int a[]={IDC_CHECK8,IDC_CHECK9,IDC_LIMITUL,IDC_DIRTOALLOW,
           IDC_FILEPATHLIST,IDC_ADDDIR,IDC_RESCAN,IDC_CHECK3,IDC_EXTLIST,IDC_CHECK4,
           IDC_EDIT3,IDC_MINUTES,IDC_SCANSTATUS,IDC_CHECK6,IDC_CHECK5,
           IDC_SHAMB};
  int x;
  int en=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK2);
  for (x=0;x<sizeof(a)/sizeof(a[0]);x++)
  {
    int een=en;
    if (a[x]==IDC_LIMITUL) een=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1);
    else if (a[x]==IDC_SHAMB) een=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK7);
    else if (en)
    {
      if (a[x]==IDC_EXTLIST) een=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK3);
      else if (a[x]==IDC_EDIT3) een=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK4);
    }
    EnableWindow(GetDlgItem(hwndDlg,a[x]),een);
  }

}


static BOOL CALLBACK Pref_SendProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_TIMER)
  {
    if (wParam == 1 && g_scan_status_buf[0])
    {
      SetDlgItemText(hwndDlg,IDC_SCANSTATUS,g_scan_status_buf);
      g_scan_status_buf[0]=0;
    }
  }
  if (uMsg == WM_INITDIALOG)
  {   
    SetTimer(hwndDlg,1,50,NULL);
    if (g_scan_status_buf[0])
    {
      SetDlgItemText(hwndDlg,IDC_SCANSTATUS,g_scan_status_buf);
      g_scan_status_buf[0]=0;
    }
    else
    {
      if (g_database)
      {
        char buf[512];
        sprintf(buf,"Scanned %d",g_database->GetNumFiles());
        SetDlgItemText(hwndDlg,IDC_SCANSTATUS,buf);
      }
    }

    if (g_accept_downloads&1) CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);
    if (g_accept_downloads&2) CheckDlgButton(hwndDlg,IDC_CHECK8,BST_CHECKED);
    if (g_accept_downloads&4) CheckDlgButton(hwndDlg,IDC_CHECK9,BST_CHECKED);
    if (g_config->ReadInt("use_extlist",0)) CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    if (g_config->ReadInt("limit_uls",1)) CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_LIMITUL,g_config->ReadInt("ul_limit",160),FALSE);    
    if (g_config->ReadInt("ulfullpaths",0))
      CheckDlgButton(hwndDlg,IDC_CHECK10,BST_CHECKED);

    if (g_config->ReadInt("shafiles",1)) CheckDlgButton(hwndDlg,IDC_CHECK7,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_SHAMB,g_config->ReadInt("maxsizesha",32),FALSE);
    
    SetDlgItemText(hwndDlg,IDC_FILEPATHLIST,g_config->ReadString("databasepath",""));
    SetDlgItemText(hwndDlg,IDC_EXTLIST,g_config->ReadString("extlist",g_def_extlist));
    SetDlgItemInt(hwndDlg,IDC_EDIT3,g_config->ReadInt("refreshint",300),FALSE);
    if (g_do_autorefresh) 
    {
      CheckDlgButton(hwndDlg,IDC_CHECK4,BST_CHECKED);
    }
    if (g_config->ReadInt("db_save",1))
    {
      CheckDlgButton(hwndDlg,IDC_CHECK5,BST_CHECKED);        
    }
    if (g_config->ReadInt("scanonstartup",1))
    {
      CheckDlgButton(hwndDlg,IDC_CHECK6,BST_CHECKED);        
    }
    PS_EnableWnds(hwndDlg);
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_CHECK10:
        g_config->WriteInt("ulfullpaths",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK10));
      break;      
      case IDC_SHAMB:
        if (HIWORD(wParam) == EN_CHANGE) 
        {
          BOOL t=0;
          int inter=GetDlgItemInt(hwndDlg,IDC_SHAMB,&t,FALSE);
          if (inter<0)inter=0;
          if (t) g_config->WriteInt("maxsizesha",inter);
        }
      return 0;
      case IDC_LIMITUL:
        if (HIWORD(wParam) == EN_CHANGE) 
        {
          BOOL t=0;
          int inter=GetDlgItemInt(hwndDlg,IDC_LIMITUL,&t,FALSE);
          if (inter<0)inter=0;
          if (t) g_config->WriteInt("ul_limit",inter);
        }
      return 0;
      case IDC_CHECK1:
        g_config->WriteInt("limit_uls",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1));
        PS_EnableWnds(hwndDlg);
      return 0;
      case IDC_CHECK7:
        g_config->WriteInt("shafiles",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK7));
        PS_EnableWnds(hwndDlg);
      return 0;          
      case IDC_CHECK6:
        g_config->WriteInt("scanonstartup",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK6));
      return 0;          
      case IDC_CHECK5:
        g_config->WriteInt("db_save",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK5));
      return 0;
      case IDC_CHECK4:
        g_config->WriteInt("dorefresh",g_do_autorefresh=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK4));
        g_next_refreshtime = time(NULL)+60*g_config->ReadInt("refreshint",300);
        PS_EnableWnds(hwndDlg);
      return 0;
      case IDC_EDIT3:
        if (HIWORD(wParam) == EN_CHANGE) 
        {
          BOOL t=0;
          int inter=GetDlgItemInt(hwndDlg,IDC_EDIT3,&t,FALSE);
          if (inter<1)inter=1;
          if (t) g_config->WriteInt("refreshint",inter);
          g_next_refreshtime = time(NULL)+60*g_config->ReadInt("refreshint",300);
        }
      return 0;
      case IDC_ADDDIR:
        {
          char name[MAX_PATH]="";
          BROWSEINFO bi={0,};
			    ITEMIDLIST *idlist;
			    bi.hwndOwner = hwndDlg;
			    bi.pszDisplayName = name;
			    bi.lpfn=BrowseCallbackProc;
			    bi.lpszTitle = "Select a directory:";
			    bi.ulFlags = BIF_RETURNONLYFSDIRS;
			    idlist = SHBrowseForFolder( &bi );
			    if (idlist) 
          {
            char buf[4096];
				    SHGetPathFromIDList( idlist, name );        
	          IMalloc *m;
	          SHGetMalloc(&m);
	          m->Free(idlist);
            GetDlgItemText(hwndDlg,IDC_FILEPATHLIST,buf,sizeof(buf));
            if (buf[0] && buf[strlen(buf)-1]!=';') strcat(buf,";");
            strcat(buf,name);
				    SetDlgItemText(hwndDlg,IDC_FILEPATHLIST,buf);
            g_config->WriteString("databasepath",buf);
			    }
        }
      return 0;
      case IDC_FILEPATHLIST:
        if (HIWORD(wParam) == EN_CHANGE) 
        {
          char buf[4096];
          GetDlgItemText(hwndDlg,IDC_FILEPATHLIST,buf,sizeof(buf));
          g_config->WriteString("databasepath",buf);
        }
      return 0;
      case IDC_EXTLIST:
        if (HIWORD(wParam) == EN_CHANGE) 
        {
          char buf[4096];
          GetDlgItemText(hwndDlg,IDC_EXTLIST,buf,sizeof(buf));
          g_config->WriteString("extlist",buf);
        }
      return 0;
      case IDC_RESCAN:
        doDatabaseRescan();
      return 0;
      case IDC_CHECK2:
        g_accept_downloads&=~1;
        g_config->WriteInt("downloadflags",
          g_accept_downloads|=IsDlgButtonChecked(hwndDlg,IDC_CHECK2)?1:0);
        PS_EnableWnds(hwndDlg);
      break;
      case IDC_CHECK8:
        g_accept_downloads&=~2;
        g_config->WriteInt("downloadflags",
          g_accept_downloads|=IsDlgButtonChecked(hwndDlg,IDC_CHECK8)?2:0);
      break;
      case IDC_CHECK9:
        g_accept_downloads&=~4;
        g_config->WriteInt("downloadflags",
          g_accept_downloads|=IsDlgButtonChecked(hwndDlg,IDC_CHECK9)?4:0);
      break;
      case IDC_CHECK3:
        g_config->WriteInt("use_extlist",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK3));
        PS_EnableWnds(hwndDlg);
      break;
    }
  }
  return 0;
}

static UINT CALLBACK importTextProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDC_BUTTON1)
    {
      char temppath[1024];
      char tempfn[2048];
      int num=0;
      GetTempPath(sizeof(temppath),temppath);
      GetTempFileName(temppath,"wst",0,tempfn);
      FILE *a=fopen(tempfn,"wt");
      if (a)
      {

        int tlen=SendDlgItemMessage(hwndDlg,IDC_KEYTEXT,WM_GETTEXTLENGTH,0,0)+1;
        char *buf=(char*)malloc(tlen+1);
        if (buf)
        {
          GetDlgItemText(hwndDlg,IDC_KEYTEXT,buf,tlen);
          buf[tlen]=0;
          fwrite(buf,1,strlen(buf),a);
          free(buf);
        }
        fclose(a);
        num=loadPKList(tempfn);
      }
      DeleteFile(tempfn);
      if (!num)
      {
        MessageBox(hwndDlg,"Error: No key(s) found in text",APP_NAME " Public Key Import Error",MB_OK|MB_ICONSTOP);
        return 0;
      }
      savePKList();      
      sprintf(tempfn,"Imported %d public keys successfully.",num);
      MessageBox(hwndDlg,tempfn,APP_NAME " Public Key Import",MB_OK|MB_ICONINFORMATION);
      PostMessage(GetParent(hwndDlg),WM_COMMAND,IDCANCEL,0);
      return 1;
    }
  }

  return 0;
}


static void importPubDlg(HWND hwndDlg)
{
  char *fnroot=(char*)malloc(65536*4);
  fnroot[0]=0;
  OPENFILENAME l={sizeof(l),};
  l.hwndOwner = hwndDlg;
  l.lpstrFilter = "Public key files (*.pub;*.txt)\0*.pub;*.txt\0All files (*.*)\0*.*\0";
  l.lpstrFile = fnroot;
  l.nMaxFile = 65535*4;
  l.lpstrTitle = "Import public key file(s)";
  l.lpstrDefExt = "pub";
  l.hInstance=g_hInst;
  l.lpfnHook=importTextProc;
  l.lpTemplateName=MAKEINTRESOURCE(IDD_KEYIMPORTDLG);

  l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_ENABLETEMPLATE|OFN_ENABLEHOOK|OFN_FILEMUSTEXIST;
  	        
  if (GetOpenFileName(&l)) 
  {
    int num=0;
    char *fn=fnroot;
    char *pathstr="";
    if (fn[strlen(fn)+1]) // multiple files
    {
      pathstr=fn;
      fn+=strlen(fn)+1;
    }
    while (*fn)
    {
      char fullfn[4096];
      fullfn[0]=0;
      if (*pathstr)
      {
        strcpy(fullfn,pathstr);
        if (fullfn[strlen(fullfn)-1]!='\\') strcat(fullfn,"\\");
      }
      strcat(fullfn,fn);

      num+=loadPKList(fullfn);
      fn+=strlen(fn)+1;
    }
    savePKList();
    if (num)
    {
      char buf[128];
      sprintf(buf,"Imported %d public keys successfully.",num);
      MessageBox(hwndDlg,buf,APP_NAME " Public Key Import",MB_OK|MB_ICONINFORMATION);
    }
    else
    {
      MessageBox(hwndDlg,"Error: No key(s) found in text",APP_NAME " Public Key Import Error",MB_OK|MB_ICONSTOP);
    }
  }
  free(fnroot);

  return;
}

static BOOL CALLBACK Pref_KeyDist2Proc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  static W_ListView *lv;
  if (uMsg == WM_INITDIALOG)
  {
    delete lv;
    lv=new W_ListView;
    lv->setwnd(GetDlgItem(hwndDlg,IDC_LIST1));
    lv->AddCol("Key name",100);
    lv->AddCol("Signature",160);
    int x;
    for (x = 0; x < g_pklist_pending.GetSize(); x ++)
    {
      PKitem *p=g_pklist_pending.Get(x);
      lv->InsertItem(x,p->name,0);
      char buf[128];
      int a;
      for (a = 0; a < SHA_OUTSIZE; a ++) sprintf(buf+a*2,"%02X",p->hash[a]);
      lv->SetItemText(x,1,buf);
    }
    if (g_keydist_flags&4) CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);   
    if (g_keydist_flags&1) 
    {
      CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);   
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK3),0);
    }
    if (g_keydist_flags&2) CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
  }
  if (uMsg == WM_NOTIFY)
  {
    LPNMHDR l=(LPNMHDR)lParam;
    if (l->idFrom==IDC_LIST1)
    {
      if (l->code == NM_DBLCLK)
        SendMessage(hwndDlg,WM_COMMAND,IDC_AUTH,0);
    }
  }
  if (uMsg == WM_COMMAND) switch (LOWORD(wParam))
  {
    case IDC_REMOVE:
      {
        int x;
        for (x = 0; x < lv->GetCount(); x ++)
        {
          if (lv->GetSelected(x))
          {
            free(g_pklist_pending.Get(x));
            g_pklist_pending.Del(x);
            lv->DeleteItem(x);
            x--;
          }
        }
        savePKList();
      }
    return 0;
    case IDC_AUTH:
      {
        int x;
        for (x = 0; x < lv->GetCount(); x ++)
        {
          if (lv->GetSelected(x))
          {
            g_pklist.Add(g_pklist_pending.Get(x));
            g_pklist_pending.Del(x);
            lv->DeleteItem(x);
            x--;

          }
        }
        savePKList();
      }
    return 0;
    case IDC_CHECK1:
    case IDC_CHECK2:
    case IDC_CHECK3:
      g_keydist_flags=(IsDlgButtonChecked(hwndDlg,IDC_CHECK2)?1:0)|
                      (IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?4:0)|
                      (IsDlgButtonChecked(hwndDlg,IDC_CHECK3)?2:0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK3),!(g_keydist_flags&1));
      g_config->WriteInt("keydistflags",g_keydist_flags);
    return 0;
  }
  return 0;
}

static BOOL CALLBACK Pref_KeyDistProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  static W_ListView *lv;
  if (uMsg == WM_INITDIALOG)
  {
    delete lv;
    lv=new W_ListView;
    lv->setwnd(GetDlgItem(hwndDlg,IDC_LIST1));
    lv->AddCol("Key name",100);
    lv->AddCol("Signature",160);
    int x;
    for (x = 0; x < g_pklist.GetSize(); x ++)
    {
      PKitem *p=g_pklist.Get(x);
      lv->InsertItem(x,p->name,0);
      char buf[128];
      int a;
      for (a = 0; a < SHA_OUTSIZE; a ++) sprintf(buf+a*2,"%02X",p->hash[a]);
      lv->SetItemText(x,1,buf);
    }
  }
  if (uMsg == WM_COMMAND) switch (LOWORD(wParam))
  {
    case IDC_VIEWKEYS:
      {
        char str[2048];
        sprintf(str,"%s.pr3",g_config_prefix);
        char dir[MAX_PATH];
        GetCurrentDirectory(sizeof(dir),dir);
        ShellExecute(hwndDlg,NULL,"notepad",str,dir,SW_SHOWMAXIMIZED);
      }
    return 0;
    case IDC_EDIT:
      {
        int x;
        for (x = 0; x < lv->GetCount(); x ++)
        {
          if (lv->GetSelected(x))
          {
            free(g_pklist.Get(x));
            g_pklist.Del(x);
            lv->DeleteItem(x);
            x--;
          }
        }
        savePKList();
      }
    return 0;
    case IDC_IMPORT:
      importPubDlg(hwndDlg);
      SendMessage(hwndDlg,WM_COMMAND,IDC_RELOAD,0);
    return 0;

    case IDC_RELOAD:
    {
      int x=g_pklist.GetSize()-1;
      while (x >= 0)
      {
        free(g_pklist.Get(x));
        g_pklist.Del(x);
        lv->DeleteItem(x);
        x--;
      }
      x=g_pklist_pending.GetSize()-1;
      while (x >= 0)
      {
        free(g_pklist_pending.Get(x));
        g_pklist_pending.Del(x);
        x--;
      }
      loadPKList();
      for (x = 0; x < g_pklist.GetSize(); x ++)
      {
        PKitem *p=g_pklist.Get(x);
        lv->InsertItem(x,p->name,0);
        char buf[128];
        int a;
        for (a = 0; a < SHA_OUTSIZE; a ++) sprintf(buf+a*2,"%02X",p->hash[a]);
        lv->SetItemText(x,1,buf);
      }
    }
    return 0;
  }
  return 0;
}

static BOOL CALLBACK keyPassChangeProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_CLOSE) EndDialog(hwndDlg,1);
  if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDCANCEL) EndDialog(hwndDlg,1);
    if (LOWORD(wParam) == IDOK)
    {
      char oldpass[1024],buf2[1024],buf3[1024];
      GetDlgItemText(hwndDlg,IDC_OLDPASS,oldpass,sizeof(oldpass));
      GetDlgItemText(hwndDlg,IDC_NEWPASS1,buf2,sizeof(buf2));
      GetDlgItemText(hwndDlg,IDC_NEWPASS2,buf3,sizeof(buf3));
      if (strcmp(buf2,buf3))
      {
        MessageBox(hwndDlg,"New passphrase mistyped",APP_NAME " Error",MB_OK|MB_ICONSTOP);
        return 0;
      }
      R_RSA_PRIVATE_KEY key;
      char keyfn[1024];
      sprintf(keyfn,"%s.pr4",g_config_prefix);
      memset(&key,0,sizeof(key));
      if (doLoadKey(hwndDlg,oldpass,keyfn,&key) || !key.bits) return 0;

      char newpasshash[SHA_OUTSIZE];
      SHAify c;
      c.add((unsigned char *)buf2,strlen(buf2));
      c.final((unsigned char *)newpasshash);

      kg_writePrivateKey(keyfn,&key,&g_random,newpasshash);

      memset(newpasshash,0,sizeof(newpasshash));
      memset(buf2,0,sizeof(buf2));
      memset(buf3,0,sizeof(buf3));
      memset(oldpass,0,sizeof(oldpass));
      memset(&key,0,sizeof(key));
      EndDialog(hwndDlg,0);
    }
  }
  return 0;
}

static BOOL CALLBACK Pref_SecurityProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  static W_ListView *lv;
  if (uMsg == WM_INITDIALOG)
  {   
    if (g_config->ReadInt("bcastkey",1))
      CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
    SendMessage(hwndDlg,WM_USER+0x109,0,0);
  }
  if (uMsg == WM_USER+0x109)
  {
    char sign[SHA_OUTSIZE*2+64];
    int x;
    if (g_key.bits)
    {
      strcpy(sign,"Key signature:");
      int t=strlen(sign);
      for (x = 0; x < SHA_OUTSIZE; x ++) sprintf(sign+t+x*2,"%02X",g_pubkeyhash[x]);
      EnableWindow(GetDlgItem(hwndDlg,IDC_BUTTON2),1);
      EnableWindow(GetDlgItem(hwndDlg,IDC_BUTTON8),1);
      EnableWindow(GetDlgItem(hwndDlg,IDC_BROWSE2),1);      
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHPASS),1);            
    }
    else
    {
      strcpy(sign,"Key not loaded.");
      EnableWindow(GetDlgItem(hwndDlg,IDC_BUTTON2),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_BUTTON8),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_BROWSE2),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHPASS),0);
      
    }
    SetDlgItemText(hwndDlg,IDC_EDIT1,sign);
  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_CHECK1:
        g_config->WriteInt("bcastkey",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1));
      return 0;
      case IDC_BUTTON8:
        main_BroadcastPublicKey();
      return 0;
      case IDC_BUTTON2:
        copyMyPubKey(hwndDlg);
      return 0;
      case IDC_CHPASS:
        if (DialogBox(g_hInst,MAKEINTRESOURCE(IDD_CHPASS),hwndDlg,keyPassChangeProc))
          return 0;
      case IDC_RELOADKEY:
        reloadKey(NULL,hwndDlg);
        SendMessage(hwndDlg,WM_USER+0x109,0,0);
      return 0;
      case IDC_BROWSE2:
        {
          char str[1024];
          sprintf(str,"%s.pr4",g_config_prefix);
          FILE *fp=fopen(str,"rb");
          if (fp)
          {
            fclose(fp);
            char buf[1024];
            OPENFILENAME l={sizeof(l),};
            GetDlgItemText(hwndDlg,IDC_EDIT3,buf,sizeof(buf));
            l.hwndOwner = hwndDlg;
            l.lpstrFilter = "Private key files (*.prv)\0*.prv\0All files (*.*)\0*.*\0";
            l.lpstrFile = buf;
            l.nMaxFile = sizeof(buf);
            l.lpstrTitle = "Select key output file";
            l.lpstrDefExt = "";

            l.Flags = OFN_EXPLORER;
            if (GetSaveFileName(&l))
            {
              if (!CopyFile(str,buf,FALSE))
                MessageBox(hwndDlg,"Error exporting key",APP_NAME " Error",MB_OK|MB_ICONEXCLAMATION);
            }
          }
          else
            MessageBox(hwndDlg,"No Private Key available for export",APP_NAME " Error",MB_OK|MB_ICONEXCLAMATION);
        }
      return 0;
      case IDC_KEYGEN:
        {
          int warn=0;
          char str[1024];
          sprintf(str,"%s.pr4",g_config_prefix);
          FILE *fp=fopen(str,"rb");
          if (fp)
          {
            fclose(fp);
            warn=1;
          }

          if (!warn || MessageBox(hwndDlg,"Warning - Generating a new key may remove your old private key.\r\n"
                                          "To save a copy of your private key, hit cancel and select\r\n"
                                          "\"Export Private Key\". To go ahead and generate a new key\r\n"
                                          "hit OK.",APP_NAME " Private Key Warning",MB_ICONQUESTION|MB_OKCANCEL) == IDOK)
          {
            char parms[2048];
            sprintf(parms,"%s.pr4",g_config_prefix);
            RunKeyGen(hwndDlg,parms);
            SendMessage(hwndDlg,WM_USER+0x109,0,0);
          }
        }
      return 0;
      case IDC_BROWSE:
        {
          int warn=0;
          char str[1024];
          sprintf(str,"%s.pr4",g_config_prefix);
          FILE *fp=fopen(str,"rb");
          if (fp)
          {
            fclose(fp);
            warn=1;
          }

          if (!warn || MessageBox(hwndDlg,"Warning - Importing a new key may remove your old private key.\r\n"
                                          "To save a copy of your private key, hit cancel and select\r\n"
                                          "\"Export Private Key\". To go ahead and import a new key\r\n"
                                          "hit OK.",APP_NAME " Private Key Warning",MB_ICONQUESTION|MB_OKCANCEL) == IDOK)
          {
            char temp[1024];
            OPENFILENAME l={sizeof(l),};
            GetDlgItemText(hwndDlg,IDC_EDIT2,temp,1024);
            l.hwndOwner = hwndDlg;
            l.lpstrFilter = "Private key files (*.prv)\0*.prv\0All files (*.*)\0*.*\0";
            l.lpstrFile = temp;
            l.nMaxFile = 1023;
            l.lpstrTitle = "Import private key file";
            l.lpstrDefExt = "prv";

            l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_FILEMUSTEXIST;
  	                  
            if (GetOpenFileName(&l)) 
            {
              char str[1024];
              sprintf(str,"%s.pr4",g_config_prefix);
              CopyFile(temp,str,FALSE);
              reloadKey(NULL,hwndDlg);
              SendMessage(hwndDlg,WM_USER+0x109,0,0);
            }
          }
        }
      return 0;
    }
  }
  return 0;
}

static void updPortText(HWND hwndDlg)
{
  char buf[128];

  if (g_port)
  {
    if (!g_listen || g_listen->is_error())
      sprintf(buf,"Error listening on port %d",g_port);
    else sprintf(buf,"Listening on port %d",g_port);
  }
  else
    strcpy(buf,"Listening disabled");
  SetDlgItemText(hwndDlg,IDC_CURPORT,buf);
}

static BOOL CALLBACK Pref_NetworkProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {   
    int x;
    for (x = 0; x < sizeof(conspeed_strs)/sizeof(conspeed_strs[0]); x ++)
    {
      SendDlgItemMessage(hwndDlg,IDC_CONSPEED,CB_ADDSTRING,0,(long)conspeed_strs[x]);
    }
    for (x = 0; x < sizeof(conspeed_strs)/sizeof(conspeed_strs[0]); x ++)
    {
      if (g_conspeed <= conspeed_speeds[x]) break;
    }
    if (x == sizeof(conspeed_strs)/sizeof(conspeed_strs[0])) x--;
    SendDlgItemMessage(hwndDlg,IDC_CONSPEED,CB_SETCURSEL,x,0);

    if (g_route_traffic)
      CheckDlgButton(hwndDlg,IDC_CHECK3,BST_CHECKED);
    else
    {
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK2),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_PORT),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_UPDPORT),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK1),0);
    }

    if (g_config->ReadInt("advertise_listen",1))
      CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);

    if (g_config->ReadInt("listen",1)) CheckDlgButton(hwndDlg,IDC_CHECK2,BST_CHECKED);
    else
    {
      EnableWindow(GetDlgItem(hwndDlg,IDC_PORT),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_UPDPORT),0);
      EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK1),0);
    }
    SetDlgItemInt(hwndDlg,IDC_PORT,g_config->ReadInt("port",1337),FALSE);
    SetDlgItemText(hwndDlg,IDC_EDIT1,g_config->ReadString("networkname",""));

    updPortText(hwndDlg);

  }
  if (uMsg == WM_COMMAND)
  {
    switch (LOWORD(wParam))
    {
      case IDC_CONSPEED:
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
          int x=SendDlgItemMessage(hwndDlg,IDC_CONSPEED,CB_GETCURSEL,0,0);
          if (x >= 0 && x < sizeof(conspeed_strs)/sizeof(conspeed_strs[0])) {
            if (conspeed_speeds[x] != g_conspeed) RebroadcastCaps(g_mql);
            g_conspeed=conspeed_speeds[x];
            g_config->WriteInt("conspeed",conspeed_speeds[x]);
            g_route_traffic=conspeed_speeds[x]>64;
            CheckDlgButton(hwndDlg,IDC_CHECK3,conspeed_speeds[x]>64?BST_CHECKED:BST_UNCHECKED);
          }
        }
        else return 0;
      case IDC_CHECK3:
      case IDC_CHECK2:
        {
          g_route_traffic=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK3);
          EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK2),g_route_traffic);
          g_config->WriteInt("route",g_route_traffic);
          int a=!!IsDlgButtonChecked(hwndDlg,IDC_CHECK2);
          g_config->WriteInt("listen",a);
          if (!g_route_traffic) a=0;
          update_set_port();
          updPortText(hwndDlg);
          EnableWindow(GetDlgItem(hwndDlg,IDC_PORT),a);
          EnableWindow(GetDlgItem(hwndDlg,IDC_UPDPORT),a);
          EnableWindow(GetDlgItem(hwndDlg,IDC_CHECK1),a);
        }
      return 0;
      case IDC_UPDPORT:
        {
          BOOL t;
          int r=GetDlgItemInt(hwndDlg,IDC_PORT,&t,FALSE);
          if (t) g_config->WriteInt("port",r);
        }
        update_set_port();
        updPortText(hwndDlg);
      return 0;
      case IDC_CHECK1:
        g_config->WriteInt("advertise_listen",!!IsDlgButtonChecked(hwndDlg,IDC_CHECK1));
      return 0;
      case IDC_EDIT1:
        if (HIWORD(wParam) == EN_CHANGE)
        {
          char buf[256];
          GetDlgItemText(hwndDlg,IDC_EDIT1,buf,sizeof(buf));
          g_config->WriteString("networkname",buf);
          if (buf[0])
          {
            SHAify m;
            m.add((unsigned char *)buf,strlen(buf));
            m.final(g_networkhash);
            m.reset();
            g_use_networkhash=1;
          }
          else 
          {
            memset(g_networkhash,0,sizeof(g_networkhash));
            g_use_networkhash=0;
          }
          memset(buf,0,sizeof(buf));
        }
      return 0;
    }
    return 0;
  }
  return 0;

}

BOOL CALLBACK PrefsOuterProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			{
        SetWndTitle(hwndDlg,APP_NAME " Preferences");
        prefs_last_page=g_config->ReadInt("prefslp",12);
				lp_v=NULL;
				HWND hwnd=GetDlgItem(hwndDlg,IDC_TREE1);
				HTREEITEM h;
        _additem(hwnd,TVI_ROOT,"About",0,12);
        _additem(hwnd,TVI_ROOT,"Profiles",0,8);
				_additem(hwnd,TVI_ROOT,"Identity",0,13);
				h=_additem(hwnd,TVI_ROOT,"Network",1,0);
  				_additem(hwnd,h,"Private Key",0,1);
  				_additem(hwnd,h,"Public Keys",0,10);
  				_additem(hwnd,h,"Pending Keys",0,11);
          _additem(hwnd,h,"IP Addresses",0,4);
          _additem(hwnd,h,"Bandwidth",0,9);
				SendMessage(hwnd,TVM_EXPAND,TVE_EXPAND,(long)h);
				_additem(hwnd,TVI_ROOT,"Display",0,5);
				_additem(hwnd,TVI_ROOT,"Chat",0,2);
				h=_additem(hwnd,TVI_ROOT,"File Transfers",1,3);
          _additem(hwnd,h,"Receiving",0,6);
          _additem(hwnd,h,"Sending",0,7);
				SendMessage(hwnd,TVM_EXPAND,TVE_EXPAND,(long)h);

				if (lp_v) SendMessage(hwnd,TVM_SELECTITEM,TVGN_CARET,(long)lp_v);
			}
		return TRUE;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDCANCEL:
				case IDOK:
					DestroyWindow(hwndDlg);
				return FALSE;
			}
		break;
		case WM_NOTIFY:
		{
			NM_TREEVIEW *p;
			p=(NM_TREEVIEW *)lParam;

			if (p->hdr.code==TVN_SELCHANGED)
			{
				HTREEITEM hTreeItem = TreeView_GetSelection(GetDlgItem(hwndDlg,IDC_TREE1));
				TV_ITEM i={TVIF_HANDLE,hTreeItem,0,0,0,0,0};
				TreeView_GetItem(GetDlgItem(hwndDlg,IDC_TREE1),&i);
				{
					int id=-1;
					DLGPROC proc;
					prefs_last_page=i.lParam;
          g_config->WriteInt("prefslp",prefs_last_page);
					switch (i.lParam)
					{
						case 0:		id=IDD_PREF_NETWORK;	proc=Pref_NetworkProc; break;
						case 1:		id=IDD_PREF_SECURITY;	proc=Pref_SecurityProc; break;
						case 2:		id=IDD_PREF_CHAT;	proc=Pref_ChatProc; break;
						case 3:		id=IDD_PREF_FILES;	proc=Pref_FilesProc; break;
						case 4:		id=IDD_PREF_NETWORK_A;	proc=Pref_Network2Proc; break;
						case 5:		id=IDD_PREF_DISPLAY;	proc=Pref_DisplayProc; break;
						case 6:		id=IDD_PREF_FILES_RECV;	proc=Pref_RecvProc; break;
						case 7:		id=IDD_PREF_FILES_SEND;	proc=Pref_SendProc; break;
						case 8:		id=IDD_PREF_PROFILES;	proc=Pref_ProfilesProc; break;
						case 9:		id=IDD_PREF_THROTTLE;	proc=Pref_ThrottleProc; break;
						case 10:	id=IDD_PREF_KEYDIST;	proc=Pref_KeyDistProc; break;
						case 11:	id=IDD_PREF_KEYDIST_PENDING;	proc=Pref_KeyDist2Proc; break;
						case 12:	id=IDD_PREF_ABOUT;	proc=Pref_AboutProc; break;
						case 13:	id=IDD_PREF_IDENT;	proc=Pref_IdentProc; break;
					}

					if (prefs_cur_wnd) 
					{
						DestroyWindow(prefs_cur_wnd); 
						prefs_cur_wnd=0;
					}
					if (id != -1)
					{
						RECT r;
						prefs_cur_wnd=CreateDialog(g_hInst,MAKEINTRESOURCE(id),hwndDlg,proc);
						GetWindowRect(GetDlgItem(hwndDlg,IDC_RECT),&r);
						ScreenToClient(hwndDlg,(LPPOINT)&r);
						SetWindowPos(prefs_cur_wnd,0,r.left,r.top,0,0,SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
						ShowWindow(prefs_cur_wnd,SW_SHOWNA);
					}

				}
			}
		}
		break;
		case WM_DESTROY:
			if (prefs_cur_wnd) DestroyWindow(prefs_cur_wnd); 
			prefs_cur_wnd=0;
			prefs_hwnd=NULL;
      CheckMenuItem(GetMenu(g_mainwnd),ID_VIEW_PREFERENCES,MF_UNCHECKED|MF_BYCOMMAND);
      g_config->Flush();
      SaveNetQ();
		break;
	}
	return FALSE;
}







/////////////////////////// wizard shit ////////////////////////////////////

static int sw_pos;
static HWND sw_wnd;

static BOOL WINAPI SW_Proc1(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
  if (uMsg == WM_INITDIALOG)
  {
    int x;
    for (x = 0; x < sizeof(conspeed_strs)/sizeof(conspeed_strs[0]); x ++)
    {
      SendDlgItemMessage(hwndDlg,IDC_CONSPEED,CB_ADDSTRING,0,(long)conspeed_strs[x]);
    }
    for (x = 0; x < sizeof(conspeed_strs)/sizeof(conspeed_strs[0]); x ++)
    {
      if (g_config->ReadInt("conspeed",28) <= conspeed_speeds[x]) break;
    }
    if (x == sizeof(conspeed_strs)/sizeof(conspeed_strs[0])) x--;
    SendDlgItemMessage(hwndDlg,IDC_CONSPEED,CB_SETCURSEL,x,0);
    SetDlgItemText(hwndDlg,IDC_EDIT1,g_config->ReadString("nick",""));
    SetDlgItemText(hwndDlg,IDC_EDIT2,g_config->ReadString("userinfo",APP_NAME " User"));
    SetDlgItemText(hwndDlg,IDC_EDIT6,g_config->ReadString("networkname",""));    
  }  
  if (uMsg == WM_COMMAND && wParam == MAKEWPARAM(IDC_EDIT6,EN_CHANGE))
  {
    char buf[256];
    GetDlgItemText(hwndDlg,IDC_EDIT6,buf,sizeof(buf));
    g_config->WriteString("networkname",buf);
    if (buf[0])
    {
      SHAify m;
      m.add((unsigned char *)buf,strlen(buf));
      m.final(g_networkhash);
      m.reset();
      g_use_networkhash=1;
    }
    else 
    {
      memset(g_networkhash,0,sizeof(g_networkhash));
      g_use_networkhash=0;
    }
    memset(buf,0,sizeof(buf));
  }
  if (uMsg == WM_COMMAND && wParam == MAKEWPARAM(IDC_EDIT1,EN_CHANGE))
  {
    GetDlgItemText(hwndDlg,IDC_EDIT1,g_regnick,32);
    g_regnick[32]=0;
    g_config->WriteString("nick",g_regnick);
  }
  if (uMsg == WM_COMMAND && wParam == MAKEWPARAM(IDC_EDIT2,EN_CHANGE))
  {
    char buf[255];
    GetDlgItemText(hwndDlg,IDC_EDIT2,buf,256);
    buf[255]=0;
    g_config->WriteString("userinfo",buf);
  }
  if (uMsg == WM_COMMAND && wParam == MAKEWPARAM(IDC_CONSPEED,CBN_SELCHANGE))
  {
    int x=SendDlgItemMessage(hwndDlg,IDC_CONSPEED,CB_GETCURSEL,0,0);
    if (x >= 0 && x < sizeof(conspeed_strs)/sizeof(conspeed_strs[0])) {
      g_config->WriteInt("conspeed",conspeed_speeds[x]);
      int tab[sizeof(conspeed_strs)/sizeof(conspeed_strs[0])]={1,2,2,3,4};
      g_config->WriteInt("keepupnet",tab[x]);
    }
  }
  return 0;
}

static BOOL WINAPI SW_Proc2(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
  if (uMsg == WM_INITDIALOG)
  {
    SetDlgItemText(hwndDlg,IDC_SAVEPATH,g_config->ReadString("downloadpath",""));
    SetDlgItemText(hwndDlg,IDC_FILEPATHLIST,g_config->ReadString("databasepath",""));
  }
  if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDC_ADDDIR) 
    {
      char name[MAX_PATH]="";
      BROWSEINFO bi={0,};
			ITEMIDLIST *idlist;
			bi.hwndOwner = hwndDlg;
			bi.pszDisplayName = name;
			bi.lpfn=BrowseCallbackProc;
			bi.lpszTitle = "Select a directory:";
			bi.ulFlags = BIF_RETURNONLYFSDIRS;
			idlist = SHBrowseForFolder( &bi );
			if (idlist) 
      {
        char buf[4096];
				SHGetPathFromIDList( idlist, name );        
	      IMalloc *m;
	      SHGetMalloc(&m);
	      m->Free(idlist);
        GetDlgItemText(hwndDlg,IDC_FILEPATHLIST,buf,sizeof(buf));
        if (buf[0] && buf[strlen(buf)-1]!=';') strcat(buf,";");
        strcat(buf,name);
				SetDlgItemText(hwndDlg,IDC_FILEPATHLIST,buf);
        g_config->WriteString("databasepath",buf);
			}
    }
    if (wParam == MAKEWPARAM(IDC_FILEPATHLIST,EN_CHANGE) )
    {
      char buf[4096];
      GetDlgItemText(hwndDlg,IDC_FILEPATHLIST,buf,sizeof(buf));
      g_config->WriteString("databasepath",buf);
    }
  }
  if (uMsg == WM_COMMAND && LOWORD(wParam) == IDC_SAVEPATH)
  {
		char name[MAX_PATH];
    BROWSEINFO bi={0,};
		ITEMIDLIST *idlist;
		GetDlgItemText(hwndDlg,IDC_SAVEPATH,name,MAX_PATH);
		bi.hwndOwner = hwndDlg;
		bi.pszDisplayName = name;
		bi.lpfn=BrowseCallbackProc;
		bi.lParam=(LPARAM)GetDlgItem(hwndDlg,IDC_SAVEPATH);
		bi.lpszTitle = "Select a directory:";
		bi.ulFlags = BIF_RETURNONLYFSDIRS;
		idlist = SHBrowseForFolder( &bi );
		if (idlist) 
    {
			SHGetPathFromIDList( idlist, name );        
	    IMalloc *m;
	    SHGetMalloc(&m);
	    m->Free(idlist);
			SetDlgItemText(hwndDlg,IDC_SAVEPATH,name);
      g_config->WriteString("downloadpath",name);
		}
  }
  return 0;
}
static BOOL WINAPI SW_Proc4(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
  if (uMsg == WM_INITDIALOG)
  {
    char buf[128];
    sprintf(buf,"%d keys loaded total",g_pklist.GetSize());
    SetDlgItemText(hwndDlg,IDC_KEYINF,buf);
    char str[1024];
    sprintf(str,"%s.pr4",g_config_prefix);
    FILE *fp=fopen(str,"rb");
    if (fp)
    {
      fclose(fp);
      if (!g_key.bits) reloadKey(NULL,hwndDlg);
    }
    SendMessage(hwndDlg,WM_USER+0x132,0,0);
  }

  if (uMsg == WM_USER+0x132)
  {
    char sign[SHA_OUTSIZE*2+64];
    int x;
    if (g_key.bits)
    {
      strcpy(sign,"Key signature:");
      int t=strlen(sign);
      for (x = 0; x < SHA_OUTSIZE; x ++) sprintf(sign+t+x*2,"%02X",g_pubkeyhash[x]);
      EnableWindow(GetDlgItem(hwndDlg,IDC_BUTTON2),1);
    }
    else
    {
      strcpy(sign,"Key not loaded.");
      EnableWindow(GetDlgItem(hwndDlg,IDC_BUTTON2),0);
    }
    SetDlgItemText(hwndDlg,IDC_KEYINFO,sign);
  }


  if (uMsg == WM_COMMAND) switch (LOWORD(wParam))
  {
      case IDC_BUTTON2:
        copyMyPubKey(hwndDlg);
      return 0;
        case IDC_IMPORT:
          importPubDlg(hwndDlg);
          {
            char buf[128];
            sprintf(buf,"%d keys loaded total",g_pklist.GetSize());
            SetDlgItemText(hwndDlg,IDC_KEYINF,buf);
          }
        return 0;
        case IDC_BUTTON1:
          {
            char parms[2048];
            sprintf(parms,"%s.pr4",g_config_prefix);
            RunKeyGen(hwndDlg,parms);
            SendMessage(hwndDlg,WM_USER+0x132,0,0);
          }
        return 0;
        case IDC_BROWSE:
          {
            char temp[1024];
            OPENFILENAME l={sizeof(l),};
            GetDlgItemText(hwndDlg,IDC_EDIT2,temp,1024);
            l.hwndOwner = hwndDlg;
            l.lpstrFilter = "Private key files (*.prv)\0*.prv\0All files (*.*)\0*.*\0";
            l.lpstrFile = temp;
            l.nMaxFile = 1023;
            l.lpstrTitle = "Import private key file...";
            l.lpstrDefExt = "";

            l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER;
  	                  
            if (GetOpenFileName(&l)) 
            {
              char str[1024];
              sprintf(str,"%s.pr4",g_config_prefix);
              CopyFile(temp,str,FALSE);
              reloadKey(NULL,hwndDlg);
              SendMessage(hwndDlg,WM_USER+0x132,0,1);
            }
          }
        return 0;
  }
  return 0;
}

static void dosw(HWND hwndDlg)
{
  void *procs[]={SW_Proc1,SW_Proc4,SW_Proc2};
  if (sw_wnd) DestroyWindow(sw_wnd);
  sw_wnd=0;
  int tab[]={IDD_WIZ1,IDD_WIZ4,IDD_WIZ2};
  sw_wnd=CreateDialog(g_hInst,MAKEINTRESOURCE(tab[sw_pos]),hwndDlg,(BOOL (WINAPI*)(HWND,UINT,WPARAM,LPARAM))procs[sw_pos]);
  RECT r;
  GetWindowRect(GetDlgItem(hwndDlg,IDC_RECTANGLE),&r);
  ScreenToClient(hwndDlg,(LPPOINT)&r);
  ScreenToClient(hwndDlg,((LPPOINT)&r)+1);
  SetWindowPos(sw_wnd,NULL,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
  ShowWindow(sw_wnd,SW_SHOWNA);
  char str[64];
  sprintf(str,APP_NAME " Profile Setup Wizard (Step %d/4)",sw_pos+1);
  SetWindowText(hwndDlg,str);
}

BOOL WINAPI SetupWizProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      sw_pos=0;
      dosw(hwndDlg);
    return 0;
    case WM_CLOSE:
      if (MessageBox(hwndDlg,"Abort " APP_NAME " Profile Setup Wizard?",APP_NAME,MB_YESNO|MB_ICONQUESTION) == IDYES)
      {
        if (sw_wnd) DestroyWindow(sw_wnd);
        sw_wnd=0;
        EndDialog(hwndDlg,0);
      }
    return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_BACK:
        case IDOK:
          if (LOWORD(wParam) == IDC_BACK) 
          {
            if (sw_pos>0) sw_pos--;
            else SendMessage(hwndDlg,WM_CLOSE,0,0);
          }
          else  // IDOK
          {
            if (sw_pos < 2) sw_pos++;
            else 
            {
              if (sw_wnd) DestroyWindow(sw_wnd);
              sw_wnd=0;
              EndDialog(hwndDlg,1);
              return 0;
            }
          }
          dosw(hwndDlg);
          SetDlgItemText(hwndDlg,IDOK,(sw_pos==2)?"Run":"Next>>");
          SetDlgItemText(hwndDlg,IDC_BACK,(sw_pos==0)?"Quit":"<<Back");
        break;
      }
    return 0;
  }
  return 0;
}



////////////////////////////// profile selection dialog //////////////////////////////////////////


static int pep_mode; //copy,rename,create
static char pep_n[128];

static BOOL WINAPI ProfEditProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      SetWindowText(hwndDlg,pep_mode?pep_mode==2?"Create Profile":"Rename Profile":"Copy Profile");
      SetDlgItemText(hwndDlg,IDC_EDIT1,pep_n);
      SetDlgItemText(hwndDlg,IDC_EDIT2,pep_mode != 1 ? "new profile" : pep_n);
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDOK:
        case IDCANCEL:
          GetDlgItemText(hwndDlg,IDC_EDIT2,pep_n,sizeof(pep_n));
          EndDialog(hwndDlg,LOWORD(wParam)==IDCANCEL);
        return 0;
      }
    return 0;
  }
  return 0;
}

static BOOL WINAPI ProfilesProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
  switch (uMsg)
  {
    case WM_INITDIALOG:

      SetWindowText(hwndDlg,APP_NAME " Profile Manager");

      {
        if (GetPrivateProfileInt("config","showprofiles",0,g_config_mainini))
          CheckDlgButton(hwndDlg,IDC_CHECK1,BST_CHECKED);
      }

      {
        HWND hwndList=GetDlgItem(hwndDlg,IDC_LIST1);
        WIN32_FIND_DATA fd;
        HANDLE h;

        int gotAny=0;
        char tmp[1024];
        getProfilePath(tmp);
        strcat(tmp,"*.pr0");
        h=FindFirstFile(tmp,&fd);
        if (h)
        {
          do
          {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
              char *p=fd.cFileName;
              while (*p) p++;
              while (p >= fd.cFileName && *p != '.') p--;
              if (p > fd.cFileName)
              {
                *p=0;
                if (strlen(fd.cFileName) < sizeof(g_profile_name))
                {
                  SendMessage(hwndList,LB_ADDSTRING,0,(LPARAM)fd.cFileName);
                  gotAny=1;
                }
              }
            }
          }
          while (FindNextFile(h,&fd));
          FindClose(h);
        }
        if (!gotAny) 
        {
          SendMessage(hwndList,LB_ADDSTRING,0,(LPARAM)"Default");
        }
        int l=SendMessage(hwndList,LB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)g_profile_name);
        if (l == LB_ERR) l=SendMessage(hwndList,LB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)"Default");
        if (l != LB_ERR) SendMessage(hwndList,LB_SETCURSEL,(WPARAM)l,0);
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_RENAME:
        case IDC_CLONE:
          {
            int l=SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETCURSEL,0,0);
            if (l != LB_ERR)
            {
              pep_mode=LOWORD(wParam) == IDC_RENAME;
              SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETTEXT,(WPARAM)l,(LPARAM)pep_n);
              char oldfn[1024+1024];
              getProfilePath(oldfn);
              strcat(oldfn,pep_n);
              strcat(oldfn,".pr0");

              if (!DialogBox(g_hInst,MAKEINTRESOURCE(IDD_PROFILE_MOD),hwndDlg,ProfEditProc) && pep_n[0])
              {
                char tmp[1024+1024];
                getProfilePath(tmp);
                strcat(tmp,pep_n);
                strcat(tmp,".pr0");
                if (stricmp(tmp,oldfn))
                {
                  BOOL ret;
                  if (LOWORD(wParam) == IDC_RENAME) ret=MoveFile(oldfn,tmp);
                  else ret=CopyFile(oldfn,tmp,TRUE);
                  if (ret)
                  {
                    tmp[strlen(tmp)-1]++;
                    oldfn[strlen(oldfn)-1]++;
                    if (LOWORD(wParam) == IDC_RENAME) MoveFile(oldfn,tmp);
                    //else CopyFile(oldfn,tmp,FALSE);

                    tmp[strlen(tmp)-1]++;
                    oldfn[strlen(oldfn)-1]++;
                    if (LOWORD(wParam) == IDC_RENAME) MoveFile(oldfn,tmp);
                    else CopyFile(oldfn,tmp,FALSE);

                    tmp[strlen(tmp)-1]++;
                    oldfn[strlen(oldfn)-1]++;
                    if (LOWORD(wParam) == IDC_RENAME) MoveFile(oldfn,tmp);
                    else CopyFile(oldfn,tmp,FALSE);

                    tmp[strlen(tmp)-1]++;
                    oldfn[strlen(oldfn)-1]++;
                    if (LOWORD(wParam) == IDC_RENAME) MoveFile(oldfn,tmp);
                    else CopyFile(oldfn,tmp,FALSE);

                    if (LOWORD(wParam) == IDC_RENAME) SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_DELETESTRING,(WPARAM)l,0);

                    l=SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_ADDSTRING,0,(WPARAM)pep_n);
                    SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_SETCURSEL,(WPARAM)l,0);
                  }
                  else MessageBox(hwndDlg,LOWORD(wParam) == IDC_RENAME ? "Error renaming profile" : "Error copying profile",APP_NAME " Error",MB_OK|MB_ICONEXCLAMATION);
                }
              }
            }
          }
        return 0;
        case IDC_CREATE:
          {
            pep_mode=2;
            strcpy(pep_n,"NULL");
            if (!DialogBox(g_hInst,MAKEINTRESOURCE(IDD_PROFILE_MOD),hwndDlg,ProfEditProc) && pep_n[0])
            {
              char tmp[1024+1024];
              getProfilePath(tmp);
              strcat(tmp,pep_n);
              strcat(tmp,".pr0");
              HANDLE h=CreateFile(tmp,0,0,NULL,CREATE_NEW,0,NULL);
              if (h != INVALID_HANDLE_VALUE)
              {
                CloseHandle(h);
                int l=SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_ADDSTRING,0,(WPARAM)pep_n);
                SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_SETCURSEL,(WPARAM)l,0);
              }
              else MessageBox(hwndDlg,"Error creating profile",APP_NAME " Error",MB_OK|MB_ICONEXCLAMATION);
            }
          }
        return 0;
        case IDC_DELETE:
          {
            int l=SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETCURSEL,0,0);
            if (l != LB_ERR)
            {
              char tmp[1024+1024];
              getProfilePath(tmp);
              SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETTEXT,(WPARAM)l,(LPARAM)(tmp+strlen(tmp)));
              strcat(tmp,".pr0");
              if (DeleteFile(tmp))
              {
                tmp[strlen(tmp)-1]++;
                DeleteFile(tmp);
                tmp[strlen(tmp)-1]++;
                DeleteFile(tmp);
                tmp[strlen(tmp)-1]++;
                DeleteFile(tmp);
                tmp[strlen(tmp)-1]++;
                DeleteFile(tmp);
                SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_DELETESTRING,l,0);
              }
              else MessageBox(hwndDlg,"Error deleting profile",APP_NAME " Error",MB_OK|MB_ICONEXCLAMATION);              
            }
          }
        return 0;
        case IDC_LIST1:
          if (HIWORD(wParam) != LBN_DBLCLK) return 0;
        case IDOK:
          {
            int l=SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETCURSEL,0,0);
            if (l != LB_ERR)
            {
              SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETTEXT,(WPARAM)l,(LPARAM)g_profile_name);
              EndDialog(hwndDlg,IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?3:1); 
            }
          }
        return 0;
        case IDCANCEL: 
          {
            int l=SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETCURSEL,0,0);
            if (l != LB_ERR)
            {
              SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_GETTEXT,(WPARAM)l,(LPARAM)g_profile_name);
            }
          }
          EndDialog(hwndDlg,IsDlgButtonChecked(hwndDlg,IDC_CHECK1)?2:0); 
        return 0;
      }
    return 0;
  }

  return 0;
}

int Prefs_SelectProfile(int force) // returns 1 on user abort
{
  if (g_profile_name[0]) return 0;

  if (force || GetPrivateProfileInt("config","showprofiles",0,g_config_mainini)) // bring up profile manager
  {
    GetPrivateProfileString("config","lastprofile","Default",g_profile_name,sizeof(g_profile_name),g_config_mainini);
    int rv=DialogBox(g_hInst,MAKEINTRESOURCE(IDD_PROFILES),GetDesktopWindow(),ProfilesProc);
    if (!g_profile_name[0]) strcpy(g_profile_name,"Default");
    WritePrivateProfileString("config","showprofiles",(rv&2)?"1":"0",g_config_mainini);
    WritePrivateProfileString("config","lastprofile",g_profile_name,g_config_mainini);
    return !(rv&1);
  }
  strcpy(g_profile_name,"Default");
  char proffn[4096];
  sprintf(proffn,"%s\\%s.pr0",g_config_prefix,g_profile_name);
  if (!GetPrivateProfileInt("profile","valid",0,proffn))
  {
    sprintf(proffn,"%s\\*.pr0",g_config_prefix);
    WIN32_FIND_DATA d;
    HANDLE h=FindFirstFile(proffn,&d);
    if (h != INVALID_HANDLE_VALUE)
    {
      do
      {
        sprintf(proffn,"%s\\%s",g_config_prefix,d.cFileName);
        if (GetPrivateProfileInt("profile","valid",0,proffn))
        {
          char *p=d.cFileName;
          while (*p) p++;
          while (p > d.cFileName && *p != '.') p--;
          if (p > d.cFileName) 
          {
            *p=0;
            safe_strncpy(g_profile_name,d.cFileName,sizeof(g_profile_name));
            break;
          }
        }
      } while (FindNextFile(h,&d));
      FindClose(h);
    }
  }
  return 0;

}