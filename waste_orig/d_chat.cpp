/*
    WASTE - d_chat.cpp (Chat window dialog implementations)
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
#include "childwnd.h"
#include "d_chat.h"
#include "m_chat.h"
#include "srchwnd.h"

#include "resource.h"

#define MAX_CHATTEXT_SIZE 30000

chatroom_item *L_Chatroom;

static void add_chatline(HWND hwndDlg, char *line)
{
  HWND m_hwnd=GetDlgItem(hwndDlg,IDC_CHATTEXT);
  SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_POS|SIF_TRACKPOS,};
  GetScrollInfo(m_hwnd,SB_VERT,&si);

  if (line)
  {
    int oldsels,oldsele;
    SendMessage(m_hwnd,EM_GETSEL,(WPARAM)&oldsels,(LPARAM)&oldsele);
	  if(strlen(line)>MAX_CHATTEXT_SIZE-1) return;

	  char txt[MAX_CHATTEXT_SIZE];
	  GetWindowText(m_hwnd,txt,MAX_CHATTEXT_SIZE-1);
	  txt[MAX_CHATTEXT_SIZE-1]=0;

	  while(strlen(txt)+strlen(line)+4>MAX_CHATTEXT_SIZE)
	  {
		  char *p=txt;
		  while(*p!=0 && *p!='\n') p++;
		  if(*p==0) return;
		  while (*p=='\n') p++;
		  strcpy(txt,p);
      oldsels -= p-txt;
      oldsele -= p-txt;
	  }
    if (oldsels < 0) oldsels=0;
    if (oldsele < 0) oldsele=0;

	  if(txt[0]!=0) strcat(txt,"\n");
	  strcat(txt,line);
    CHARFORMAT2 cf2;
    cf2.cbSize=sizeof(cf2);
    cf2.dwMask=CFM_LINK;
    cf2.dwEffects=0;
    SendMessage(m_hwnd,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&cf2);
	  SetWindowText(m_hwnd,txt);

    GetWindowText(m_hwnd,txt,MAX_CHATTEXT_SIZE-1);
    txt[MAX_CHATTEXT_SIZE-1]=0;

    char *t=txt;
    char lt=' ';
    int sub=0;
    while (*t)
    {
      if (lt == ' ' || lt == '\n' || lt == '\r')
      {
        int isurl=0;
        if (*t == '#' || *t == '&') isurl=1;
        else if (!strnicmp(t,"waste:",6)) isurl=6;
        else if (!strnicmp(t,"http:",5)) isurl=5;
        else if (!strnicmp(t,"ftp:",4)) isurl=4;
        else if (!strnicmp(t,"www.",4)) isurl=4;

        if (isurl && t[isurl] != ' ' && t[isurl] != '\n' && t[isurl] != '\r' && t[isurl])
        {
          int spos=t-txt-sub;
          t+=isurl;
          while (*t && *t != ' ' && *t != '\n' && *t != '\r') { t++; }
          SendMessage(m_hwnd,EM_SETSEL,spos,(t-txt)-sub);
          CHARFORMAT2 cf2;
          cf2.cbSize=sizeof(cf2);
          cf2.dwMask=CFM_LINK;
          cf2.dwEffects=CFE_LINK;
          SendMessage(m_hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf2);
        }
      }
      if (*t == '\n') sub++;
      if (*t) lt=*t++;
    }
    SendMessage(m_hwnd,EM_SETSEL,oldsels,oldsele);
  }
  chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);

  if (GetFocus() == m_hwnd)      
  {
    SendMessage(m_hwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION,si.nTrackPos),0);
  }
  else
  {
    GetScrollInfo(m_hwnd,SB_VERT,&si);
    SendMessage(m_hwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION,si.nMax),0);
    //SendMessage(m_hwnd,EM_LINESCROLL,0,4096);
  }
}

int IsChatRoomDlgMessage(MSG *msg)
{
  chatroom_item *cr=L_Chatroom;
  while (cr)
  {
    if (msg->hwnd == cr->hwnd || IsChild(cr->hwnd,msg->hwnd))
    {
      if (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP) 
      {
        if (msg->wParam == VK_ESCAPE) return 1;
        if (msg->wParam == VK_TAB && msg->hwnd == GetDlgItem(cr->hwnd,IDC_CHATEDIT)) 
        {
          if (cr->channel[0] == '#' || cr->channel[0] == '&')
          {
            SendMessage(cr->hwnd,WM_USER+0x103,0,0);
          }
          return 1;
        }
      }
    }
    if (IsDialogMessage(cr->hwnd,msg)) return 1;
    cr=cr->next;
  }
  return 0;
}

static void gotNick(HWND hwnd, char *nick, int state, int statemask, int delifold)
{
  KillTimer(hwnd,1);
  SetTimer(hwnd,1,1000,NULL);

  HWND hwndTree=GetDlgItem(hwnd,IDC_TREE1);
  HTREEITEM h=TreeView_GetChild(hwndTree,TVI_ROOT);
  TVITEM i;
  int gotme=0;
  while (h)
  {
    char tmp[64];
    tmp[0]=0;
    i.mask=TVIF_TEXT|TVIF_PARAM|TVIF_STATE;
    i.hItem=h;
    i.pszText=tmp;
    i.cchTextMax=sizeof(tmp);
    i.state=0;
    i.stateMask=TVIS_BOLD;
    TreeView_GetItem(hwndTree,&i);
    if (delifold && !nick)
    {
      h=TreeView_GetNextSibling(hwndTree,h);
      if (!stricmp(tmp,g_regnick) && !gotme)
      {
        gotme=1;
      }
      else if (i.lParam)
      {
        int age=GetTickCount() - i.lParam;
        if (!(i.state&TVIS_BOLD))
        {
          if (age > 15000) 
          {
            TreeView_DeleteItem(hwndTree,i.hItem);
            chatroom_item *cli=(chatroom_item *)GetWindowLong(hwnd,GWL_USERDATA);
            if (cli)
            {
              char buf[1024];
              sprintf(buf,"*** %s may have left %s (no response)",i.pszText,cli->channel);
              add_chatline(hwnd,buf);
            }
          }
        }
        else 
        {
          if (age > 5000)  // set it to unbold
          {
            i.mask=TVIF_HANDLE|TVIF_STATE;
            i.state=0;
            i.stateMask=TVIS_BOLD;
            TreeView_SetItem(hwndTree,&i);
          }
        }
      }
    }
    else 
    {
      if (!stricmp(i.pszText,nick)) 
      {
        if (delifold)
        {
          TreeView_DeleteItem(hwndTree,h);
        }
        else
        {
          i.hItem=h;
          i.mask=TVIF_HANDLE|TVIF_PARAM|TVIF_STATE|TVIF_TEXT;
          i.pszText=nick;
          i.lParam=0;
          i.state=state;
          i.stateMask=statemask;
          TreeView_SetItem(hwndTree,&i);
        }
        return;
      }
      h=TreeView_GetNextSibling(hwndTree,h);
    }
  }

  if (!delifold)
  {
    TVINSERTSTRUCT tis;
    tis.hParent=TVI_ROOT;
    tis.hInsertAfter=TVI_SORT;
    tis.item.mask=TVIF_PARAM|TVIF_STATE|TVIF_TEXT;
    tis.item.lParam=0;
    tis.item.pszText=nick;
    tis.item.state=state;
    tis.item.stateMask=statemask;
    TreeView_InsertItem(hwndTree,&tis);
  }
  else if (!nick && !gotme)
  {
    TVINSERTSTRUCT tis;
    tis.hParent=TVI_ROOT;
    tis.hInsertAfter=TVI_SORT;
    tis.item.mask=TVIF_PARAM|TVIF_STATE|TVIF_TEXT;
    tis.item.lParam=0;
    tis.item.pszText=g_regnick;
    tis.item.state=TVIS_BOLD;
    tis.item.stateMask=TVIS_BOLD;
    TreeView_InsertItem(hwndTree,&tis);
  }
}


void formatChatString(char *txt, char *src, char *str, chatroom_item *cnl)
{
  if (!cnl)
  {
    debug_printf("formatChatString: got chat message '%s','%s','%s', but cnl=NULL\n",
      txt,src,str);
    return;
  }
  if (g_chat_timestamp&(1 << (int)(cnl->channel[0]=='#' || cnl->channel[0]=='&')))
  {
    time_t t; struct tm *tm; 
    t = time(NULL);
    tm = localtime(&t);
    if (tm)
    {
      if (g_chat_timestamp&4)
        txt+=sprintf(txt,"[%d/%d/%02d@%02d:%02d:%02d] ",
          tm->tm_mon+1,tm->tm_mday,tm->tm_year%100,
          tm->tm_hour,tm->tm_min, tm->tm_sec);
      else
        txt+=sprintf(txt,"[%02d:%02d:%02d] ",tm->tm_hour,tm->tm_min, tm->tm_sec);
    }
  }

  if (!strncmp(str,"/nick/",6) && str[6])
  {
    if (cnl->channel[0] == '#' || cnl->channel[0] == '&')
    {
      sprintf(txt,"*** %s is now known as %s",str+6,src);
      gotNick(cnl->hwnd,str+6,0,0,1);
    }
    else
    {
      if (str[6] == '#' || str[6] == '&')
      {
        strcpy(txt,"*** invalid message received");
      }
      else
      {
        sprintf(txt,"*** %s is now known as %s",str+6,src);
        safe_strncpy(cnl->channel,src,sizeof(cnl->channel));
        SendMessage(cnl->hwnd,WM_USER_TITLEUPDATE,0,0);
      }
    }
  }
  else if (!strncmp(str,"/iam/",5))
  {
    if (strlen(str)>255) str[255]=0;
    sprintf(txt,"*** %s is %s",src,str+5);
  }
  else if (!strcmp(str,"/join"))
  {
    if (cnl->channel[0] == '#' || cnl->channel[0] == '&')
    {
      sprintf(txt,"*** %s has joined %s",src,cnl->channel);
    }
    else strcpy(txt,"*** invalid message received");
  }
  else if (!strcmp(str,"/leave"))
  {
    if (cnl->channel[0] == '#' || cnl->channel[0] == '&')
    {
      sprintf(txt,"*** %s has left %s",src,cnl->channel);
    }
    else strcpy(txt,"*** invalid message received");
  }
  else if (!strnicmp(str,"/me ",4)) sprintf(txt,"* %s %s",src,str+4);
  else if (!strncmp(str,"//",2)) sprintf(txt,"<%s> %s",src,str+1);
  else if (str[0] != '/')
  {
    sprintf(txt,"<%s> %s",src,str);
  }
  else txt[0]=0;
}

static WNDPROC text_oldWndProc;
static BOOL CALLBACK text_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_CHAR)
  {
    HWND h=GetDlgItem(GetParent(hwndDlg),IDC_CHATEDIT);
    SetFocus(h);
    if (wParam == VK_RETURN)
    {
      SendMessage(GetParent(hwndDlg),WM_COMMAND,IDC_CHAT,0);
      return 0;
    }
    SendMessage(h,uMsg,wParam,lParam);
    return 0;
  }
  return CallWindowProc(text_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}



#if 0//need to make this work
static void chatroom_AddHist(chatroom_item *cli, char *buf)
{
  if (!buf[0]) return;
  int x=CHATEDIT_HISTSIZE;
  while (x-- && cli->chatedit_hist[x]);
  if (x >= 0 && strcmp(buf,cli->chatedit_hist[x]))
  {
    if (x == CHATEDIT_HISTSIZE-1)
    {
      free(cli->chatedit_hist[0]);
      memcpy(cli->chatedit_hist,cli->chatedit_hist+1,CHATEDIT_HISTSIZE-1);
    }
    cli->chatedit_hist[x]=strdup(buf);
  }
}

static WNDPROC edit_oldWndProc;
static BOOL CALLBACK edit_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_KEYDOWN)
  {
    if (wParam == VK_DOWN || wParam == VK_UP)
    {
      if (!(GetAsyncKeyState(VK_CONTROL)&0x8000))
      {
        chatroom_item *cli=(chatroom_item *)GetWindowLong(GetParent(hwndDlg),GWL_USERDATA);
        char buf[1024];
        GetWindowText(hwndDlg,buf,sizeof(buf));

        if (wParam == VK_DOWN)
        {
          chatroom_AddHist(buf);

          if (cli->chatedit_hist_pos == CHATEDIT_HISTSIZE-1)
          {
            SetWindowText(hwndDlg,"");
          }
          else
          {
            cli->chatedit_hist_pos++;
            char *t=cli->chatedit_hist[cli->chatedit_hist_pos];
            if (t) SetWindowText(hwndDlg,t);
            else SetWindowText(hwndDlg,"");
          }
        }
        else 
        {
          if (cli->chatedit_hist_pos > 0)
          {
            cli->chatedit_hist_pos--;
            char *t=cli->chatedit_hist[cli->chatedit_hist_pos];
            if (t) SetWindowText(hwndDlg,t);
            else SetWindowText(hwndDlg,"");
          }
        }
        return 0;
      }
    }
  }
  return CallWindowProc(edit_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}

#endif

void chat_updateTitles()
{
  chatroom_item *p=L_Chatroom;
  while (p)
  {
    SendMessage(p->hwnd,WM_USER_TITLEUPDATE,0,0);
    InvalidateRect(p->hwnd,NULL,FALSE);
    p=p->next;
  }
}

void chat_sendNickChangeFrom(char *oldnick)
{
  char buf[64];
  sprintf(buf,"/nick/%s",oldnick);
  {
    T_Message msg={0,};
    C_MessageChat req;
    req.set_chatstring(buf);
		req.set_dest("&");
		req.set_src(g_regnick);
    msg.data=req.Make();
    msg.message_type=MESSAGE_CHAT;
    if (msg.data)
    {
      msg.message_length=msg.data->GetLength();
      g_mql->send(&msg);
    }
  }
  chatroom_item *p=L_Chatroom;
  while (p)
  {
    T_Message msg={0,};
    C_MessageChat req;
    if (p->channel[0] == '#' || p->channel[0] == '&')
    {
      HWND hwndTree=GetDlgItem(p->hwnd,IDC_TREE1);
      HTREEITEM h=TreeView_GetChild(hwndTree,TVI_ROOT);
      while (h)
      {
        TVITEM i;
        i.mask=TVIF_TEXT|TVIF_STATE;
        i.hItem=h;
        char tx[64];
        i.pszText=tx;
        i.cchTextMax=62;        
        TreeView_GetItem(hwndTree,&i);
        tx[63]=0;

        if (!strcmp(tx,oldnick))
        {
          TreeView_DeleteItem(hwndTree,i.hItem);
          break;
        }
        h=TreeView_GetNextSibling(hwndTree,h);
      }
      TVINSERTSTRUCT tis;
      tis.hParent=TVI_ROOT;
      tis.hInsertAfter=TVI_SORT;
      tis.item.mask=TVIF_PARAM|TVIF_STATE|TVIF_TEXT;
      tis.item.lParam=0;
      tis.item.pszText=g_regnick;
      tis.item.state=TVIS_BOLD;
      tis.item.stateMask=TVIS_BOLD;
      TreeView_InsertItem(hwndTree,&tis);
    }
    p=p->next;
  }
}

static void ChatDiv_UpdPos(chatroom_item *cli, int xp)
{
  RECT r,r2;
  GetClientRect(cli->hwnd,&r);

  GetWindowRect(GetDlgItem(cli->hwnd,cli->resize[4].id),&r2);
  if (cli->wnd_old_xoffs-(r.right-(xp+r2.left)) > 20) return;

  {
    GetWindowRect(GetDlgItem(cli->hwnd,cli->resize[0].id),&r2);
    int newx=xp+r2.left;
    ScreenToClient(cli->hwnd,(LPPOINT)&r2);
    ScreenToClient(cli->hwnd,((LPPOINT)&r2)+1);              
    if (((r2.right-r2.left)+newx) < cli->resize[0].rinfo.left+90) return;
  }

  int x;
  for (x = 0; x < 5; x ++)
  {
    GetWindowRect(GetDlgItem(cli->hwnd,cli->resize[x].id),&r2);
    int newx=xp+r2.left;
    ScreenToClient(cli->hwnd,(LPPOINT)&r2);
    ScreenToClient(cli->hwnd,((LPPOINT)&r2)+1);
    if (x == 0 || x == 4) cli->resize[x].rinfo.right=r.right-((r2.right-r2.left)+newx);
    if (x == 3 || x == 4) cli->resize[x].rinfo.left=r.right-newx;
  }
  cli->lastdivpos=cli->wnd_old_xoffs-cli->resize[4].rinfo.left;

  childresize_resize(cli->hwnd,cli->resize,5);
}

static BOOL CALLBACK chatdiv_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  chatroom_item *cli=(chatroom_item *)GetWindowLong(GetParent(hwndDlg),GWL_USERDATA);
  if (!cli) return 0;
  if (uMsg == WM_LBUTTONDOWN)
  {
    SetForegroundWindow(hwndDlg);
    SetCapture(hwndDlg);
    SetCursor(LoadCursor(NULL,IDC_SIZEWE));
  }
  else if (uMsg == WM_SETCURSOR)
  {
    SetCursor(LoadCursor(NULL,IDC_SIZEWE));
    return TRUE;
  }
  else if (uMsg == WM_MOUSEMOVE && GetCapture()==hwndDlg)
  {    
    POINT p;
    RECT r3;
    GetCursorPos(&p);
    ScreenToClient(GetParent(hwndDlg),(LPPOINT)&p);
    GetWindowRect(hwndDlg,&r3);
    ChatDiv_UpdPos(cli,p.x-r3.left);
  }
  else if (uMsg == WM_MOUSEMOVE)
  {
    SetCursor(LoadCursor(NULL,IDC_SIZEWE));
  }
  else if (uMsg == WM_LBUTTONUP)
  {
    ReleaseCapture();
  }
  return CallWindowProc(cli->chatdiv_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}

  
BOOL WINAPI Chat_DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static unsigned int linkclicktime;
  switch (uMsg)
  {
    case WM_USER+0x103:
      {
        char buf[512],*bufptr=buf;
        int mode=0;
        GetDlgItemText(hwndDlg,IDC_CHATEDIT,buf,sizeof(buf));
        if (buf[0] && !strstr(buf,"/") && !strstr(buf," ") && !strstr(buf,":")) mode=1;
        else if (!strnicmp(buf,"/whois ",7) && buf[7]) mode=7;
        else if (!strnicmp(buf,"/msg ",5) && buf[5]) mode=5;
        else if (!strnicmp(buf,"/query ",7) && buf[7]) mode=7;
        else if (!strnicmp(buf,"/browse ",8) && buf[8]) mode=8;

        if (mode>1)
        {
          bufptr+=mode;
          while (*bufptr == ' ') bufptr++;
          if (!*bufptr || strstr(bufptr,":") || strstr(bufptr,"/")) mode=0;
        }
      
      
        if (mode)
        {
          HWND hwndTree=GetDlgItem(hwndDlg,IDC_TREE1);
          HTREEITEM h=TreeView_GetChild(hwndTree,TVI_ROOT);
          while (h)
          {
            TVITEM i;
            i.mask=TVIF_TEXT;
            i.hItem=h;
            char tx[64+16];
            i.pszText=tx;
            i.cchTextMax=61;
            TreeView_GetItem(hwndTree,&i);

            if (!strnicmp(tx,bufptr,strlen(bufptr)))
            {
              if (mode == 1) strcat(tx,": ");
              else
              {
                buf[mode]=0;
                strcat(buf,tx);
                strcpy(tx,buf);
              }

              SetDlgItemText(hwndDlg,IDC_CHATEDIT,tx);

              POINTL p;
              SendDlgItemMessage(hwndDlg,IDC_CHATEDIT,EM_POSFROMCHAR,(WPARAM)&p,strlen(tx)); // simulate click at the new point
              SendDlgItemMessage(hwndDlg,IDC_CHATEDIT,WM_LBUTTONDOWN,0,MAKELPARAM(p.x,p.y));
              SendDlgItemMessage(hwndDlg,IDC_CHATEDIT,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));
              break;
            }
            h=TreeView_GetNextSibling(hwndTree,h);
          }
        }
      }
    return 0;
    case WM_GETMINMAXINFO:
      {
        LPMINMAXINFO m=(LPMINMAXINFO)lParam;
        if (m)
        {
          m->ptMinTrackSize.x=204;
          m->ptMinTrackSize.y=134;
        }
      }
    return 0;
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
      {
				chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);
        if (cli && (cli->channel[0] == '#' || cli->channel[0] == '&'))
        {
          POINT p;
          RECT r3;
          GetWindowRect(GetDlgItem(hwndDlg,IDC_NAMELISTRESIZER),&r3);
          GetCursorPos(&p);
          int d=p.x-r3.right;
          int d2=p.x-r3.left;
          if (d<0)d=-d;
          if (d2<0)d2=-d2;

          if (d < 6 || d2 < 6) SendDlgItemMessage(hwndDlg,IDC_NAMELISTRESIZER,uMsg,0,0);
        }        
      }
    return 0;
    case WM_CLOSE:
      {
				chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);

        RECT r;
        GetWindowRect(hwndDlg,&r);
        int w=r.right-r.left;
        int h=r.bottom-r.top;

        if (cli && (cli->channel[0] == '#' || cli->channel[0] == '&'))
        {
          T_Message msg={0,};
          C_MessageChat req;
          req.set_chatstring("/leave");
			    req.set_dest(cli->channel);
			    req.set_src(g_regnick);
          msg.data=req.Make();
          msg.message_type=MESSAGE_CHAT;
          if (msg.data)
          {
            msg.message_length=msg.data->GetLength();
            g_mql->send(&msg);
            cli->lastMsgGuid=msg.message_guid;
            cli->lastmsgguid_time=GetTickCount();
			    }
          g_config->WriteInt("chatchan_w",w);
          g_config->WriteInt("chatchan_h",h);
          g_config->WriteInt("chat_divpos",cli->lastdivpos);

        }
        else
        {
          g_config->WriteInt("chatuser_w",w);
          g_config->WriteInt("chatuser_h",h);
        }
        
        if(cli==L_Chatroom) L_Chatroom=cli->next;
				else {
					chatroom_item *p=L_Chatroom;
					while(p->next && p->next!=cli) p=p->next;
					if(p->next) p->next=p->next->next;
				}
				free(cli);
				DestroyWindow(hwndDlg);
      }
    return 0;
    case WM_DROPFILES:
      {
        chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);
        if (cli->channel[0] != '#' && cli->channel[0] != '&')
          UserListOnDropFiles(hwndDlg,NULL,(HDROP)wParam,cli->channel);
        else
          UserListOnDropFiles(hwndDlg,GetDlgItem(hwndDlg,IDC_TREE1),(HDROP)wParam,NULL);
      }
    return 0;
    case WM_SIZE:
      if (wParam == SIZE_MINIMIZED)
      {
        if (g_config->ReadInt("cwhmin",0) || (GetAsyncKeyState(VK_SHIFT)&0x8000)) ShowWindow(hwndDlg,SW_HIDE);
      }
      else {
        chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);
        if (cli) childresize_resize(hwndDlg,cli->resize,5);
        add_chatline(hwndDlg,NULL);
      }
    return 0;
    case WM_TIMER:
      if (wParam == 1)
      {
        gotNick(hwndDlg,NULL,0,0,1);
      }
      if (wParam == 3)
      {
        KillTimer(hwndDlg,3);
        ShowWindow(hwndDlg,SW_SHOW);
        SetForegroundWindow(hwndDlg);
      }
    return 0;
    case WM_INITDIALOG:
      L_Chatroom->hwnd = hwndDlg;
			SetFocus(GetDlgItem(hwndDlg,IDC_CHATEDIT));
			SetWindowLong(hwndDlg,GWL_USERDATA,(long)L_Chatroom);

      text_oldWndProc=(WNDPROC) SetWindowLong(GetDlgItem(hwndDlg,IDC_CHATTEXT),GWL_WNDPROC,(LONG)text_newWndProc);
#if 0 // need to fix this
      edit_oldWndProc=(WNDPROC) SetWindowLong(GetDlgItem(hwndDlg,IDC_CHATEDIT),GWL_WNDPROC,(LONG)edit_newWndProc);
#endif

      if (L_Chatroom->channel[0] == '#' || L_Chatroom->channel[0] == '&')
      {
        L_Chatroom->chatdiv_oldWndProc=(WNDPROC) 
          SetWindowLong(GetDlgItem(hwndDlg,IDC_NAMELISTRESIZER),GWL_WNDPROC,(LONG)chatdiv_newWndProc);

        childresize_init(hwndDlg,L_Chatroom->resize,5);

        L_Chatroom->wnd_old_xoffs=L_Chatroom->resize[4].rinfo.left;
        POINT p={0,0};
        ScreenToClient(hwndDlg,&p);
        ChatDiv_UpdPos(L_Chatroom,g_config->ReadInt("chat_divpos",70)+p.x);

        T_Message msg={0,};
        C_MessageChat req;
        req.set_chatstring("/join");
			  req.set_dest(L_Chatroom->channel);
			  req.set_src(g_regnick);
        msg.data=req.Make();
        msg.message_type=MESSAGE_CHAT;
        if (msg.data)
        {
          msg.message_length=msg.data->GetLength();
          g_mql->send(&msg);
          L_Chatroom->lastMsgGuid=msg.message_guid;
          L_Chatroom->lastmsgguid_time=GetTickCount();
			  }
        HWND hwndTree=GetDlgItem(hwndDlg,IDC_TREE1);
        TVINSERTSTRUCT tis;
        tis.hParent=TVI_ROOT;
        tis.hInsertAfter=TVI_SORT;
        tis.item.mask=TVIF_PARAM|TVIF_STATE|TVIF_TEXT;
        tis.item.lParam=0;
        tis.item.pszText=g_regnick;
        tis.item.state=TVIS_BOLD;
        tis.item.stateMask=TVIS_BOLD;
        TreeView_InsertItem(hwndTree,&tis);
      }
      else childresize_init(hwndDlg,L_Chatroom->resize,4);

      int w,h;
      RECT oldr;
      GetWindowRect(hwndDlg,&oldr);
      if (L_Chatroom->channel[0] == '#' || L_Chatroom->channel[0] == '&')
      {
        w=g_config->ReadInt("chatchan_w",500);
        h=g_config->ReadInt("chatchan_h",300);
      }
      else
      {
        w=g_config->ReadInt("chatuser_w",300);
        h=g_config->ReadInt("chatuser_h",300);
      }
      SetWindowPos(hwndDlg,NULL, 
        oldr.left + (oldr.right-oldr.left - w)/2,
        oldr.top + (oldr.bottom-oldr.top - h)/2,w,h,SWP_NOZORDER|SWP_NOACTIVATE);

    case WM_USER_TITLEUPDATE:
      {
        char buf[64];
        chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);
        if (cli->channel[0] == '#' ||
            cli->channel[0] == '&')
          sprintf(buf,"%s - " APP_NAME " Chat",cli->channel);
        else
          sprintf(buf,"%s - " APP_NAME " User Chat",cli->channel);
			  SetWndTitle(hwndDlg,buf);

        HWND m_hwnd=GetDlgItem(hwndDlg,IDC_CHATTEXT);
        SendMessage(m_hwnd, EM_SETEVENTMASK, 0, ENM_LINK);
//        SendMessage(m_hwnd, EM_AUTOURLDETECT, 1, 0);

        CHARFORMAT fmt={sizeof(fmt),};
				SendMessage(m_hwnd,EM_GETCHARFORMAT,1,(LPARAM)&fmt);

        fmt.dwMask=CFM_SIZE|CFM_FACE|CFM_BOLD|CFM_COLOR|CFM_ITALIC|CFM_STRIKEOUT|CFM_UNDERLINE;
        fmt.dwEffects = g_config->ReadInt("cfont_fx",0);
        fmt.yHeight = g_config->ReadInt("cfont_yh",180);
        fmt.crTextColor = g_config->ReadInt("cfont_color",0);
        if (cli->channel[0] == '#' || cli->channel[0] == '&') TreeView_SetTextColor(GetDlgItem(hwndDlg,IDC_TREE1),fmt.crTextColor);

        HWND editWnd=GetDlgItem(hwndDlg,IDC_CHATEDIT);
        char *p=g_config->ReadString("cfont_face","Fixedsys");
        if (*p)
        {
          safe_strncpy(fmt.szFaceName,p,sizeof(fmt.szFaceName));
          SendMessage(m_hwnd,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&fmt);
          SendMessage(editWnd,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&fmt);

          int bgc=g_config->ReadInt("cfont_bgc",0xFFFFFF);

          LOGBRUSH lb;
          lb.lbColor=bgc;
          lb.lbStyle=BS_SOLID;
          SendMessage(m_hwnd,EM_SETBKGNDCOLOR,FALSE,bgc);
          SendMessage(editWnd,EM_SETBKGNDCOLOR,FALSE,bgc);
          if (cli->channel[0] == '#' || cli->channel[0] == '&') TreeView_SetBkColor(GetDlgItem(hwndDlg,IDC_TREE1),bgc);
        }
        else fmt.yHeight=100;

        // resize edit field
        cli->resize[1].rinfo.top = cli->resize[1].rinfo.bottom + fmt.yHeight/15+12;

        // resize chat output
        cli->resize[0].rinfo.bottom=cli->resize[1].rinfo.top+2;

        // resize invisible chat button
        int old2size=cli->resize[2].rinfo.top-cli->resize[2].rinfo.bottom;
        cli->resize[2].rinfo.bottom=cli->resize[1].rinfo.top+2;
        cli->resize[2].rinfo.top=cli->resize[2].rinfo.bottom+old2size;

        // resize 3rd field, if necessary :)
        if (cli->channel[0] == '#' || cli->channel[0] == '&')
        {
          cli->resize[3].rinfo.bottom=cli->resize[1].rinfo.top+2;
        }
        childresize_resize(hwndDlg,cli->resize,5);
      }
	  return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_CHAT:
					if(!g_regnick[0])
					{
						add_chatline(hwndDlg,"Register your nickname first !");
						SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
						break;
					}
					{
            int isme=0;
  					chatroom_item *cli=(chatroom_item *)GetWindowLong(hwndDlg,GWL_USERDATA);
            char textb[2048];
            GetDlgItemText(hwndDlg,IDC_CHATEDIT,textb,2047);
            char *text=textb;
            while (*text == ' ') text++;
            if (text[0] == '/' && text[1] != '/')
            {
              if (text[1] == 'J' || text[1] == 'j')
              {
                while (*text && *text != ' ') text++;
                while (*text == ' ') text++;
                if(strstr(text,"\r") || strstr(text,"\n") || (*text != '#' && *text != '&'))
                {
								  add_chatline(hwndDlg,"Usage: /join <#channel|&channel>");
                }
                else
                {
                  chat_ShowRoom(text,1);
								  SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
                  break;
                }
              }
              else if (!strnicmp(text+1,"whois",5))
              {
                while (*text && *text != ' ') text++;
                while (*text == ' ') text++;
                if(!*text || strstr(text,"\r") || strstr(text,"\n") || strstr(text,"#") || strstr(text,"&"))
                {
								  add_chatline(hwndDlg,"Usage: /whois <username>");
                }
                else
                {
                  T_Message msg;
                  // send a message to text that is /whois
                  C_MessageChat req;
                  req.set_chatstring("/whois");
							    req.set_dest(text);
							    req.set_src(g_regnick);
                  msg.data=req.Make();
                  msg.message_type=MESSAGE_CHAT;
                  if (msg.data)
                  {
                    msg.message_length=msg.data->GetLength();
                    g_mql->send(&msg);
                  }
                }
              }
              else if (!strnicmp(text+1,"search",6))
              {
                while (*text && *text != ' ') text++;
                while (*text == ' ') text++;
                if(!*text || strstr(text,"\r") || strstr(text,"\n"))
                {
								  add_chatline(hwndDlg,"Usage: /search <searchterm>");
                }
                else
                {
                  SendMessage(g_mainwnd,WM_COMMAND,IDC_SEARCH,0);
                  Search_Search(text);
                  SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
                  return 0;
                }
              }
              else if (!strnicmp(text+1,"browse",6))
              {
                while (*text && *text != ' ') text++;
                while (*text == ' ') text++;
                if(!*text || strstr(text,"\r") || strstr(text,"\n"))
                {
								  add_chatline(hwndDlg,"Usage: /browse <user|path>");
                }
                else
                {
                  if (*text != '/') 
                  {
                    text--;
                    *text='/';
                  }
                  SendMessage(g_mainwnd,WM_COMMAND,IDC_SEARCH,0);
                  Search_Search(text);
                  SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
                  return 0;
                }
              }
              else if (!strnicmp(text+1,"leave",5))
              {
                PostMessage(hwndDlg,WM_CLOSE,0,0);
              }
              else if (!strnicmp(text+1,"clear",5))
              {
                SetDlgItemText(hwndDlg,IDC_CHATTEXT,"");
              }
              else if (!strnicmp(text+1,"query",5))
              {
                while (*text && *text != ' ') text++;
                while (*text == ' ') text++;
                if (!strstr(text,"\r") && !strstr(text,"\n") && *text && text[0] != '#' && text[0] != '&')
                {
                  chat_ShowRoom(text,1);
                  SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
                  break;
                }
                else
                {
								  add_chatline(hwndDlg,"Usage: /query <nickname>");
                }
              }
              else if (!strnicmp(text+1,"me",2))
              {
                if (!strstr(text,"\r") && !strstr(text,"\n") && text[3]==' ' && text[4]) 
                {
                  isme=1;
                  goto senditanyway;
                }
  						  add_chatline(hwndDlg,"Usage: /me <action>");
              }
              else if (!strnicmp(text+1,"msg",3))
              {
                char *dstr;
                if (!strstr(text,"\r") && !strstr(text,"\n") && text[4] == ' ' && text[5] && text[5] != ' ' && 
                    (dstr=strstr(text+5," ")) )
                {
                  T_Message msg={0,};
                  C_MessageChat req;
                  // dstr is the string we are sending
                  if (dstr[1] == '/') 
                  {
                    dstr[0]='/';
                    req.set_chatstring(dstr);
                  }
                  else
                  {
                    req.set_chatstring(dstr+1);
                  }
                  *dstr=0;

							    req.set_dest(text+5);
							    req.set_src(g_regnick);
                  msg.data=req.Make();
                  msg.message_type=MESSAGE_CHAT;
                  if (msg.data)
                  {
                    msg.message_length=msg.data->GetLength();
                    g_mql->send(&msg);
								    char tmp[4096];
                    sprintf(tmp,"-> *%s*: %s",text+5,dstr+1);
								    add_chatline(hwndDlg,tmp);
							    }
                }
                else
                  add_chatline(hwndDlg,"Usage: /msg <user> <text>");
              }
              else if (!strnicmp(text+1,"help",4))
              {
								add_chatline(hwndDlg,"Commands:");
								add_chatline(hwndDlg,"  /msg <user> <text>");
								add_chatline(hwndDlg,"  /query <user>");
								add_chatline(hwndDlg,"  /whois <user>");
								add_chatline(hwndDlg,"  /join <channel>");
								add_chatline(hwndDlg,"  /me <action>");
								add_chatline(hwndDlg,"  /search <searchterm>");
								add_chatline(hwndDlg,"  /browse <user|path>");
								add_chatline(hwndDlg,"  /leave");
								add_chatline(hwndDlg,"  /clear");
              }
              else
              {
								add_chatline(hwndDlg,"Unknown command! Try /help");
              }
            }           
            else if (strlen(text)>0)
            {
senditanyway:
              if (cli->channel[0] == '#' || cli->channel[0] == '&')
              {
                HWND hwndTree=GetDlgItem(hwndDlg,IDC_TREE1);
                HTREEITEM h=TreeView_GetChild(hwndTree,TVI_ROOT);
                while (h)
                {
                  TVITEM i;
                  i.mask=TVIF_PARAM|TVIF_STATE;
                  i.hItem=h;
                  i.state=0;
                  i.stateMask=TVIS_BOLD;
                  TreeView_GetItem(hwndTree,&i);

                  if (i.state || !i.lParam)
                  {
                    i.hItem=h;
                    i.mask=TVIF_HANDLE|TVIF_PARAM;
                    i.lParam=GetTickCount();
                    TreeView_SetItem(hwndTree,&i);
                  }
                  h=TreeView_GetNextSibling(hwndTree,h);
                }
              }
              else
              {
                SetDlgItemText(hwndDlg,IDC_STATUS,"Sent message, awaiting reply");
              }

              int l=0;
              while (text && *text && l++ < 16)
              {
                while (*text == '\r' || *text == '\n') text++;
                while (*text == ' ') text++;
                
                char *lasttext=text;
                while (*text && *text != '\r' && *text != '\n') text++;
                if (*text) *text++=0;
                if (!*lasttext) break;

                T_Message msg={0,};
                C_MessageChat req;

                char buf[1024];
                if (lasttext[0] == '/' && lasttext[1] != '/' && !isme)
                {
                  safe_strncpy(buf+1,lasttext,sizeof(buf)-1);
                  buf[0]='/';
                }
                else 
                {
                  safe_strncpy(buf,lasttext,sizeof(buf));
                }
                isme=0;
                req.set_chatstring(buf);

							  req.set_dest(cli->channel);
							  req.set_src(g_regnick);
                msg.data=req.Make();
                msg.message_type=MESSAGE_CHAT;
                if (msg.data)
                {
                  msg.message_length=msg.data->GetLength();
                  g_mql->send(&msg);
                  cli->lastMsgGuid=msg.message_guid;
                  cli->lastmsgguid_time=GetTickCount();
								  char tmp[4096];
                  formatChatString(tmp,req.get_src(),buf,cli);
								  if (tmp[0]) add_chatline(hwndDlg,tmp);
							  }
              }
							SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
						}
						SetDlgItemText(hwndDlg,IDC_CHATEDIT,"");
						SetFocus(GetDlgItem(hwndDlg,IDC_CHATEDIT));
					}
					break;
			}
		  return 0;
      case WM_NOTIFY:
      {
       LPNMHDR l=(LPNMHDR)lParam; 
       if (l->idFrom == IDC_TREE1)
       {
         if (l->code == NM_RCLICK)
         {
           UserListContextMenu(l->hwndFrom);
         }
         else if (l->code == NM_DBLCLK)
         {
            HWND htree=l->hwndFrom;
            HTREEITEM h=TreeView_GetSelection(htree);
            if (h)
            {
              char text[256];
              TVITEM i;
              i.mask=TVIF_TEXT|TVIF_HANDLE;
              i.hItem=h;
              i.pszText=text;
              i.cchTextMax=sizeof(text);
              text[0]=0;
              TreeView_GetItem(htree,&i);
              if (text[0])
              {
                chat_ShowRoom(text,2);
                return 1;
              }
            }
         }
       }
       else if (l->idFrom == IDC_CHATTEXT)
       {
         if (l->code == EN_LINK)
         {
           ENLINK *el=(ENLINK*)lParam;
           if(el->msg==WM_LBUTTONDOWN) linkclicktime=GetTickCount();
           if(el->msg==WM_LBUTTONUP && GetTickCount()-linkclicktime < 5000) 
           {
             linkclicktime=0;
             char tmp[1024];
             HWND h=l->hwndFrom;
             TEXTRANGE r;
             r.chrg=el->chrg;
             r.lpstrText=tmp;
             if (r.chrg.cpMax-r.chrg.cpMin < sizeof(tmp)-4)
             {
               SendMessage(h,EM_GETTEXTRANGE,0,(LPARAM)&r);

               if (!strnicmp(tmp,"waste:",6))
               {
                 handleWasteURL(tmp);
               }
               else if (tmp[0] == '#' || tmp[0] == '&')
               {
                 chat_ShowRoom(tmp,2);
               }
               else if (tmp[0])
               {
                 if (!strnicmp(tmp,"www.",4))
                 {
                   char tmp2[1024+32];
                   strcpy(tmp2,"http://");
                   strcat(tmp2,tmp);
                   safe_strncpy(tmp,tmp2,sizeof(tmp));
                 }
                 ShellExecute(NULL,"open",tmp,NULL,".",0);
               }
             }
           }
         }
       }
     }
		 return 0;
  }
  return 0;
}




chatroom_item *chat_ShowRoom(char *channel, int activate)
{
  int n=0;
	chatroom_item *p=L_Chatroom;
	while(p!=NULL)
	{
		if(!stricmp(p->channel,channel))
		{
			if (activate) 
      {
        WINDOWPLACEMENT wp={sizeof(wp),};
        GetWindowPlacement(p->hwnd,&wp);
        if (wp.showCmd == SW_SHOWMINIMIZED) 
          ShowWindow(p->hwnd,SW_RESTORE);
        else
          SetWindowPos(p->hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        if (activate == 2)
        {
          SetTimer(p->hwnd,3,250,NULL);
        }
      }
			return p;
		}
		p=p->next;
    n++;
	}

  if (g_config->ReadInt("limitchat",1) && g_config->ReadInt("limitchatn",64) <= n)
  {
    return NULL;
  }
	
	chatroom_item *cli=(chatroom_item *)malloc(sizeof(chatroom_item));
  memset(cli,0,sizeof(chatroom_item));
  cli->resize[0].id=IDC_CHATTEXT;
  cli->resize[0].type=0x0011;
  cli->resize[1].id=IDC_CHATEDIT;
  cli->resize[1].type=0x0111;
  cli->resize[2].id=IDC_CHAT;
  cli->resize[2].type=0x1111;
  int resid;
  if (channel[0] == '#' || channel[0] == '&')
  {
    if (channel[0] == '#') main_onGotChannel(channel);
    resid=IDD_CHATCHAN;
    cli->resize[3].id=IDC_TREE1;
    cli->resize[3].type=0x1011;
    cli->resize[4].id=IDC_NAMELISTRESIZER;
    cli->resize[4].type=0x1011;
  }
  else
  {
    resid=IDD_CHATPRIV;
    cli->resize[3].id=IDC_STATUS;
    cli->resize[3].type=0x0111;
  }
	strcpy(cli->channel,channel);
	cli->next=L_Chatroom;
	L_Chatroom=cli;

	CreateDialog(g_hInst,MAKEINTRESOURCE(resid),NULL,Chat_DlgProc);
  if (activate == 2)
  {
    SetTimer(cli->hwnd,3,250,NULL);
  }
  else if (activate) ShowWindow(cli->hwnd,SW_SHOWNA);
  else ShowWindow(cli->hwnd,SW_SHOW);

  return cli;
}

static WNDPROC chatrooms_oldWndProc;
static BOOL CALLBACK chatrooms_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if ((uMsg == WM_KEYDOWN || uMsg == WM_KEYUP)&&(wParam == VK_RETURN))
  {
      if (uMsg == WM_KEYUP) SendMessage(GetParent(hwndDlg),WM_COMMAND,IDC_CREATE,0);
      return 0;
  }
  return CallWindowProc(chatrooms_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}


int chat_HandleMsg(T_Message *message)
{
	chatroom_item *p=L_Chatroom;

  if (message->message_type == MESSAGE_CHAT_REPLY)
  {
    C_MessageChatReply repl(message->data);
    char *n=repl.getnick();
    if (strlen(n) < 24 && n[0] && n[0] != '.') main_onGotNick(n,0);
	  while(p!=NULL)
	  {
		  if(!memcmp(&p->lastMsgGuid,&message->message_guid,16))
		  {
        if (n[0])
        {
          // see if nick is already in list
          if (p->channel[0] == '#' || p->channel[0] == '&')
          {
            gotNick(p->hwnd,n,TVIS_BOLD,TVIS_BOLD,0);
          }
          else
          {
            char buf[128];
            char *tmp="Message sent, delivery confirmed";
            GetDlgItemText(p->hwnd,IDC_STATUS,buf,sizeof(buf));
            if (strncmp(buf,tmp,strlen(tmp)))
            {
              sprintf(buf,"%s - (%dms roundtrip)",tmp,GetTickCount()-p->lastmsgguid_time);
            }
            else
            {
              int a=1;
              char *q=buf+strlen(tmp)+1;
              if (*q == '(') a=atoi(q+1);
              sprintf(buf,"%s (%d recipients) - (%dms max roundtrip).",tmp,a+1,GetTickCount()-p->lastmsgguid_time);
            }
            SetDlgItemText(p->hwnd,IDC_STATUS,buf);
          }
        }
        break;
		  }
		  p=p->next;
	  }
    return !!p;
  }
  int retval=0;
	C_MessageChat chat(message->data);
  char *cnl=chat.get_dest();
  {
    char *n=chat.get_src();
    if (n[0] && strlen(n)<24 && n[0] != '.')
      main_onGotNick(n,0);
  }
  if (cnl && *cnl == '#')
  {
    main_onGotChannel(cnl);
  }

  if (!strcmp(chat.get_chatstring(),"/whois"))
  {
    if (!stricmp(chat.get_dest(),g_regnick) && g_regnick[0])
    {
      char buf[256+512+32];
      sprintf(buf,"/iam/%s (%s",g_config->ReadString("userinfo",APP_NAME " User"),g_nameverstr);
      int length=0;
      if (g_database) length=g_database->GetNumMB();
      else if (g_newdatabase) length=g_newdatabase->GetNumMB();

      if (length)
      {
        if (length < 1024)
          sprintf(buf+strlen(buf),", %u MB public",length);
        else sprintf(buf+strlen(buf),", %u.%u GB public",length>>10,((length%1024)*10)/1024);
      }
      strcat(buf,")");
      C_MessageChat req;
      req.set_chatstring(buf);
	    req.set_dest(chat.get_src());
	    req.set_src(g_regnick);
      T_Message msg;
      msg.data=req.Make();
      msg.message_type=MESSAGE_CHAT;
      if (msg.data)
      {
        msg.message_length=msg.data->GetLength();
        g_mql->send(&msg);
      }
    }
    return 0;
  }

  if (!strncmp(chat.get_chatstring(),"/nick/",6) && chat.get_chatstring()[6] && !strcmp(chat.get_dest(),"&"))
  {
    main_onGotNick(chat.get_chatstring()+6,1);

	  if (chat.get_chatstring()[6] != '#' && chat.get_chatstring()[6] != '&') while(p!=NULL)
	  {
      int isgood=0;
      if (p->channel[0] == '#' || p->channel[0] == '&')
      {
        HWND hwndTree=GetDlgItem(p->hwnd,IDC_TREE1);
        HTREEITEM h=TreeView_GetChild(hwndTree,TVI_ROOT);
        while (h)
        {
          TVITEM i;
          i.mask=TVIF_TEXT;
          i.hItem=h;
          char tx[64];
          i.pszText=tx;
          i.cchTextMax=63;
          TreeView_GetItem(hwndTree,&i);
          tx[63]=0;

          if (!stricmp(tx,chat.get_chatstring()+6))
          {
            isgood=1;
            gotNick(p->hwnd,chat.get_src(),TVIS_BOLD,TVIS_BOLD,0);
            break;
          }
          h=TreeView_GetNextSibling(hwndTree,h);
        }        
      }

      if (isgood || !stricmp(p->channel,chat.get_chatstring()+6))
      {
			  char txt[4096];
        formatChatString(txt,chat.get_src(),chat.get_chatstring(),p);
			  if (txt[0]) add_chatline(p->hwnd,txt);
        retval=1;
      }
      p=p->next;
    }
    return 0;
  }

#if 0
	while(p!=NULL)
	{
    if (!strcmp(p->channel,"/sniff/"))
    {
			char txt[4096];
      sprintf(txt,"<%s/%s>: %s",chat.get_src(),chat.get_dest(),chat.get_chatstring());
			add_chatline(p->hwnd,txt);
      break;
    }
    p=p->next;
  }
  p=L_Chatroom;
#endif

  if (g_regnick[0] && !stricmp(cnl,g_regnick)) // privmsg
  {
	  while(p!=NULL)
	  {
		  if(!stricmp(p->channel,chat.get_src()))
		  {
			  char txt[4096];
        formatChatString(txt,chat.get_src(),chat.get_chatstring(),p);
			  if (txt[0]) 
        {
          add_chatline(p->hwnd,txt);
          int f=g_config->ReadInt("gayflashp",65);
          if (f&1) DoFlashWindow(p->hwnd,(f&2)?f/4:0);
          retval=1;
        }
        break;
		  }
		  p=p->next;
	  }
    if (!p && g_config->ReadInt("allowpriv",1)) // time to create a privchat
    {
  	  char txt[4096];
      p=chat_ShowRoom(chat.get_src(),1);
			formatChatString(txt,chat.get_src(),chat.get_chatstring(),p);
      if (txt[0])
      {
			  add_chatline(p->hwnd,txt);
        int f=g_config->ReadInt("gayflashp",65);
        if (f&1) DoFlashWindow(p->hwnd,(f&2)?f/4:0);
        retval=1;
      }
    }
  }
  else if (cnl[0] == '#' || cnl[0] == '&')
  {
	  while(p!=NULL)
	  {
		  if(!stricmp(p->channel,cnl))
		  {
			  char txt[4096];
        gotNick(p->hwnd,chat.get_src(),TVIS_BOLD,TVIS_BOLD,!strcmp(chat.get_chatstring(),"/leave"));
        formatChatString(txt,chat.get_src(),chat.get_chatstring(),p);
			  if (txt[0]) 
        {
          add_chatline(p->hwnd,txt);
          int f=g_config->ReadInt("gayflash",64);
          if (f&1) DoFlashWindow(p->hwnd,(f&2)?f/4:0);
          retval=1;
        }
        break;
		  }
		  p=p->next;
	  }
  }
  return retval;
}



BOOL WINAPI CreateChat_DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG) // set default values, if any
  {
    char *p=(char *)lParam;
    SendDlgItemMessage(hwndDlg,IDC_EDIT1,EM_LIMITTEXT,31,0);
    if (p && *p)
    {
      SetDlgItemText(hwndDlg,IDC_EDIT1,p);
      SendDlgItemMessage(hwndDlg,IDC_EDIT1,EM_SETSEL,0,strlen(p));
    }
    EnableWindow(GetDlgItem(hwndDlg,IDOK),p && p[0]);
    SetWndTitle(hwndDlg,"Join / Create Chat Room - " APP_NAME);
  }
  if (uMsg == WM_CLOSE) PostMessage(hwndDlg,WM_COMMAND,IDCANCEL,0);
  if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDOK)
    {
      char buf[128];
      GetDlgItemText(hwndDlg,IDC_EDIT1,buf,sizeof(buf));

      chatroom_item *p=chat_ShowRoom(buf,1);
      if (p) SetForegroundWindow(p->hwnd);

      EndDialog(hwndDlg,0);
    }
    else if (LOWORD(wParam) == IDCANCEL)
    {
      EndDialog(hwndDlg,0);
    }
    else if (LOWORD(wParam) == IDC_EDIT1 && HIWORD(wParam) == EN_CHANGE)
    {
      char buf[128];
      GetDlgItemText(hwndDlg,IDC_EDIT1,buf,sizeof(buf));
      EnableWindow(GetDlgItem(hwndDlg,IDOK),buf[0]);
    }
  }
  return 0;
}
