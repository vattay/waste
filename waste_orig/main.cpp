/*
    WASTE - main.cpp (Windows main entry point and a lot of code :)
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

/*

---- some things that would be good to do eventually:

  permissions per key:
    C=chat
    S=search/browse out
    s=search/browsable
    X=file transfer
    K=key distribution
    U=uploads
    P=pings/ips/etc
    ?=unknown (all others)
      have the connection keep track of the flags and key hash, then have the
      prefs update all connections' flags.

  improve ip list sorting (use a sorted listview?)

  plugin interface
  ipv6 support
  Ephemeral Diffie-Hellman for session key exchange?
    




---- bits about how the negotiation works:

RNG: based on RSAREF's, but extended (uses 32 byte state, with 16 bytes counter, and 16 bytes of system entropy 
constantly mixed in -- this takes from when messages arrive, mouse movement, timing information on when connections
come up, etc).


On connection:


1)
  A sends B 16 random bytes (bfhpkA)
  A sends B blowFish(bfhpkA,20 byte SHA-1 of public key + 4 pad bytes)
  B sends A 16 random bytes (bfhpkB)
  B sends A blowFish(bfhpkB,20 byte SHA-1 of public key + 4 pad bytes)

2)
  if A knows B's public key, and B knows A public key, continue.

  A sends B: RSA(pubkey_B,sKey1 + bfhpkB), sKey1 is 64 random bytes. (+ = concat)
  B sends A: RSA(pubKey_A,sKey2 + bfhpkA), sKey2 is 64 random bytes. (+ = concat)

  Check to make sure bfhpkA and bfhpkB are correct, and to make sure sKey1^sKey2 is nonzero,
  with the last 8 bytes of sKey1 differing from the last 8 bytes of sKey2.

3)
  each side initializes blowfish using the first 56 bytes sKey1^sKey2 as key.
  A uses the last 8 bytes of sKey2 as recv CBC IV.
  B uses the last 8 bytes of sKey1 as recv CBC IV.

4) 
  each side sends a 16 byte signature, blowfished.  
  each side verifies 16 byte signature.
  message communication begins.
  further messages are verified with a MD5 to detect tampering.

 */



#include "main.h"
#include "resource.h"
#include "rsa/md5.h"

#include "childwnd.h"
#include "m_upload.h"
#include "m_chat.h"
#include "m_search.h"
#include "m_ping.h"
#include "m_keydist.h"
#include "m_lcaps.h"
#include "d_chat.h"
#include "prefs.h"
#include "xferwnd.h"
#include "netq.h"
#include "netkern.h"
#include "srchwnd.h"

#define VERSION "v1.0b"

char *g_nameverstr=APP_NAME " " VERSION;
char *g_def_extlist="ppt;doc;xls;txt;zip;";

C_FileDB *g_database, *g_newdatabase;
C_AsyncDNS *g_dns;
C_Listen *g_listen;
C_MessageQueueList *g_mql;
C_Config *g_config;

HMENU g_context_menus;
HWND g_mainwnd;
HICON g_hSmallIcon;
HINSTANCE g_hInst;

int g_hidewnd_state;

unsigned char g_networkhash[SHA_OUTSIZE];
int g_use_networkhash;

char g_config_prefix[1024];
char g_config_mainini[1024];

char g_profile_name[128];
int g_extrainf;
int g_keepup;
int g_conspeed,g_route_traffic;
int g_do_log;
int g_max_simul_dl;
unsigned int g_max_simul_dl_host;
int g_forceip, g_forceip_addr;
int g_use_accesslist;
int g_appendprofiletitles;
int g_do_autorefresh;
int g_accept_downloads;
int g_port;
int g_chat_timestamp;
int g_keydist_flags;

R_RSA_PRIVATE_KEY g_key;
unsigned char g_pubkeyhash[SHA_OUTSIZE];

char g_regnick[32];

char g_filedlg_ulpath[256];

int g_throttle_flag, g_throttle_send, g_throttle_recv;

int g_search_showfull,g_search_showfullbytes;

int g_scanloadhack;
char g_scan_status_buf[128];
time_t g_next_refreshtime;

time_t g_last_pingtime,g_last_bcastkeytime;
T_GUID g_last_scanid;
int g_last_scanid_used;

T_GUID g_last_pingid;
int g_last_pingid_used;
T_GUID g_client_id;
char g_client_id_str[33];


static C_ItemList<C_UploadRequest> uploadPrompts;
static C_ItemList<C_KeydistRequest> keydistPrompts;


void main_BroadcastPublicKey(T_Message *src)
{
  C_KeydistRequest rep;
  rep.set_nick(g_regnick);
  rep.set_flags((g_port && g_listen && !g_listen->is_error())?M_KEYDIST_FLAG_LISTEN:0);
  R_RSA_PUBLIC_KEY k;
  k.bits=g_key.bits;
  memcpy(k.exponent,g_key.publicExponent,MAX_RSA_MODULUS_LEN);
  memcpy(k.modulus,g_key.modulus,MAX_RSA_MODULUS_LEN);
  rep.set_key(&k);

  T_Message msg={0,};
	msg.data=rep.Make();
  if (msg.data)
  {
    if (src)
    {
      msg.message_guid=src->message_guid;
      msg.message_type=MESSAGE_KEYDIST_REPLY;
    }
    else
    {
	    msg.message_type=MESSAGE_KEYDIST;
    }
		msg.message_length=msg.data->GetLength();
    g_mql->send(&msg);
  }
}

static void main_handleKeyDist(C_KeydistRequest *kdr, int pending)
{ 
  if (!kdr->get_key()->bits) return;
  if (findPublicKeyFromKey(kdr->get_key())) return;

  PKitem *p=(PKitem *)malloc(sizeof(PKitem));

  SHAify m;
  safe_strncpy(p->name,kdr->get_nick(),sizeof(p->name));
  p->pk = *kdr->get_key();
  m.add((unsigned char *)p->pk.modulus,MAX_RSA_MODULUS_LEN);
  m.add((unsigned char *)p->pk.exponent,MAX_RSA_MODULUS_LEN);
  m.final(p->hash);

  if (pending) 
    g_pklist_pending.Add(p);
  else 
    g_pklist.Add(p);
  
  savePKList();
}

static void main_handleUpload(char *guidstr, char *fnstr, C_UploadRequest *t)
{
  int willq=Xfer_WillQ(fnstr,guidstr);

  int p=g_lvrecvq.InsertItem(g_lvrecvq.GetCount(),fnstr,0);
  char sizebuf[64];
  strcpy(sizebuf,"?");
  int fs_l,fs_h;
  t->get_fsize(&fs_l,&fs_h);
  if (fs_l != -1 || fs_h != -1)
  {
    FormatSizeStr64(sizebuf,fs_l,fs_h);
  }

  g_lvrecvq.SetItemText(p,1,sizebuf);
  g_lvrecvq.SetItemText(p,2,guidstr);
  g_files_in_download_queue++;
  if (g_config->ReadInt("aorecv",1))
  {
    HWND h=GetForegroundWindow();
    SendMessage(g_mainwnd,WM_COMMAND,ID_VIEW_TRANSFERS,0);

    if (g_config->ReadInt("aorecv_btf",0)) SetForegroundWindow(g_xferwnd);
    else SetForegroundWindow(h);

    XferDlg_SetSel(willq);
  }
  RecvQ_UpdateStatusText();
}



void main_MsgCallback(T_Message *message, C_MessageQueueList *_this, C_Connection *cn)
{
	switch(message->message_type)
	{
    case MESSAGE_LOCAL_SATURATE:
      //debug_printf("got a %d byte saturation message\n",40+message->message_length);
    break;
    case MESSAGE_LOCAL_CAPS:
      {
        C_MessageLocalCaps mlc(message->data);
        int x;
        for (x = 0; x < mlc.get_numcaps(); x ++)
        {
          int n,v;
          mlc.get_cap(x,&n,&v);
          switch (n)
          {
            case MLC_SATURATION:
              debug_printf("got request that saturation be %d on this link\n",v);
              if (cn) cn->set_saturatemode(v); // if ((get_saturatemode()&1) && (g_throttle_flag&32)) do outbound saturation
            break;
            case MLC_BANDWIDTH:
              debug_printf("got request that max sendbuf size be %d on this link\n",v);
              if (cn) cn->set_max_sendsize(v);
            break;
          }
        }
      }

    break;
    case MESSAGE_KEYDIST_REPLY:
    case MESSAGE_KEYDIST:
      
      {
        C_KeydistRequest *r=new C_KeydistRequest(message->data);
        if ((r->get_flags() & M_KEYDIST_FLAG_LISTEN) || (g_port && g_listen && !g_listen->is_error()) && r->get_key()->bits)
        {
          if (g_keydist_flags&1) // add without prompt
          {
            main_handleKeyDist(r,0);
            delete r;
          }
          else if (g_keydist_flags&2) // add with prompt
          {
            keydistPrompts.Add(r);
          }
          else // add to pending
          {
            main_handleKeyDist(r,1);
            delete r;
          }

          if (message->message_type != MESSAGE_KEYDIST_REPLY)
          {
            main_BroadcastPublicKey(message);
          }
        }
        else delete r;
      }
    break;
		case MESSAGE_PING:
		{
      MYSRANDUPDATE((unsigned char *)&message->message_guid,16);
			C_MessagePing rep(message->data);
	     
      if (rep.m_port && rep.m_ip && (int)cn->get_interface() != rep.m_ip) 
            add_to_netq(rep.m_ip,(int)(unsigned short)rep.m_port,90,0);

      if (rep.m_nick[0] && rep.m_nick[0] != '#' && rep.m_nick[0] != '&' &&
          rep.m_nick[0] != '.' && strlen(rep.m_nick)<24)
        main_onGotNick(rep.m_nick,0);
		}
		break;
    case MESSAGE_SEARCH_USERLIST:
      if (g_regnick[0] && g_regnick[0] != '.' && strlen(g_regnick)<24)
      {
			  C_MessageSearchReply repl;
			  repl.set_conspeed(g_conspeed);
			  repl.set_guid(&g_client_id);
  		  repl.add_item(-1,g_regnick,"Node",g_database->GetNumFiles(),g_database->GetNumMB(),g_database->GetLatestTime());
        T_Message msg={0,};
        msg.message_guid=message->message_guid;
        msg.data=repl.Make();
			  if (msg.data)
			  {
	        msg.message_type=MESSAGE_SEARCH_REPLY;
				  msg.message_length=msg.data->GetLength();
  			  _this->send(&msg);
			  }
      }
    break;
		case MESSAGE_SEARCH:
      if ((g_accept_downloads&1) && g_database->GetNumFiles()>0)
		  {
	      C_MessageSearchRequest req(message->data);
        char *ss=req.get_searchstring();
        if (
             (ss[0] == '/' && (g_accept_downloads&4)) || (ss[0] && ss[0] != '/' && (g_accept_downloads&2))
           )
			  {
				  C_MessageSearchReply repl;
				  repl.set_conspeed(g_conspeed);
				  repl.set_guid(&g_client_id);
  		    g_database->Search(ss,&repl,_this,message,main_MsgCallback);
        }
		  }
		break;
		case MESSAGE_SEARCH_REPLY:
		{
      if (g_last_scanid_used && !memcmp(&g_last_scanid,&message->message_guid,16))
      {
        C_MessageSearchReply repl(message->data);
        if (repl.get_numitems()==1)
        {
				  char name[1024],metadata[256];
			    if (!repl.get_item(0,NULL,name,metadata,NULL,NULL,NULL))
              main_onGotNick(name,0);
        }
      }
	    else Search_AddReply(message);
		}
		break;
    case MESSAGE_FILE_REQUEST:
      {
        C_FileSendRequest *r = new C_FileSendRequest(message->data);

        if (!memcmp(r->get_guid(),&g_client_id,sizeof(g_client_id)))
        {
          int n=g_sends.GetSize();
          int x;
          for (x = 0; x < n; x ++)
          {
            if (!memcmp(r->get_prev_guid(),g_sends.Get(x)->get_guid(),16))
            {
              if (r->is_abort()==2)
              {
                int a=g_sends.Get(x)->GetIdx()-UPLOAD_BASE_IDX;
                if (a >= 0 && a < g_uploads.GetSize())
                {
                  char *p=g_uploads.Get(a);
                  if (p)
                  {
                    int lvidx;
                    while ((lvidx=g_lvsend.FindItemByParam((int)p)) >= 0)
                    {
                      g_lvsend.DeleteItem(lvidx);
                    }
                    free(p);
                    g_uploads.Set(a,NULL);
                  }
                }
              }
              
              g_sends.Get(x)->set_guid(&message->message_guid);
              g_sends.Get(x)->onGotMsg(r);
              break;
            }
          }
          if (x == n && !r->is_abort()) // new file request
          {
            if (g_config->ReadInt("limit_uls",1) && n < g_config->ReadInt("ul_limit",160))
            {
              char fn[2048];
              fn[0]=0;
              int idx=r->get_idx();
              if (idx >= 0)
              {
                if (idx < UPLOAD_BASE_IDX)
                {
                  if (!(g_accept_downloads&1)) fn[0]=0;
                  else g_database->GetFile(idx,fn,NULL,NULL,NULL);
                }
                else
                {
                  idx-=UPLOAD_BASE_IDX;
                  if (idx<g_uploads.GetSize() && g_uploads.Get(idx)) 
                  {
                    safe_strncpy(fn,g_uploads.Get(idx),sizeof(fn));
                    int lvidx;
                    if ((lvidx=g_lvsend.FindItemByParam((int)g_uploads.Get(idx))) >= 0)
                    {
                      g_lvsend.DeleteItem(lvidx);
                    }
                  }
                }
              }
              if (fn[0])
              {
                XferSend *a=new XferSend(_this,&message->message_guid,r,fn);

                // remove any timed out files of a->GetName() from r->get_nick()
                n=g_lvsend.GetCount();
                for (x = 0; x < n; x ++)
                {
                  if (g_lvsend.GetParam(x)) continue;
                  char buf[1024];
                  g_lvsend.GetText(x,3,buf,sizeof(buf));
                  if (strncmp(buf,"Timed out",9)) continue;
                  g_lvsend.GetText(x,0,buf,sizeof(buf));
                  if (strcmp(buf,a->GetName())) continue;
                  g_lvsend.GetText(x,1,buf,sizeof(buf));
                  if (strcmp(buf,r->get_nick())) continue;
                  g_lvsend.DeleteItem(x);
                  x--;
                  n--;
                }

                char *err=a->GetError();
                if (err)
                {
                  if (!g_config->ReadInt("send_autoclear",0))
                  {
                    g_lvsend.InsertItem(0,a->GetName(),0);
                    char buf[32];
                    int fs_l,fs_h;
                    a->GetSize((unsigned int *)&fs_l,(unsigned int *)&fs_h);
                    FormatSizeStr64(buf,fs_l,fs_h);
                    g_lvsend.SetItemText(0,1,r->get_nick());
                    g_lvsend.SetItemText(0,2,buf);
                    g_lvsend.SetItemText(0,3,err);
                  }
                  delete a;
                }
                else
                {
                  g_sends.Add(a);
                  g_lvsend.InsertItem(0,a->GetName(),(int)a);
                  char buf[32];
                  int fs_l,fs_h;
                  a->GetSize((unsigned int *)&fs_l,(unsigned int *)&fs_h);
                  FormatSizeStr64(buf,fs_l,fs_h);
                  g_lvsend.SetItemText(0,1,r->get_nick());
                  g_lvsend.SetItemText(0,2,buf);
                  g_lvsend.SetItemText(0,3,"Sending");
                }
                PostMessage(g_xferwnd,WM_USER_TITLEUPDATE,0,0);
              }
              else
              {
                T_Message msg={0,};
                C_FileSendReply reply;
                reply.set_error(1);
                msg.data=reply.Make();
                if (msg.data)
                {
                  msg.message_type=MESSAGE_FILE_REQUEST_REPLY;
                  msg.message_length=msg.data->GetLength();
                  msg.message_guid=message->message_guid;
                  g_mql->send(&msg);
                }
              }
            }
          }
        }
        delete r;
      }
    break;
    case MESSAGE_FILE_REQUEST_REPLY:
      {
        int n=g_recvs.GetSize();
        int x;
        for (x = 0; x < n; x ++)
        {
          if (!memcmp(g_recvs.Get(x)->get_guid(),&message->message_guid,16))
          {
            g_recvs.Get(x)->onGotMsg(new C_FileSendReply(message->data));
            break;
          }
        }
      }
    break;
    case MESSAGE_CHAT_REPLY:
      chat_HandleMsg(message);
    break;
		case MESSAGE_CHAT:
			if (chat_HandleMsg(message) && g_regnick[0])
      { // send reply
	      C_MessageChatReply rep;	  
        rep.setnick(g_regnick);

			  T_Message msg={0,};
			  msg.message_guid=message->message_guid;
			  msg.data=rep.Make();
			  if (msg.data)
			  {
	        msg.message_type=MESSAGE_CHAT_REPLY;
				  msg.message_length=msg.data->GetLength();
				  _this->send(&msg);
			  }
      }
		break;
    case MESSAGE_UPLOAD:
      {
        int upflag=g_config->ReadInt("accept_uploads",1);
        if (upflag&1) {
          C_UploadRequest *r = new C_UploadRequest(message->data);
          if (!stricmp(r->get_dest(),g_client_id_str) || (g_regnick[0] && !stricmp(r->get_dest(),g_regnick)))
          {
            char *fn=r->get_fn();
            int weirdfn=!strncmp(fn,"..",2) || strstr(fn,":") || strstr(fn,"..\\") || strstr(fn,"../") || fn[0]=='\\' || fn[0] == '/';
            if (!(upflag & 4) || weirdfn)
            {
              char *p=fn;
              while (*p) p++;
              while (p >= fn && *p != '/' && *p != '\\') p--;
              p++;
              if (p != fn || !weirdfn) fn=p;
              else fn="";
            }

            if (fn[0])
            {              
              if (!(upflag & 2)) 
              {
                char str[64];
                MakeID128Str(r->get_guid(),str);
                sprintf(str+strlen(str),":%d",r->get_idx());
                main_handleUpload(str,fn,r);
              }
              else 
              {
                char *t=strdup(fn);
                r->set_fn(t);
                free(t);
                uploadPrompts.Add(r);
                break;
              }
            }
            else
            {
              debug_printf("got upload request that was invalid\n");
            }
          }
          delete r;
        }
      }
    break;
		default:
//	  debug_printf("unknown message received : %d\n",message->message_type);
		break;
  }
}

static UINT CALLBACK fileHookProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SetDlgItemText(hwndDlg,IDC_UPATH,g_filedlg_ulpath);
  }
  if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDC_UPATH && HIWORD(wParam) == EN_CHANGE)
    {
      GetDlgItemText(hwndDlg,IDC_UPATH,g_filedlg_ulpath,sizeof(g_filedlg_ulpath));
      return 1;
    }
  }

  return 0;
}

void UserListContextMenu(HWND htree)
{
  TVHITTESTINFO hit;

  hit.pt.x=GET_X_LPARAM(GetMessagePos());
  hit.pt.y=GET_Y_LPARAM(GetMessagePos());
  ScreenToClient(htree,&hit.pt);
  HTREEITEM h=TreeView_HitTest(htree,&hit);             
  if (!h) h=TreeView_GetSelection(htree);

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
    if (i.pszText[0])
    {
      HMENU hMenu=GetSubMenu(g_context_menus,2);

      POINT p;
      GetCursorPos(&p);

      int x=TrackPopupMenu(hMenu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_LEFTBUTTON|TPM_NONOTIFY,p.x,p.y,0,GetParent(htree),NULL);

      if (x == ID_SENDFILENODE)
      {
        char *fnroot=(char*)malloc(65536*4);

        OPENFILENAME l={sizeof(l),};
        fnroot[0]=0;
        l.hwndOwner = GetParent(htree);
        l.lpstrFilter = "All files (*.*)\0*.*\0";
        l.lpstrFile = fnroot;
        l.nMaxFile = 65535*4;
        l.lpstrTitle = "Open file(s) to send";
        l.lpstrDefExt = "";
        l.hInstance=g_hInst;
        l.lpfnHook=fileHookProc;
        l.lpTemplateName=MAKEINTRESOURCE(IDD_FILESUBDLG);

        l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_ENABLETEMPLATE|OFN_ENABLEHOOK|OFN_FILEMUSTEXIST;
        if (GetOpenFileName(&l))
        {
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
            Xfer_UploadFileToUser(GetParent(htree),fullfn,i.pszText,g_filedlg_ulpath);

            fn+=strlen(fn)+1;
          }
        }
        free(fnroot);
      }
      else if (x == ID_BROWSEUSER)
      {
        char buf[1024];
        sprintf(buf,"/%s",i.pszText);
        SendMessage(g_mainwnd,WM_COMMAND,IDC_SEARCH,0);
        Search_Search(buf);
      }
      else if (x == ID_PRIVMSGNODE)
      {
        chat_ShowRoom(i.pszText,1);
      }
      else if (x == ID_WHOISUSER)
      {
        T_Message msg;
        // send a message to text that is /whois
        C_MessageChat req;
        req.set_chatstring("/whois");
				req.set_dest(i.pszText);
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
  }
}

void handleWasteURL(char *url)
{
 char tmp[2048];
 safe_strncpy(tmp,url,sizeof(tmp));
 char *in=tmp+6,*out=tmp;
 while (*in)
 {
   if (!strncmp(in,"%20",3)) { in+=3; *out++=' '; }
   else *out++=*in++;
 }
 *out=0;

 if (tmp[0] == '?')
 {
   if (!strnicmp(tmp,"?chat=",6))
   {
     chat_ShowRoom(tmp+6,2);
   }
   else if (!strnicmp(tmp,"?browse=",8))
   {
     SendMessage(g_mainwnd,WM_COMMAND,IDC_SEARCH,0);
     Search_Search(tmp+8);
   }
 }
 else
 {
   SendMessage(g_mainwnd,WM_COMMAND,IDC_SEARCH,0);
   Search_Search(tmp);
 }
}


static void SendDirectoryToUser(HWND hwndDlg, char *fullfn, char *text, int offs)
{
  char maskstr[2048];
  WIN32_FIND_DATA d;
  sprintf(maskstr,"%s\\*.*",fullfn);
  HANDLE h=FindFirstFile(maskstr,&d);
  if (h != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
        if (d.cFileName[0] != '.')
        {
          sprintf(maskstr,"%s\\%s",fullfn,d.cFileName);
          SendDirectoryToUser(hwndDlg,maskstr,text,offs);
        }
      }
      else
      {
        sprintf(maskstr,"%s\\%s",fullfn,d.cFileName);
        Xfer_UploadFileToUser(hwndDlg,maskstr,text,fullfn+offs);
      }
    }
    while (FindNextFile(h,&d));
    FindClose(h);
  }
}


void UserListOnDropFiles(HWND hwndDlg, HWND htree, HDROP hdrop, char *forcenick)
{
  TVHITTESTINFO i;
  DragQueryPoint(hdrop,&i.pt);
  ClientToScreen(hwndDlg,&i.pt);
  ScreenToClient(htree,&i.pt);
  HTREEITEM htt=0;
  if (!forcenick) htt=TreeView_HitTest(htree,&i);
  if (forcenick || htt)
  {
    char fullfn[1024];
	  int x,y = DragQueryFile(hdrop,0xffffffff,fullfn,sizeof(fullfn));
    char texto[64];
    TVITEM tvi;
    tvi.mask=TVIF_TEXT|TVIF_HANDLE;
    tvi.hItem=htt;
    texto[0]=0;
    tvi.pszText=texto;
    tvi.cchTextMax=sizeof(texto);
    if (!forcenick) TreeView_GetItem(htree,&tvi);
    if (forcenick || tvi.pszText[0]) 
    {
      char *text=forcenick?forcenick:tvi.pszText;
	    if (text[0]) for (x = 0; x < y; x ++) 
	    {
		    DragQueryFile(hdrop,x,fullfn,sizeof(fullfn));
        DWORD attr=GetFileAttributes(fullfn);
        if (attr != 0xFFFFFFFF)
        {
          if (attr & FILE_ATTRIBUTE_DIRECTORY)
          {
            int a=strlen(fullfn);
            while (a>=0 && fullfn[a] != '\\') a--;
            a++;
            SendDirectoryToUser(hwndDlg,fullfn,text,a);
          }
          else Xfer_UploadFileToUser(hwndDlg,fullfn,text,"");
        }
  	  } 
    }
    DragFinish(hdrop);
  }
}

void update_set_port()
{
  delete g_listen;
  g_listen=NULL;

  g_port=(g_route_traffic && g_config->ReadInt("listen",1)) ? 
            g_config->ReadInt("port",1337) : 0;
  if (g_port) 
  {
    debug_printf("[main] creating listen object on %d\n",g_port);
    g_listen = new C_Listen((short)g_port);
  }
}



void doDatabaseRescan()
{
  debug_printf("DB: reinitializing database\n");
  if (g_database && g_database != g_newdatabase && g_newdatabase) 
  {
    debug_printf("DB: deleting temp db\n");
    delete g_newdatabase;
  }             
  g_newdatabase=new C_FileDB();
  if (g_config->ReadInt("use_extlist",0))
    g_newdatabase->UpdateExtList(g_config->ReadString("extlist",g_def_extlist));
  else 
    g_newdatabase->UpdateExtList("*");

  g_newdatabase->Scan(g_config->ReadString("databasepath",""));
  if (!g_database || !g_database->GetNumFiles())
  {
    if (g_database) 
    {
      debug_printf("DB: making scanning db live\n");
      delete g_database;
    }
    g_database=g_newdatabase;
  }
}


void main_onGotChannel(char *cnl)
{
  HWND htree=GetDlgItem(g_mainwnd,IDC_CHATROOMS);   
  int added=0;
  HTREEITEM h = TreeView_GetChild(htree,TVI_ROOT);
  while (h)
  {
    char text[512];
    TVITEM i;
    i.mask=TVIF_HANDLE|TVIF_PARAM|TVIF_TEXT;
    i.cchTextMax=sizeof(text);
    i.pszText=text;
    i.hItem=h;
    TreeView_GetItem(htree,&i);
    if (cnl)
    {
      if (!stricmp(text,cnl))
      {
        i.pszText=cnl;
        i.lParam=time(NULL);
        TreeView_SetItem(htree,&i);
        added=1;
      }
    }
  
    if (time(NULL) - (unsigned int)i.lParam >  4*60)
    {
      h=TreeView_GetNextSibling(htree,h);
      chatroom_item *p=L_Chatroom;
	    while(p!=NULL)
  	  {
        if (!stricmp(p->channel,text)) break;
        p=p->next;
      }
      if (!p) TreeView_DeleteItem(htree,i.hItem);
    }
    else h=TreeView_GetNextSibling(htree,h);
  }
  if (!added && cnl)
  {
    TVINSERTSTRUCT i;
    i.hParent=TVI_ROOT;
    i.hInsertAfter=TVI_SORT;
    i.item.mask=TVIF_PARAM|TVIF_TEXT;
    i.item.pszText=cnl;
    i.item.lParam=time(NULL);
    TreeView_InsertItem(htree,&i);
  }
}



void main_onGotNick(char *nick, int del)
{
  if (nick)
  {
    if (!stricmp(nick,g_regnick)) return;
    KillTimer(g_mainwnd,5);
    SetTimer(g_mainwnd,5,5000,0);
  }
  HWND htree=GetDlgItem(g_mainwnd,IDC_USERS);
  HTREEITEM h=TreeView_GetChild(htree,TVI_ROOT);
  while (h)
  {
    char text[512];
    TVITEM i;
    i.mask=TVIF_HANDLE|TVIF_PARAM|TVIF_TEXT;
    text[0]=0;
    i.cchTextMax=sizeof(text);
    i.pszText=text;
    i.hItem=h;
    TreeView_GetItem(htree,&i);
    if (nick)
    {
      if (!stricmp(nick,i.pszText))
      {
        if (del)
        {
        	TreeView_DeleteItem(htree,h);
          return;
        }
        i.mask=TVIF_HANDLE|TVIF_PARAM|TVIF_TEXT;
        i.pszText=nick;
        i.lParam=time(NULL);
        TreeView_SetItem(htree,&i);
        break;
      }
      h=TreeView_GetNextSibling(htree,h);
    }
    else
    {
      h=TreeView_GetNextSibling(htree,h);
      if (time(NULL)-i.lParam > 4*60)
      {
      	TreeView_DeleteItem(htree,i.hItem);
      }
    }
  }
  if (!h && nick)
  {
    TVINSERTSTRUCT i;
    i.hParent=TVI_ROOT;
    i.hInsertAfter=TVI_SORT;
    i.item.mask=TVIF_PARAM|TVIF_TEXT;
    i.item.pszText=nick;
    i.item.lParam=time(NULL);
    TreeView_InsertItem(htree,&i);
  }
}





static int mainwnd_old_yoffs;
static ChildWndResizeItem mainwnd_rlist[]={
  {IDC_USERSLABEL,0x0010},
  {IDC_USERS,0x0011},
  {IDC_DIVIDER,0x0111},
  {IDC_CHATROOMSLABEL,0x0111},
  {IDC_CHATROOMS,0x0111},
  {IDC_CREATECHATROOM,0x1010},
  {IDC_SEARCH,0x1010},
  {IDC_NETSTATUS,0x0010},
};

//unfortunately this code is super super dumb. I actually did a much better
//version of it for gen_ml later on, but alas, too late now.
static void MainDiv_UpdPos(int yp)
{
  RECT r,r2;
  GetClientRect(g_mainwnd,&r);

  GetWindowRect(GetDlgItem(g_mainwnd,mainwnd_rlist[2].id),&r2);
  int tmp=mainwnd_old_yoffs-(r.bottom-(yp+r2.top));

  { // see if bottom of IDC_USERS is < top of IDC_USERS+16
      GetWindowRect(GetDlgItem(g_mainwnd,mainwnd_rlist[1].id),&r2);
      int newy=yp+r2.top;
      ScreenToClient(g_mainwnd,(LPPOINT)&r2);
      ScreenToClient(g_mainwnd,((LPPOINT)&r2)+1);              
      if (((r2.bottom-r2.top)+newy) < mainwnd_rlist[1].rinfo.top+16)
        tmp=80;
  }
  if (tmp < 80)
  {
    int x;
    for (x = 1; x < 5; x ++)
    {
      GetWindowRect(GetDlgItem(g_mainwnd,mainwnd_rlist[x].id),&r2);
      int newy=yp+r2.top;
      ScreenToClient(g_mainwnd,(LPPOINT)&r2);
      ScreenToClient(g_mainwnd,((LPPOINT)&r2)+1);
      if (x > 1) mainwnd_rlist[x].rinfo.top=r.bottom-newy;
      if (x < 4) mainwnd_rlist[x].rinfo.bottom=r.bottom-((r2.bottom-r2.top)+newy);
    }
    g_config->WriteInt("main_divpos",mainwnd_old_yoffs-mainwnd_rlist[2].rinfo.top);
  }

  childresize_resize(g_mainwnd,mainwnd_rlist,sizeof(mainwnd_rlist)/sizeof(mainwnd_rlist[0]));
}

static UINT WM_TASKBARCREATED;


static WNDPROC div_oldWndProc;
static BOOL CALLBACK div_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_LBUTTONDOWN)
  {
    SetForegroundWindow(hwndDlg);
    SetCapture(hwndDlg);
    SetCursor(LoadCursor(NULL,IDC_SIZENS));
  }
  else if (uMsg == WM_SETCURSOR)
  {
    SetCursor(LoadCursor(NULL,IDC_SIZENS));
    return TRUE;
  }
  else if (uMsg == WM_MOUSEMOVE && GetCapture()==hwndDlg)
  {    
    POINT p;
    RECT r3;
    GetCursorPos(&p);
    ScreenToClient(GetParent(hwndDlg),(LPPOINT)&p);
    GetWindowRect(hwndDlg,&r3);
    MainDiv_UpdPos(p.y-r3.top);
  }
  else if (uMsg == WM_MOUSEMOVE)
  {
    SetCursor(LoadCursor(NULL,IDC_SIZENS));
  }
  else if (uMsg == WM_LBUTTONUP)
  {
    ReleaseCapture();
  }
  return CallWindowProc(div_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}

#define CHATMENU_HIDEALL 4000
#define CHATMENU_SHOWALL 4001
#define CHATMENU_BASE 4002
#define CHATMENU_MAX 5000

static void MakeChatSubMenu(HMENU hMenu)
{
  MENUITEMINFO i={sizeof(i),};
  while (RemoveMenu(hMenu,0,MF_BYPOSITION));

  i.fMask=MIIM_TYPE|MIIM_DATA|MIIM_ID|MIIM_STATE;
  i.fType=MFT_STRING;
  i.fState=MFS_ENABLED;
  chatroom_item *p=L_Chatroom;
  if (!p)
  {
    i.dwTypeData="No active chats";
    i.fState=MFS_DISABLED;
    InsertMenuItem(hMenu,0,TRUE,&i);
  }
  else
  {
    i.dwTypeData="Hide all chat windows";
    i.wID=CHATMENU_HIDEALL;
    InsertMenuItem(hMenu,0,TRUE,&i);

    i.dwTypeData="Show all chat windows";
    i.wID=CHATMENU_SHOWALL;
    InsertMenuItem(hMenu,1,TRUE,&i);

    i.wID=0;
    i.fType=MFT_SEPARATOR;
    InsertMenuItem(hMenu,2,TRUE,&i);
    i.fType=MFT_STRING;

    i.wID = CHATMENU_BASE;

    while (p && i.wID < CHATMENU_MAX)
    {
      i.fState = IsWindowVisible(p->hwnd)?MFS_CHECKED:MFS_ENABLED;
      i.dwTypeData=p->channel;
      InsertMenuItem(hMenu,3+i.wID - CHATMENU_BASE,TRUE,&i);
      i.wID++;
      p=p->next;
    }
  }
}

static BOOL WINAPI Main_DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

  static int m_isinst;
  static dlgSizeInfo sizeinf={"m",100,100,112,335};
  if (uMsg == WM_TASKBARCREATED)
  {
    if (systray_state)
    {
      systray_del(g_mainwnd);
      systray_add(g_mainwnd,g_hSmallIcon);
    }
    return 0;
  }
  switch (uMsg) 
  {
    case WM_INITMENU:
      MakeChatSubMenu(GetSubMenu(GetSubMenu((HMENU)wParam,1),5));
    return 0;
    case WM_DROPFILES:
      UserListOnDropFiles(hwndDlg,GetDlgItem(hwndDlg,IDC_USERS),(HDROP)wParam,NULL);
    return 0;
    case WM_GETMINMAXINFO:
      {
        LPMINMAXINFO m=(LPMINMAXINFO)lParam;
        if (m)
        {
          m->ptMinTrackSize.x=112;
          m->ptMinTrackSize.y=260 - g_config->ReadInt("main_divpos",55);
        }
        return 0;
      }
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE:
      {
        POINT p;
        RECT r3;
        GetWindowRect(GetDlgItem(hwndDlg,IDC_DIVIDER),&r3);
        GetCursorPos(&p);
        int d=p.y-r3.bottom;
        int d2=p.y-r3.top;
        if (d<0)d=-d;
        if (d2<0)d2=-d2;
        if (d2<d) d=d2;
        if (d < 6)
          SendDlgItemMessage(hwndDlg,IDC_DIVIDER,uMsg,0,0);
        
      }
    return 0;
    case WM_USER_TITLEUPDATE:
      SetWndTitle(hwndDlg,APP_NAME);
      if (g_search_wnd) SendMessage(g_search_wnd,uMsg,wParam,lParam);
      SendMessage(g_netstatus_wnd,uMsg,wParam,lParam);
      SendMessage(g_xferwnd,uMsg,wParam,lParam);      
      chat_updateTitles();
      if (systray_state)
      {
        systray_del(g_mainwnd);
        systray_add(g_mainwnd,g_hSmallIcon);
      }
      TreeView_SetTextColor(GetDlgItem(g_mainwnd,IDC_USERS),g_config->ReadInt("mul_color",0));
      TreeView_SetTextColor(GetDlgItem(g_mainwnd,IDC_CHATROOMS),g_config->ReadInt("mul_color",0));
      TreeView_SetBkColor(GetDlgItem(g_mainwnd,IDC_USERS),g_config->ReadInt("mul_bgc",0xFFFFFF));
      TreeView_SetBkColor(GetDlgItem(g_mainwnd,IDC_CHATROOMS),g_config->ReadInt("mul_bgc",0xFFFFFF));
    return 0;
    case WM_INITDIALOG:
      {
        g_mainwnd=hwndDlg;
        div_oldWndProc=(WNDPROC) SetWindowLong(GetDlgItem(hwndDlg,IDC_DIVIDER),GWL_WNDPROC,(LONG)div_newWndProc);
        safe_strncpy(g_regnick,g_config->ReadString("nick",""),sizeof(g_regnick));
        if (g_regnick[0] == '#' || g_regnick[0] == '&')
          g_regnick[0]=0;
        childresize_init(hwndDlg,mainwnd_rlist,sizeof(mainwnd_rlist)/sizeof(mainwnd_rlist[0]));

        mainwnd_old_yoffs=mainwnd_rlist[2].rinfo.top;
        POINT p={0,0};
        ScreenToClient(hwndDlg,&p);
        MainDiv_UpdPos(g_config->ReadInt("main_divpos",55)+p.y);

        SetWndTitle(hwndDlg,APP_NAME);

        if (g_config->ReadInt("systray",1)) systray_add(hwndDlg, g_hSmallIcon);

        handleDialogSizeMsgs(hwndDlg,uMsg,wParam,lParam,&sizeinf);
        ShowWindow(hwndDlg,SW_SHOW);

        g_netstatus_wnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_NET),NULL,Net_DlgProc);
        g_xferwnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_XFERS),NULL,Xfers_DlgProc);

        LoadNetQ();

        toolWindowSet(g_config->ReadInt("toolwindow",0));

        if (g_config->ReadInt("net_vis",1))
          SendMessage(hwndDlg,WM_COMMAND,IDC_NETSETTINGS,0);
        if (g_config->ReadInt("xfers_vis",0))
          SendMessage(hwndDlg,WM_COMMAND,ID_VIEW_TRANSFERS,0);
        if (g_config->ReadInt("search_vis",0))
          SendMessage(hwndDlg,WM_COMMAND,IDC_SEARCH,0);

        SetForegroundWindow(hwndDlg);

        if (g_config->ReadInt("aot",0))
        {
          SetWindowPos(hwndDlg,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
          CheckMenuItem(GetMenu(hwndDlg),ID_VIEW_ALWAYSONTOP,MF_CHECKED|MF_BYCOMMAND);
        }
        TreeView_SetTextColor(GetDlgItem(g_mainwnd,IDC_USERS),g_config->ReadInt("mul_color",0));
        TreeView_SetTextColor(GetDlgItem(g_mainwnd,IDC_CHATROOMS),g_config->ReadInt("mul_color",0));
        TreeView_SetBkColor(GetDlgItem(g_mainwnd,IDC_USERS),g_config->ReadInt("mul_bgc",0xFFFFFF));
        TreeView_SetBkColor(GetDlgItem(g_mainwnd,IDC_CHATROOMS),g_config->ReadInt("mul_bgc",0xFFFFFF));

        CreateTooltip(GetDlgItem(hwndDlg,IDC_CREATECHATROOM),"Create/join chat");
        SendDlgItemMessage(hwndDlg,IDC_CREATECHATROOM,BM_SETIMAGE,IMAGE_ICON,
          (LPARAM)LoadImage(g_hInst,MAKEINTRESOURCE(IDI_CHAT),IMAGE_ICON,16,16,0));      

        CreateTooltip(GetDlgItem(hwndDlg,IDC_NETSETTINGS),"Open network status");
        SendDlgItemMessage(hwndDlg,IDC_NETSETTINGS,BM_SETIMAGE,IMAGE_ICON,
          (LPARAM)LoadImage(g_hInst,MAKEINTRESOURCE(IDI_CONNECT),IMAGE_ICON,16,16,0));      

        CreateTooltip(GetDlgItem(hwndDlg,IDC_SEARCH),"Open browser");
      
        SendDlgItemMessage(hwndDlg,IDC_SEARCH,BM_SETIMAGE,IMAGE_ICON,
          (LPARAM)LoadImage(g_hInst,MAKEINTRESOURCE(IDI_SEARCH),IMAGE_ICON,16,16,0));      

        SetTimer(hwndDlg,1,5,NULL);
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_FILE_QUIT: DestroyWindow(hwndDlg); break;
        case ID_VIEW_MAINWND:
          {
            WINDOWPLACEMENT wp={sizeof(wp),};
            GetWindowPlacement(hwndDlg,&wp);
            if (wp.showCmd == SW_SHOWMINIMIZED) 
              ShowWindow(hwndDlg,SW_RESTORE);
            else 
              ShowWindow(hwndDlg,SW_SHOW);
            SetForegroundWindow(hwndDlg);
          }
        break;
        case IDC_NETSETTINGS:
        case ID_VIEW_NETWORKCONNECTIONS:
        case ID_VIEW_NETWORKCONNECTIONS2:
          CheckMenuItem(GetMenu(hwndDlg),ID_VIEW_NETWORKCONNECTIONS,MF_CHECKED|MF_BYCOMMAND);
          {
            WINDOWPLACEMENT wp={sizeof(wp),};
            GetWindowPlacement(g_netstatus_wnd,&wp);
            if (wp.showCmd == SW_SHOWMINIMIZED) 
              ShowWindow(g_netstatus_wnd,SW_RESTORE);
            else 
              ShowWindow(g_netstatus_wnd,SW_SHOW);
          }
          SetForegroundWindow(g_netstatus_wnd);
          g_config->WriteInt("net_vis",1);
        break;
        case ID_VIEW_TRANSFERS:
        case ID_VIEW_TRANSFERS2:
          CheckMenuItem(GetMenu(hwndDlg),ID_VIEW_TRANSFERS,MF_CHECKED|MF_BYCOMMAND);
          {
            WINDOWPLACEMENT wp={sizeof(wp),};
            GetWindowPlacement(g_xferwnd,&wp);
            if (wp.showCmd == SW_SHOWMINIMIZED) 
              ShowWindow(g_xferwnd,SW_RESTORE);
            else 
              ShowWindow(g_xferwnd,SW_SHOW);
          }
          SetForegroundWindow(g_xferwnd);
          g_config->WriteInt("xfers_vis",1);
        break;
        case ID_VIEW_ALWAYSONTOP2:
        case ID_VIEW_ALWAYSONTOP:
          {
            int newaot=!g_config->ReadInt("aot",0);
            g_config->WriteInt("aot",newaot);
            if (newaot)
            {
              SetWindowPos(hwndDlg,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
              CheckMenuItem(GetMenu(hwndDlg),ID_VIEW_ALWAYSONTOP,MF_CHECKED|MF_BYCOMMAND);
            }
            else
            {
              SetWindowPos(hwndDlg,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
              CheckMenuItem(GetMenu(hwndDlg),ID_VIEW_ALWAYSONTOP,MF_UNCHECKED|MF_BYCOMMAND);
            }
          }

        break;
        case ID_VIEW_PREFERENCES:
        case ID_VIEW_PREFERENCES2:
          if (prefs_hwnd)
          {
            ShowWindow(prefs_hwnd,SW_SHOW);
            SetForegroundWindow(prefs_hwnd);
          }
          else
          {
            prefs_hwnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_PREFS),g_mainwnd,PrefsOuterProc);
            CheckMenuItem(GetMenu(hwndDlg),ID_VIEW_PREFERENCES,MF_CHECKED|MF_BYCOMMAND);
            ShowWindow(prefs_hwnd,SW_SHOW);
          }
        break;
        case IDC_SEARCH:
        case IDC_SEARCH2:
          if (!g_search_wnd) g_search_wnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_SEARCH),NULL,Search_DlgProc);
          {
            WINDOWPLACEMENT wp={sizeof(wp),};
            GetWindowPlacement(g_search_wnd,&wp);
            if (wp.showCmd == SW_SHOWMINIMIZED) 
              ShowWindow(g_search_wnd,SW_RESTORE);
            else 
              ShowWindow(g_search_wnd,SW_SHOW);
          }
          SetForegroundWindow(g_search_wnd);
          CheckMenuItem(GetMenu(hwndDlg),IDC_SEARCH,MF_CHECKED|MF_BYCOMMAND);
          g_config->WriteInt("search_vis",1);
        break;
        case IDC_CREATECHATROOM:
        case IDC_CREATECHATROOM2:
          {
            char text[64];
            text[0]=0;
            HTREEITEM h=TreeView_GetSelection(GetDlgItem(hwndDlg,IDC_CHATROOMS));
            if (h)
            {
              TVITEM i;
              i.mask=TVIF_TEXT|TVIF_HANDLE;
              i.hItem=h;
              i.pszText=text;
              i.cchTextMax=sizeof(text);
              TreeView_GetItem(GetDlgItem(hwndDlg,IDC_CHATROOMS),&i);
              if (i.pszText != text)
                safe_strncpy(text,i.pszText,sizeof(text));
            }
            DialogBoxParam(g_hInst,MAKEINTRESOURCE(IDD_CHATROOMCREATE),hwndDlg,CreateChat_DlgProc,(LPARAM)text);
          }
        break;
        case CHATMENU_SHOWALL:
        case CHATMENU_HIDEALL:
          {
            chatroom_item *p=L_Chatroom;
            while (p)
            {
              if (LOWORD(wParam) == CHATMENU_HIDEALL)
              {
                ShowWindow(p->hwnd,SW_HIDE);
              }
              else
              {
                WINDOWPLACEMENT wp={sizeof(wp),};
                GetWindowPlacement(p->hwnd,&wp);
                if (wp.showCmd == SW_SHOWMINIMIZED) ShowWindow(p->hwnd,SW_RESTORE);
                if (!IsWindowVisible(p->hwnd)) ShowWindow(p->hwnd,SW_SHOW);
                SetForegroundWindow(p->hwnd);
              }
              p=p->next;
            }
          }
        break;
        default:
          if (LOWORD(wParam) >= CHATMENU_BASE && LOWORD(wParam) < CHATMENU_MAX)
          {
            int n=LOWORD(wParam)-CHATMENU_BASE;
            chatroom_item *p=L_Chatroom;
            while (n-- && p)
            {
              p=p->next;
            }
            if (p)
            {
              WINDOWPLACEMENT wp={sizeof(wp),};
              GetWindowPlacement(p->hwnd,&wp);
              if (wp.showCmd == SW_SHOWMINIMIZED) 
                ShowWindow(p->hwnd,SW_RESTORE);
              else if (!IsWindowVisible(p->hwnd)) ShowWindow(p->hwnd,SW_SHOW);
              SetForegroundWindow(p->hwnd);
            }
          }
        break;
      }
    return 0;
    case WM_ENDSESSION:
    case WM_DESTROY:
      SaveNetQ();
      DestroyWindow(g_netstatus_wnd);
      DestroyWindow(g_xferwnd);
      if (g_search_wnd) DestroyWindow(g_search_wnd);
      if (g_config->ReadInt("db_save",1) && g_database && g_database != g_newdatabase) 
      {
        char dbfile[1024+8];
        sprintf(dbfile,"%s.pr2",g_config_prefix);
        g_database->writeOut(dbfile);
      }
  		if (systray_state) systray_del(hwndDlg);
      PostQuitMessage(0);
    return 0;
    case WM_NOTIFY:
      {
        LPNMHDR p=(LPNMHDR) lParam;
        if (p->code == NM_RCLICK)
        {
          if (p->idFrom == IDC_USERS)
          {
            UserListContextMenu(p->hwndFrom);
          }
        }
        if (p->code == NM_DBLCLK)
        {
          if (p->idFrom == IDC_CHATROOMS || p->idFrom == IDC_USERS)
          {
            HWND htree=p->hwndFrom;
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
      }
    return 0;
    case WM_CLOSE:
      if (!g_config->ReadInt("confirmquit",1) || MessageBox(hwndDlg,"Closing this window will quit " APP_NAME " and disconnect you from the private network.\n"
                             "Are you sure you want to do this?",
        "Close " APP_NAME,MB_YESNO) == IDYES) DestroyWindow(hwndDlg);
    return 0;
    case WM_TIMER:
      if (wParam == 5) // update user list
      {
        // check treeviews for any nicks/channels that we haven't seen in 4 minutes.
        main_onGotNick(NULL,0);
        main_onGotChannel(NULL);
      }

      if (wParam==1) 
      {
        static int upcnt;
        if (uploadPrompts.GetSize() && !upcnt)
        {
          upcnt=1;
          C_UploadRequest *t=uploadPrompts.Get(0);
          uploadPrompts.Del(0);
          char buf[1024];
          char str[64];
          MakeID128Str(t->get_guid(),str);
          sprintf(str+strlen(str),":%d",t->get_idx());
          if (strlen(t->get_fn()) > 768)
            t->get_fn()[768]=0;
          int f=MB_YESNO;
          char sizebuf[64];
          sizebuf[0]=0;
          int fs_l,fs_h;
          t->get_fsize(&fs_l,&fs_h);
          if (fs_l != -1 || fs_h != -1)
          {
            strcpy(sizebuf," (");
            FormatSizeStr64(sizebuf+2,fs_l,fs_h);
            strcat(sizebuf,")");
          }
          if (uploadPrompts.GetSize() > 1)
          {
            f=MB_YESNOCANCEL;
            sprintf(buf,"Accept upload of file named '%s'%s from user '%s'?\r\n(Cancel will remove all %d pending uploads)",t->get_fn(),sizebuf,t->get_nick(),uploadPrompts.GetSize()-1);
          }
          else sprintf(buf,"Accept upload of file named '%s'%s from user '%s'?",t->get_fn(),sizebuf,t->get_nick());

          f=MessageBox(NULL,buf,APP_NAME " - Accept Upload?",f|MB_TOPMOST|MB_ICONQUESTION);
          if (f==IDYES) main_handleUpload(str,t->get_fn(),t);
          else if (f == IDCANCEL)
          {
            while (uploadPrompts.GetSize()>0)
            {
              delete uploadPrompts.Get(0);
              uploadPrompts.Del(0);
            }
          }

          delete t;
          upcnt=0;
        }
        static int kdcnt;
        if (keydistPrompts.GetSize() && !kdcnt)
        {
          kdcnt=1;
          C_KeydistRequest *t=keydistPrompts.Get(0);
          keydistPrompts.Del(0);

          if (!findPublicKeyFromKey(t->get_key()))
          {
            char buf[1024];
            char sign[SHA_OUTSIZE*2+1];
            unsigned char hash[SHA_OUTSIZE];
            SHAify m;
            m.add((unsigned char *)t->get_key()->modulus,MAX_RSA_MODULUS_LEN);
            m.add((unsigned char *)t->get_key()->exponent,MAX_RSA_MODULUS_LEN);
            m.final(hash);
            int x;
            for (x = 0; x < SHA_OUTSIZE; x ++) sprintf(sign+x*2,"%02X",hash[x]);

            sprintf(buf,"Authorize public key with signature '%s' from user '%s'?\r\n(Cancel will keep this key and any remaining prompts in the pending key list)",sign,t->get_nick());

            int f=MessageBox(NULL,buf,APP_NAME " - Accept Public Key?",MB_YESNOCANCEL|MB_TOPMOST|MB_ICONQUESTION);

            if (f==IDYES) main_handleKeyDist(t,0);
            else if (f == IDCANCEL)
            {
              main_handleKeyDist(t,1);
              while (keydistPrompts.GetSize()>0)
              {
                main_handleKeyDist(keydistPrompts.Get(0),1);
                delete keydistPrompts.Get(0);
                keydistPrompts.Del(0);
              }
            }
          }
          delete t;
          kdcnt=0;
        }

        static int in_timer;
        if (!in_timer)
        {
          in_timer=1;

          int scanworking=0;
          if (g_scanloadhack)
          {
            g_scanloadhack=0;
            if (g_database)
            {
              sprintf(g_scan_status_buf,"%d files (cached)",g_database->GetNumFiles());
            }
          }
          if (g_newdatabase) 
          {
            int r=g_newdatabase->DoScan(15, 
                      (g_newdatabase != g_database && g_database->GetNumFiles()) ? g_database : NULL
                  );
            if (r != -1)
            {
              scanworking=1;
              sprintf(g_scan_status_buf,"Scanning %d",r);
            }
            else
            {
              sprintf(g_scan_status_buf,"Scanned %d",g_newdatabase->GetNumFiles());
              if (g_newdatabase != g_database) 
              {
                debug_printf("DB: replacing old database with temp database\r\n");
                delete g_database;
                g_database=g_newdatabase;
              }
              else
                debug_printf("DB: removing temp database reference, scanning done\r\n");
              g_newdatabase=0;
            }
          }

          int lastqueues=g_mql->GetNumQueues();

          int x;
          NetKern_Run();
          int n=1;
          if (g_conspeed>=64) n++;
          if (g_conspeed>=384) n++;
          if (g_conspeed>=1600) n+=2; // fast connections
          if (g_conspeed>=20000) n+=3; // really fast connections

          for (x = 0; x < n; x ++)
          {
            Xfer_Run();

            g_mql->run(g_route_traffic);
          }
          int newl=g_mql->GetNumQueues();
          if (lastqueues != newl) 
          {
            MYSRANDUPDATE((unsigned char *)&lastqueues,sizeof(lastqueues));

            char text[32];

            sprintf(text,":%d",newl);

            SetDlgItemText(g_mainwnd,IDC_NETSTATUS,text);
          }

          if (time(NULL) > g_next_refreshtime && g_do_autorefresh && !g_newdatabase)
          {
            if (!scanworking)
            {
              if (g_next_refreshtime)
              {
                g_next_refreshtime=0;
                debug_printf("DB: initiating autorescan of files\n");
                g_newdatabase=new C_FileDB();
                if (g_config->ReadInt("use_extlist",0))
                  g_newdatabase->UpdateExtList(g_config->ReadString("extlist",g_def_extlist));
                else 
                  g_newdatabase->UpdateExtList("*");
                g_newdatabase->Scan(g_config->ReadString("databasepath",""));
                if (!g_database->GetNumFiles())
                {
                  debug_printf("DB: making scanning db live\n");
                  delete g_database;
                  g_database=g_newdatabase;
                }
              }
              else g_next_refreshtime = time(NULL)+60*g_config->ReadInt("refreshint",300);
            }
          }

          if (time(NULL) > g_last_pingtime && newl) // send ping every 2m
          {
            g_last_pingtime = time(NULL)+120;
            DoPing(NULL);

            // flush config and netq
            g_config->Flush();
            SaveNetQ();
          }
          if (time(NULL) > g_last_bcastkeytime && newl)
          {
            g_last_bcastkeytime = time(NULL)+60*60; // hourly
            if (g_config->ReadInt("bcastkey",1))
              main_BroadcastPublicKey();
          }

          in_timer=0;
        }
      }
    return 0;
		case WM_USER_SYSTRAY:
			switch (LOWORD(lParam))
			{
        case WM_RBUTTONUP:
          {
            HMENU hMenu=GetSubMenu(g_context_menus,5);
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(hwndDlg);

            MakeChatSubMenu(GetSubMenu(hMenu,5));

            CheckMenuItem(hMenu,ID_VIEW_NETWORKCONNECTIONS,MF_BYCOMMAND | 
              (IsWindowVisible(g_netstatus_wnd)?MF_CHECKED:MF_UNCHECKED));
            CheckMenuItem(hMenu,IDC_SEARCH,MF_BYCOMMAND | 
              (IsWindowVisible(g_search_wnd)?MF_CHECKED:MF_UNCHECKED));
            CheckMenuItem(hMenu,ID_VIEW_TRANSFERS,MF_BYCOMMAND | 
              (IsWindowVisible(g_xferwnd)?MF_CHECKED:MF_UNCHECKED));
            CheckMenuItem(hMenu,ID_VIEW_PREFERENCES,MF_BYCOMMAND | 
              (prefs_hwnd?MF_CHECKED:MF_UNCHECKED));

            TrackPopupMenu(hMenu,TPM_RIGHTBUTTON|TPM_LEFTBUTTON,p.x,p.y,0,hwndDlg,NULL);
          }
        return 0;
				case WM_LBUTTONDOWN:
          SendMessage(hwndDlg,WM_COMMAND,ID_VIEW_MAINWND,0);
				return 0;
			}
		return 0;
    case WM_COPYDATA:
      if (((COPYDATASTRUCT *)lParam)->dwData == 0xB3EF)
      {
        handleWasteURL((char*)((COPYDATASTRUCT *)lParam)->lpData);
      }
      else if (((COPYDATASTRUCT *)lParam)->dwData == 0xF00D)
      {
        m_isinst=!stricmp((char*)((COPYDATASTRUCT *)lParam)->lpData,g_config_prefix);
      }
    return TRUE;
    case WM_USER_PROFILECHECK:
      SetWindowLong(hwndDlg,DWL_MSGRESULT,m_isinst);
    return TRUE;
    case WM_MOVE:
      handleDialogSizeMsgs(hwndDlg,uMsg,wParam,lParam,&sizeinf);
    break;
		case WM_SIZE:
			if (wParam==SIZE_MINIMIZED)
			{
        if (toolwnd_state || (GetAsyncKeyState(VK_SHIFT)&0x8000) || g_config->ReadInt("systray_hide",1))
        {
          if (!systray_state) systray_add(hwndDlg,g_hSmallIcon);
          ShowWindow(hwndDlg,SW_HIDE);
        }
        if (g_config->ReadInt("hideallonmin",0) && !g_hidewnd_state)
        {
          g_hidewnd_state=1;
          chatroom_item *p=L_Chatroom;
	        while(p!=NULL)
  	      {
            ShowWindow(p->hwnd,SW_HIDE);
            p=p->next;
          }
          if (g_search_wnd && IsWindowVisible(g_search_wnd))
          {
            ShowWindow(g_search_wnd,SW_HIDE);
            g_hidewnd_state|=2;
          }
          if (IsWindowVisible(g_netstatus_wnd))
          {
            ShowWindow(g_netstatus_wnd,SW_HIDE);
            g_hidewnd_state|=4;
          }
          if (IsWindowVisible(g_xferwnd))
          {
            ShowWindow(g_xferwnd,SW_HIDE);
            g_hidewnd_state|=8;
          }
        }
			}
      else
      {
        if (systray_state && !g_config->ReadInt("systray",1))
          systray_del(hwndDlg);
        childresize_resize(hwndDlg,mainwnd_rlist,sizeof(mainwnd_rlist)/sizeof(mainwnd_rlist[0]));
        if (g_hidewnd_state)
        {
          chatroom_item *p=L_Chatroom;
	        while(p!=NULL)
  	      {
            ShowWindow(p->hwnd,SW_SHOWNA);
            p=p->next;
          }
          if (g_hidewnd_state&2) // browser
          {
            if (g_search_wnd) ShowWindow(g_search_wnd,SW_SHOWNA);
          }
          if (g_hidewnd_state&4) // network
          {
            ShowWindow(g_netstatus_wnd,SW_SHOWNA);
          }
          if (g_hidewnd_state&8) // transfers
          {
            ShowWindow(g_xferwnd,SW_SHOWNA);
          }
          g_hidewnd_state=0;
        }
      }
      handleDialogSizeMsgs(hwndDlg,uMsg,wParam,lParam,&sizeinf);

		return 0;
  }
  return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nShowCmd)
{

  const char *mainwndclassname=APP_NAME "mainwnd";

  int x;
  g_hInst=hInstance;
  InitCommonControls();
  CoInitialize(0);
  { // load richedit DLL
    WNDCLASS wc={0,};
    if (!LoadLibrary("RichEd20.dll")) LoadLibrary("RichEd32.dll");

    // make richedit20a point to RICHEDIT
    if (!GetClassInfo(NULL,"RichEdit20A",&wc))
    {
      GetClassInfo(NULL,"RICHEDIT",&wc);
      wc.lpszClassName = "RichEdit20A";
      RegisterClass(&wc);
    }
  }

  g_hSmallIcon = (HICON)LoadImage(g_hInst,MAKEINTRESOURCE(IDI_ICON1),IMAGE_ICON,16,16,0);

  {
    WNDCLASS wc={0,};
    GetClassInfo(NULL,"#32770",&wc);
    wc.hIcon=LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_ICON1));
    RegisterClass(&wc);
    wc.lpszClassName = mainwndclassname;
    RegisterClass(&wc);
    wc.lpszClassName = APP_NAME "wnd";
    RegisterClass(&wc);

    wc.hIcon = LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_CHAT));
    wc.lpszClassName = APP_NAME "chatwnd";
    RegisterClass(&wc);

    wc.hIcon = LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_BROWSE));
    wc.lpszClassName = APP_NAME "searchwnd";
    RegisterClass(&wc);

    wc.hIcon = LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_XFERS));
    wc.lpszClassName = APP_NAME "xferwnd";
    RegisterClass(&wc);

    wc.hIcon = LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_CONNECT));
    wc.lpszClassName = APP_NAME "netwnd";
    RegisterClass(&wc);
  }

  char *wasteOpen=NULL;
  int force_profiledlg=0;
	while (1)
	{
		while (*lpszCmdParam == ' ') lpszCmdParam++;
		if (!strnicmp(lpszCmdParam,"/profile=",9))
    {
      if (lpszCmdParam[9] && lpszCmdParam[9] != ' ')
      {
        char *p;
        if (lpszCmdParam[9] == '"')
        {
  			  safe_strncpy(g_profile_name,lpszCmdParam+10,sizeof(g_profile_name));
	  		  if ((p=strstr(g_profile_name,"\""))) *p=0;
          lpszCmdParam+=10;
          while (*lpszCmdParam != '"' && *lpszCmdParam) lpszCmdParam++;
          if (*lpszCmdParam) lpszCmdParam++;
        }
        else
        {
  			  safe_strncpy(g_profile_name,lpszCmdParam+9,sizeof(g_profile_name));
	  		  if ((p=strstr(g_profile_name," "))) *p=0;
		  	  lpszCmdParam+=9+strlen(g_profile_name);
        }
        if (!g_profile_name[0]) force_profiledlg=1;
      }
      else 
      {
        force_profiledlg=1;
        break;
      }
    }
    else if (*lpszCmdParam)
    {
      char scchar=' ';
      if (*lpszCmdParam == '\"') scchar=*lpszCmdParam;
      if (!strnicmp(lpszCmdParam+(scchar != ' '),"waste:",6) && !wasteOpen) 
      {
        if (scchar != ' ') lpszCmdParam++;

        wasteOpen=strdup(lpszCmdParam);
        char *t=wasteOpen;
        while (*t != scchar && *t) t++;
        *t=0;

        lpszCmdParam+=6;
        while (*lpszCmdParam != scchar && *lpszCmdParam) lpszCmdParam++;
      }
      else break;
    }
    else break;
  }


  GetModuleFileName(g_hInst,g_config_prefix,sizeof(g_config_prefix));
  strcpy(g_config_mainini,g_config_prefix);

  char *p=g_config_mainini+strlen(g_config_mainini);
  while (p >= g_config_mainini && *p != '.' && *p != '\\') p--; *++p=0;
  strcat(g_config_mainini,"ini");

  p=g_config_prefix+strlen(g_config_prefix);
  while (p >= g_config_prefix && *p != '\\') p--; *++p=0;

  MYSRAND();

  if (Prefs_SelectProfile(force_profiledlg))
    return 1;

  strcat(g_config_prefix,g_profile_name);

  HWND wndpos=NULL;
  while ((wndpos=FindWindowEx(NULL,wndpos,mainwndclassname,NULL)))
  {
    COPYDATASTRUCT cds;
    cds.cbData=strlen(g_config_prefix)+1;
    cds.lpData=(PVOID)g_config_prefix;
    cds.dwData=0xF00D;
    SendMessage(wndpos,WM_COPYDATA,NULL,(LPARAM)&cds);
    if (SendMessage(wndpos,WM_USER_PROFILECHECK,0,0))
    {
      WINDOWPLACEMENT wp={sizeof(wp),};
      GetWindowPlacement(wndpos,&wp);
      if (wp.showCmd == SW_SHOWMINIMIZED)  ShowWindow(wndpos,SW_RESTORE);
      else ShowWindow(wndpos,SW_SHOW);
      SetForegroundWindow(wndpos);
      
      if (wasteOpen)
      {
        cds.cbData=strlen(wasteOpen)+1;
        cds.lpData=(PVOID)wasteOpen;
        cds.dwData=0xB3EF;
        SendMessage(wndpos,WM_COPYDATA,NULL,(LPARAM)&cds);
        free(wasteOpen);
      }

      return 1;
    }
  }
  
  char tmp[1024+8];

  sprintf(tmp,"WASTEINSTANCE_%s",g_profile_name);
  tmp[MAX_PATH-1]=0;
  CreateSemaphore(NULL,0,1,tmp);
  if (GetLastError() == ERROR_ALREADY_EXISTS)
    return 1;

  sprintf(tmp,"%s.pr0",g_config_prefix);
  g_config = new C_Config(tmp);

  int need_setup=g_config->ReadInt("valid",0)<5;

  g_do_log=g_config->ReadInt("debuglog",0);
  
  if (!g_config->ReadString("downloadpath","")[0])
  {
    GetModuleFileName(g_hInst,tmp,sizeof(tmp));
    char *p=tmp;
    while (*p) p++;
    while (p >= tmp && *p != '\\') p--;
    strcpy(++p,"downloads");
    CreateDirectory(tmp,NULL);
    g_config->WriteString("downloadpath",tmp);
  } 

  loadPKList();

  if (need_setup)
  {
    if (!DialogBox(hInstance,MAKEINTRESOURCE(IDD_SETUPWIZ),NULL,SetupWizProc))
    {
      delete g_config;
      memset(&g_key,0,sizeof(g_key));
      return 1;
    }
    g_config->WriteInt("valid",5);
    g_config->Flush();
  }

  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(1, 1), &wsaData))
  {
    memset(&g_key,0,sizeof(g_key));
    MessageBox(NULL,"Error initializing Winsock\n",APP_NAME " Error",0);
    return 1;
  }
  
  strncpy(g_client_id_str,g_config->ReadString("clientid128",""),32);
  g_client_id_str[32]=0;

  if (!g_key.bits) reloadKey(g_config->ReadInt("storepass",0)?
    g_config->ReadString("keypass",NULL):
      NULL,GetDesktopWindow());

  if (MakeID128FromStr(g_client_id_str,&g_client_id)) 
  {
    CreateID128(&g_client_id);
    MakeID128Str(&g_client_id,g_client_id_str);
    g_config->WriteString("clientid128",g_client_id_str);
  }

  if (g_config->ReadInt("db_save",1)) 
  {
    g_newdatabase=g_database=new C_FileDB();
    if (g_config->ReadInt("use_extlist",0))
      g_newdatabase->UpdateExtList(g_config->ReadString("extlist",g_def_extlist));
    else 
      g_newdatabase->UpdateExtList("*");
    char dbfile[1024+8];
    sprintf(dbfile,"%s.pr2",g_config_prefix);
    g_newdatabase->readIn(dbfile);
    g_scanloadhack=1;
  }

  if (g_config->ReadInt("scanonstartup",1)) doDatabaseRescan();

  {
    char *buf=g_config->ReadString("networkname","");
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
  }
  g_conspeed=g_config->ReadInt("conspeed",28);
  g_route_traffic=g_config->ReadInt("route",g_conspeed>64);
  g_forceip=g_config->ReadInt("forceip",0);
  g_forceip_addr=g_config->ReadInt("forceip_addr",INADDR_NONE);
  g_appendprofiletitles=g_config->ReadInt("appendpt",0);
  g_extrainf=g_config->ReadInt("extrainf",0);
  g_keepup=g_config->ReadInt("keepupnet",4);
  g_use_accesslist=g_config->ReadInt("ac_use",0);
  g_accept_downloads=g_config->ReadInt("downloadflags",7);
  g_do_autorefresh=g_config->ReadInt("dorefresh",0);
  g_max_simul_dl=g_config->ReadInt("recv_maxdl",4);
  g_max_simul_dl_host=g_config->ReadInt("recv_maxdl_host",1);
  g_chat_timestamp=g_config->ReadInt("chat_timestamp",0);
  g_search_showfull=g_config->ReadInt("search_showfull",1);
  g_search_showfullbytes=g_config->ReadInt("search_showfullb",0);
  g_keydist_flags=g_config->ReadInt("keydistflags",4|2|1);

  g_throttle_flag=g_config->ReadInt("throttleflag",0);
  g_throttle_send=g_config->ReadInt("throttlesend",64);
  g_throttle_recv=g_config->ReadInt("throttlerecv",64);

  g_dns=new C_AsyncDNS;
  g_mql = new C_MessageQueueList(main_MsgCallback,6);
  updateACList(NULL);
  update_set_port();

  // oops gotta do this here.
  for (x = 0; x < SEARCHCACHE_NUMITEMS; x ++) g_searchcache[x]=new SearchCacheItem;

  g_context_menus=LoadMenu(g_hInst,MAKEINTRESOURCE(IDR_CONTEXTMENUS));
  HACCEL hAccel=LoadAccelerators(g_hInst,MAKEINTRESOURCE(IDR_ACCEL1));

  WM_TASKBARCREATED = RegisterWindowMessage("TaskbarCreated");

  CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_MAIN),GetDesktopWindow(),Main_DlgProc);

  if (wasteOpen)
  {
    handleWasteURL(wasteOpen);
    free(wasteOpen);
  }

  MSG msg;
  while (GetMessage(&msg,NULL,0,0))
  {
    if (!TranslateAccelerator(g_mainwnd,hAccel,&msg) &&
        !IsDialogMessage(g_mainwnd,&msg) && 
        !IsDialogMessage(g_netstatus_wnd,&msg) && 
        (!g_search_wnd || !IsDialogMessage(g_search_wnd,&msg)) &&
        !IsDialogMessage(g_xferwnd,&msg) &&         
        (!g_xfer_subwnd_active || !IsDialogMessage(g_xfer_subwnd_active,&msg)) &&
        (!prefs_hwnd || !IsDialogMessage(prefs_hwnd,&msg)) &&
        (!prefs_cur_wnd || !IsDialogMessage(prefs_cur_wnd,&msg)) &&        
        !IsChatRoomDlgMessage(&msg))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  delete g_listen;
  delete g_dns;
  if (g_newdatabase != g_database) delete g_newdatabase;
  delete g_database;

  for (x = 0; x < g_recvs.GetSize(); x ++) delete g_recvs.Get(x);
  for (x = 0; x < g_sends.GetSize(); x ++) delete g_sends.Get(x);
  for (x = 0; x < g_new_net.GetSize(); x ++) delete g_new_net.Get(x);
  for (x = 0; x < g_uploads.GetSize(); x ++) free(g_uploads.Get(x));

  for (x = 0; x < SEARCHCACHE_NUMITEMS; x ++) delete g_searchcache[x];

  delete g_mql;

  delete g_config;
  memset(&g_key,0,sizeof(g_key));

  DestroyMenu(g_context_menus);
  DestroyIcon(g_hSmallIcon);
  WSACleanup();
  return 0;
}
