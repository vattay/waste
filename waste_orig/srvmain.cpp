/*
    WASTE - srvmain.cpp (non-GUI client main entry point and a lot of code :)
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




    Note that this server is far from feature complete, and isn't really production
    quality.

    That being said, you should create keys and profiles using the win32 client, and
    then load that profile using this server. It's clunky, I know.

*/

#include "main.h"
#include "rsa/md5.h"

#include "m_upload.h"
#include "m_chat.h"
#include "m_search.h"
#include "m_ping.h"
#include "m_file.h"
#include "m_lcaps.h"
#include "m_keydist.h"
#include "netkern.h"
#include "xfers.h"
#include "xferwnd.h"

#define VERSION "server 1.0a0"

unsigned char g_networkhash[SHA_OUTSIZE];
int g_use_networkhash;

char *g_nameverstr=APP_NAME " " VERSION;
char *g_def_extlist="ppt;doc;xls;txt;zip;";

C_FileDB *g_database, *g_newdatabase;
C_AsyncDNS *g_dns;
C_Listen *g_listen;
C_MessageQueueList *g_mql;
C_Config *g_config;

char g_config_prefix[1024];

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

int g_search_showfull;

int g_scanloadhack;
char g_scan_status_buf[128];
time_t g_next_refreshtime;

time_t g_last_pingtime,g_last_bcastkeytime;
T_GUID g_last_scanid;
int g_last_scanid_used;
T_GUID g_search_id;
unsigned int g_search_id_time;
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

static void main_handleUpload(char *guidstr, char *fnstr)
{
#if 0
  int willq=Xfer_WillQ(fnstr,guidstr);

  int p=g_lvrecvq.InsertItem(g_lvrecvq.GetCount(),fnstr,0);
  g_lvrecvq.SetItemText(p,1,"? (upload)");
  g_lvrecvq.SetItemText(p,2,guidstr);
  g_files_in_download_queue++;
  RecvQ_UpdateStatusText();
#endif
}



void main_MsgCallback(T_Message *message, C_MessageQueueList *_this, C_Connection *cn)
{
	switch(message->message_type)
	{
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
              if (cn) cn->set_saturatemode(v); 
// if ((get_saturatemode()&1) && (g_throttle_flag&32)) do outbound saturation
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
//	    else Search_AddReply(message);
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
/*              if (r->is_abort()==2)
              {
                int a=g_sends.Get(x)->GetIdx()-UPLOAD_BASE_IDX;
                if (a >= 0 && a < g_uploads.GetSize())
                {
                  free(g_uploads.Get(a));
                  g_uploads.Set(a,NULL);
                }
              }
              */
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
                  else g_database->GetFile(idx,fn,NULL,NULL,NULL,NULL);
                }
                else
                {
                  idx-=UPLOAD_BASE_IDX;
                  if (idx<g_uploads.GetSize()) safe_strncpy(fn,g_uploads.Get(idx),sizeof(fn));
                }
              }
              if (fn[0])
              {
                XferSend *a=new XferSend(_this,&message->message_guid,r,fn);
                char *err=a->GetError();
                if (err)
                {
                  delete a;
                }
                else
                {
                  g_sends.Add(a);
//                  g_lvsend.InsertItem(0,a->GetName(),(int)a);
 //                 char buf[32];
  //                sprintf(buf,"%u",a->GetSize());
   //               g_lvsend.SetItemText(0,1,r->get_nick());
    //              g_lvsend.SetItemText(0,2,buf);
     //             g_lvsend.SetItemText(0,3,"Sending");
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
//      chat_HandleMsg(message);
    break;
		case MESSAGE_CHAT:

      {
        C_MessageChat chat(message->data);
        char *cnl=chat.get_dest();
        if (g_regnick[0] && !stricmp(g_regnick,cnl))
        {
          if (!strcmp(chat.get_chatstring(),"/whois"))
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
              else sprintf(buf+strlen(buf),", %u.%u GB public",length>>10,((length/102))%10);
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
          else 
          {
            debug_printf("privmsg(%s): %s\n",chat.get_src(),chat.get_chatstring()); 
            {
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

            {
              C_MessageChat req;
              req.set_chatstring("[autoreply] I am but a dumb server");
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
          }
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
            int weirdfn=!strncmp(fn,"..",2) || strstr(fn,":") || strstr(fn,"\\..") || strstr(fn,"/..") || strstr(fn,"..\\") || strstr(fn,"../") || fn[0]=='\\' || fn[0] == '/';
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
                main_handleUpload(str,fn);
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
}



void main_onGotNick(char *nick, int del)
{
}


int g_exit=0;

void sighandler(int sig)
{
  if (sig == SIGHUP)
  {
    g_do_log=0;
  }
  if (sig == SIGPIPE) debug_printf("got SIGPIPE!\n");
  if (sig == SIGINT)
  {
    g_exit=1;
    debug_printf("got SIGINT, setting global exit flag!\n");
  }
}


int main(int argc, char **argv)
{
  signal(SIGHUP,sighandler);
  signal(SIGPIPE,sighandler);
  signal(SIGINT,sighandler);
 
  printf("%s starting up...\n",g_nameverstr);
  strcpy(g_config_prefix,argv[0]);
  char *p=g_config_prefix;
  while (*p) p++;
  while (p >= g_config_prefix && *p != '/') p--;
  *++p=0;

  MYSRAND();

  strcat(g_config_prefix,"default");
  
  char tmp[1024+8];
  sprintf(tmp,"%s.pr0",g_config_prefix);
   printf("loading config from %s\n",tmp);
  g_config = new C_Config(tmp);

  g_do_log=g_config->ReadInt("debuglog",0);
  
  if (!g_config->ReadString("downloadpath","")[0])
  {
    strcpy(tmp,argv[0]);
    char *p=tmp;
    while (*p) p++;
    while (p >= tmp && *p != '/') p--;
    strcpy(++p,"downloads");
    CreateDirectory(tmp,NULL);
    g_config->WriteString("downloadpath",tmp);
  }

  loadPKList();

  strncpy(g_client_id_str,g_config->ReadString("clientid128",""),32);
  g_client_id_str[32]=0;

  if (!g_key.bits) reloadKey(g_config->ReadInt("storepass",0)?
    g_config->ReadString("keypass",NULL):
      NULL);

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
  g_keydist_flags=g_config->ReadInt("keydistflags",4|2|1);

  g_throttle_flag=g_config->ReadInt("throttleflag",0);
  g_throttle_send=g_config->ReadInt("throttlesend",64);
  g_throttle_recv=g_config->ReadInt("throttlerecv",64);

  g_dns=new C_AsyncDNS;
  g_mql = new C_MessageQueueList(main_MsgCallback,6);
  updateACList();
  update_set_port();



  safe_strncpy(g_regnick,g_config->ReadString("nick",""),sizeof(g_regnick));
  if (g_regnick[0] == '#' || g_regnick[0] == '&') g_regnick[0]=0;


 
  // run loop

  while (!g_exit)
  {


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

          NetKern_Run();

          Xfer_Run();

          g_mql->run(g_route_traffic);
          int newl=g_mql->GetNumQueues();
          if (lastqueues != newl) 
          {
            MYSRANDUPDATE((unsigned char *)&lastqueues,sizeof(lastqueues));

   //         char text[32];

  //          if (newl) sprintf(text,"Connections: %d",newl);
 //           else strcpy(text,"Connect");

//            SetDlgItemText(g_mainwnd,IDC_NETSETTINGS,text);
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
//            SaveNetQ();
          }
          if (time(NULL) > g_last_bcastkeytime && newl)
          {
            g_last_bcastkeytime = time(NULL)+60*60; // hourly
            if (g_config->ReadInt("bcastkey",1))
              main_BroadcastPublicKey();
          }


     Sleep(33);
  }

  // exit
  printf("cleaning up\n");

  if (g_config->ReadInt("db_save",1) && g_database && g_database != g_newdatabase) 
  {
    printf("flushing db\n");
    char dbfile[1024+8];
    sprintf(dbfile,"%s.pr2",g_config_prefix);
    g_database->writeOut(dbfile);
  }


  delete g_listen;
  delete g_dns;
  if (g_newdatabase != g_database) delete g_newdatabase;
  delete g_database;

  int x;
  for (x = 0; x < g_recvs.GetSize(); x ++) delete g_recvs.Get(x);
  for (x = 0; x < g_sends.GetSize(); x ++) delete g_sends.Get(x);
  for (x = 0; x < g_new_net.GetSize(); x ++) delete g_new_net.Get(x);
  for (x = 0; x < g_uploads.GetSize(); x ++) free(g_uploads.Get(x));

  delete g_mql;

  delete g_config;

  return 0;

}

