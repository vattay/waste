/*
    WASTE - connection.cpp (Secured TCP connection class)
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
#include "util.h"
#include "connection.h"
#include "sockets.h"
extern "C" {
#include "rsa/r_random.h"
#include "rsa/rsa.h"
};

// incoming
C_Connection::C_Connection(int s, struct sockaddr_in *loc)
{
  do_init();
  m_socket=s;
  SET_SOCK_BLOCK(m_socket,0);
  m_remote_port=0;
  m_state=STATE_CONNECTED;
  m_dns=NULL;
  m_ever_connected=0;
  if (loc) m_saddr=*loc;
  else memset(&m_saddr,0,NULL);
  m_cansend=0;
}

// outgoing
C_Connection::C_Connection(char *hostname, int port, C_AsyncDNS *dns)
{
  do_init();
  m_cansend=1;
  m_state=STATE_ERROR;
  m_ever_connected=0;
  m_dns=dns;
  m_remote_port=(short)port;
  m_socket=socket(AF_INET,SOCK_STREAM,0);
  if (m_socket==-1)
  {
    debug_printf("connection: call to socket() failed: %d.\n",ERRNO);
  }
  else
  {
    SET_SOCK_BLOCK(m_socket,0);
    strcpy(m_host,hostname);
    memset(&m_saddr,0,sizeof(m_saddr));
		m_saddr.sin_family=AF_INET;
    m_saddr.sin_port=htons(port);
    m_saddr.sin_addr.s_addr=inet_addr(hostname);
    m_state=STATE_RESOLVING;
  }
}


void C_Connection::init_crypt()
{
  bf_initted=0;

  R_GenerateBytes((unsigned char *)m_send_buffer,CONNECTION_BFPUBKEY,&g_random);

  BLOWFISH_CTX c;

  // according to X9.17, afaik.
  {
    unsigned char tmpkey[8];
    R_GenerateBytes(tmpkey,sizeof(tmpkey),&g_random);
    Blowfish_Init(&c,tmpkey,8);

    unsigned long tm[2]={time(NULL),GetTickCount()};
    Blowfish_Encrypt(&c,tm,tm+1);

    int x;
    unsigned long *t=(unsigned long *)m_send_buffer;
    for (x = 0; x < CONNECTION_BFPUBKEY; x += 8)
    {
      t[0]^=tm[0];
      t[1]^=tm[1];
      Blowfish_Encrypt(&c,t,t+1);
      t+=2;
    }
    memset(&c,0,sizeof(c));
  }

  // first 16 bytes of m_send_buffer are randA
  // next 24 bytes of m_send_buffer are pubKey_A_HASH
  memcpy(m_send_buffer+CONNECTION_BFPUBKEY,g_pubkeyhash,SHA_OUTSIZE);

  int x;

  if (g_use_networkhash)
  {
    // encrypt pubKey_A_HASH with network name, if necessary
    Blowfish_Init(&c,g_networkhash,sizeof(g_networkhash));
    for (x = 0; x < CONNECTION_PKEYHASHSIZE; x += 8)
    {
      Blowfish_Encrypt(&c,(unsigned long *)(m_send_buffer+CONNECTION_BFPUBKEY+x),
            (unsigned long *)(m_send_buffer+CONNECTION_BFPUBKEY+x+4));
    }
    memset(&c,0,sizeof(c));
  }

  Blowfish_Init(&c,(unsigned char *)m_send_buffer,CONNECTION_BFPUBKEY);

  // encrypt pubkey_A_HASH with randA.
  for (x = 0; x < CONNECTION_PKEYHASHSIZE; x += 8)
  {
    Blowfish_Encrypt(&c,(unsigned long *)(m_send_buffer+CONNECTION_BFPUBKEY+x),
                        (unsigned long *)(m_send_buffer+CONNECTION_BFPUBKEY+x+4));        
  }

  memset(&c,0,sizeof(c));

  m_send_len=CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE; // fill in shit later
  //debug_printf("sending public key portion\n");
}

void C_Connection::init_crypt_gotpkey() // got their public key
{
  m_cansend=1;

  // check to make sure randB != randA
  if (!memcmp(m_recv_buffer,m_send_buffer,CONNECTION_BFPUBKEY))
  {
    debug_printf("connection: got what looks like feedback on bfpubkey.\n");
    close(1);
    return;
  }

  BLOWFISH_CTX c;
  int x;


  // get pubkey_B_HASH
  memcpy(m_remote_pkey,m_recv_buffer+CONNECTION_BFPUBKEY,CONNECTION_PKEYHASHSIZE);
  
  // decrypt pubkey_B_HASH with randB
  Blowfish_Init(&c,(unsigned char *)m_recv_buffer,CONNECTION_BFPUBKEY);
  for (x = 0; x < CONNECTION_PKEYHASHSIZE; x += 8)
  {
    Blowfish_Decrypt(&c,(unsigned long *)(m_remote_pkey+x),(unsigned long *)(m_remote_pkey+x+4));        
  }

  // decrypt pubkey_B_HASH with network name. if necessary
  if (g_use_networkhash)
  {
    Blowfish_Init(&c,g_networkhash,sizeof(g_networkhash));
    for (x = 0; x < CONNECTION_PKEYHASHSIZE; x += 8)
    {
      Blowfish_Decrypt(&c,(unsigned long *)(m_remote_pkey+x),(unsigned long *)(m_remote_pkey+x+4));
    }
  }

  memset(&c,0,sizeof(c));

  // find pubkey_B_HASH
  if (!findPublicKey(m_remote_pkey,&remote_pubkey))
  {
    close(1);
    return;
  }


  // at this point, generate my session key and IV (sKeyA)
  R_GenerateBytes(m_mykeyinfo, CONNECTION_KEYSIZE+8, &g_random);

  // according to X9.17, afaik.
  {
    unsigned char tmpkey[8];
    R_GenerateBytes(tmpkey,sizeof(tmpkey),&g_random);
    Blowfish_Init(&c,tmpkey,8);

    unsigned long tm[2]={time(NULL),GetTickCount()};
    Blowfish_Encrypt(&c,tm,tm+1);

    int x;
    unsigned long *t=(unsigned long *)m_mykeyinfo;
    for (x = 0; x < CONNECTION_KEYSIZE+8; x += 8)
    {
      t[0]^=tm[0];
      t[1]^=tm[1];
      Blowfish_Encrypt(&c,t,t+1);
      t+=2;
    }
    memset(&c,0,sizeof(c));
  }

  // save a copy of the last 8 bytes of sKeyA for the send IV
  memcpy(m_send_cbc,m_mykeyinfo+CONNECTION_KEYSIZE,8);

  // append randB to sKeyA.
  memcpy(m_mykeyinfo+CONNECTION_KEYSIZE+8,m_recv_buffer,CONNECTION_BFPUBKEY);

  // create a working copy of sKeyA+randB
  unsigned char localkeyinfo[CONNECTION_KEYSIZE+8+CONNECTION_BFPUBKEY];
  memcpy(localkeyinfo,m_mykeyinfo,sizeof(localkeyinfo));

  // if using a network name, encrypt the first 56 bytes of the sKeyA portion of localkeyinfo
  if (g_use_networkhash)
  {
    Blowfish_Init(&c,g_networkhash,sizeof(g_networkhash));
    for (x = 0; x < CONNECTION_KEYSIZE; x += 8)
    {
      Blowfish_Encrypt(&c,(unsigned long *)(localkeyinfo+x),(unsigned long *)(localkeyinfo+x+4));
    }
    memset(&c,0,sizeof(c));
  }

  // encrypt using pubkey_B, the local key info (sKeyA + randB), (where sKeyA might be encrypted using network name)
  unsigned int l=0;
  if (RSAPublicEncrypt((unsigned char *)m_send_buffer+CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE,&l,localkeyinfo,CONNECTION_KEYSIZE+8+CONNECTION_BFPUBKEY,&remote_pubkey,&g_random))
  {
    debug_printf("connection: error encrypting session key\n");
  }
  memset(localkeyinfo,0,sizeof(localkeyinfo));
#if 0
  {
    int x;
    char buf[512];
    for (x = 0; x < 32; x ++)
    {
      sprintf(buf+x*2,"%02X",m_send_buffer[CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+x]);
    }
    debug_printf("encrypted session info to %s\n",buf);
  }
#endif
}



void C_Connection::init_crypt_decodekey() // got their encrypted session key for me to use
{
  unsigned char m_key[MAX_RSA_MODULUS_LEN];
  unsigned int m_kl=0;
  int err=0;

  // decrypt with my private key to get sKeyB + randA (where sKeyB might be encrypted using network name)
  if ((err=RSAPrivateDecrypt(m_key,&m_kl,(unsigned char *)m_recv_buffer+CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE,
                              (g_key.bits + 7) / 8,&g_key)) || 
        m_kl != CONNECTION_KEYSIZE+8+CONNECTION_BFPUBKEY) //error decrypting
  {
    debug_printf("connection: error decrypting session key with my own private key (%d,%d)\n",err,m_kl);
    close(1);
    return;
  }

  int x;

  //decrypt to get sKeyB if necessary
  if (g_use_networkhash)
  {
    BLOWFISH_CTX c;
    Blowfish_Init(&c,g_networkhash,sizeof(g_networkhash));
    for (x = 0; x < CONNECTION_KEYSIZE; x += 8)
    {
      Blowfish_Decrypt(&c,(unsigned long *)(m_key+x),(unsigned long *)(m_key+x+4));
    }
    memset(&c,0,sizeof(c));
  }
  
  
  // combine the first 56 bytes of sKeyA and sKeyB to get our session key
  // check to make sure it is nonzero.
  int zeros=0;
  for (x = 0; x < CONNECTION_KEYSIZE; x ++)
  {
    m_key[x]^=m_mykeyinfo[x];
    if (!m_key[x]) zeros++;
  }       

  if (zeros == CONNECTION_KEYSIZE)
  {
    debug_printf("connection: zero session key, aborting.\n");
    close(1);
    return;
  }

  // save a copy of the last 8 bytes of sKeyB for the recv IV.
  memcpy(m_recv_cbc,m_key+CONNECTION_KEYSIZE,8);

  if (!memcmp(m_recv_cbc,m_send_cbc,8))
  {
    debug_printf("connection: CBC IVs are equal, being hacked?\n");
    close(1);
    return;
  }

  // check to see the decrypted randA is the same as the randA we sent
  if (memcmp(m_key+CONNECTION_KEYSIZE+8,m_send_buffer,CONNECTION_BFPUBKEY))
  {
    debug_printf("connection: error decrypting session key (signature is wrong, being hacked?)\n");
    close(1);
    return;
  }

  Blowfish_Init(&m_fish,m_key,CONNECTION_KEYSIZE);
  m_ever_connected=1;
}


void C_Connection::do_init(void)
{
  int x;
  m_satmode=0;
  m_remote_maxsend=2048;
  for (x = 0 ; x < sizeof(bps_count)/sizeof(bps_count[0]); x ++)
  {
    bps_count[x].recv_bytes=0;
    bps_count[x].send_bytes=0;
    bps_count[x].time_ms=GetTickCount();
  }
  bps_count_pos=0;

  memset(m_recv_buffer,0,sizeof(m_recv_buffer));
  memset(m_send_buffer,0,sizeof(m_send_buffer));
  m_recv_len=m_recv_pos=0;
  m_send_len=m_send_pos=0;
  m_send_bytes_total=0;
  m_recv_bytes_total=0;
  m_start_time=GetTickCount();
  init_crypt();
}

C_Connection::~C_Connection()
{
  if (m_socket >= 0)
  {
    shutdown(m_socket, SHUT_RDWR );
    closesocket(m_socket);
  }
  memset(&m_fish,0,sizeof(m_fish));
}

void C_Connection::calc_bps(int *send, int *recv)
{
  unsigned int now=GetTickCount();
  unsigned int timediff=now-bps_count[bps_count_pos].time_ms;
  if (timediff < 1) timediff=1;

#ifdef _WIN32
  m_last_sendbps=MulDiv(m_send_bytes_total-bps_count[bps_count_pos].send_bytes,1000,timediff);
  m_last_recvbps=MulDiv(m_recv_bytes_total-bps_count[bps_count_pos].recv_bytes,1000,timediff);
#else
  // fixme: enough prec?
  m_last_sendbps=(((m_send_bytes_total-bps_count[bps_count_pos].send_bytes)*1000)/timediff);
  m_last_recvbps=(((m_recv_bytes_total-bps_count[bps_count_pos].recv_bytes)*1000)/timediff);
#endif

  if (send) *send=m_last_sendbps;
  if (recv) *recv=m_last_recvbps;

  int l=bps_count_pos-1;
  if (l<0) l=sizeof(bps_count)/sizeof(bps_count[0])-1;

  if (now - bps_count[l].time_ms < 0 ||
      now - bps_count[l].time_ms >= 100)
  {
    bps_count[bps_count_pos].send_bytes=m_send_bytes_total;
    bps_count[bps_count_pos].recv_bytes=m_recv_bytes_total;
    bps_count[bps_count_pos].time_ms=now;
    bps_count_pos++;
    if (bps_count_pos>=sizeof(bps_count)/sizeof(bps_count[0])) bps_count_pos=0;
  }
}


C_Connection::state C_Connection::run(int max_send_bytes, int max_recv_bytes)
{
  int bytes_allowed_to_send=(max_send_bytes<0||max_send_bytes>MAX_CONNECTION_SENDSIZE)?MAX_CONNECTION_SENDSIZE:max_send_bytes;
  int bytes_allowed_to_recv=(max_recv_bytes<0||max_recv_bytes>MAX_CONNECTION_RECVSIZE)?MAX_CONNECTION_RECVSIZE:max_recv_bytes;  

  if (GetTickCount() - m_start_time > 10*1000 && bf_initted<12)
  {
    return m_state=STATE_ERROR;
  }

  if (m_socket==-1 || m_state==STATE_ERROR)
  {
    return STATE_ERROR;
  }

  if (m_state == STATE_RESOLVING)
  {
    if (!m_host[0]) return (m_state=STATE_ERROR);
    if (m_saddr.sin_addr.s_addr == INADDR_NONE)
    {
      int a=m_dns?m_dns->resolve(m_host,(unsigned long int *)&m_saddr.sin_addr.s_addr):-1;
      switch (a)
      {
        case 0: m_state=STATE_CONNECTING; break;
        case 1: return STATE_RESOLVING;
        case -1: 
          debug_printf("connection: error resolving hostname\n");
        return (m_state=STATE_ERROR);
      }
    }
    int res=::connect(m_socket,(struct sockaddr *)&m_saddr,16);
    if (!res) 
    {
      m_state=STATE_CONNECTED;
    }
    else if (ERRNO!=EINPROGRESS)
    {
      debug_printf("connection: connect() returned error: %d\n",ERRNO);
      return (m_state=STATE_ERROR);
    }
    else m_state=STATE_CONNECTING;
    return m_state;
  }
  if (m_state == STATE_CONNECTING)
  {		
		fd_set f,f2;
		FD_ZERO(&f);
		FD_ZERO(&f2);
		FD_SET(m_socket,&f);
		FD_SET(m_socket,&f2);
    struct timeval tv;
    memset(&tv,0,sizeof(tv));
		if (select(0,NULL,&f,&f2,&tv)==-1)
		{
      debug_printf("connection::run: select() returned error: %d\n",ERRNO);
      return (m_state=STATE_ERROR);
    }
    if (FD_ISSET(m_socket,&f2))
    {
      debug_printf("connection::run: select() notified of error\n");
      return (m_state=STATE_ERROR);
    }
    if (!FD_ISSET(m_socket,&f)) return STATE_CONNECTING;
    m_state=STATE_CONNECTED;
  }
  int active=0;
  if (m_state == STATE_CONNECTED || m_state == STATE_CLOSING)
  {
    if (m_send_len>0 && m_cansend)
    {
      int len=MAX_CONNECTION_SENDSIZE-m_send_pos;
      if (len > m_send_len) 
      {
        len=m_send_len;
      }

      if (bf_initted<3)
      {
        int l=CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE-m_send_pos;
        if (len > l) len=l;
        if (len<0) len=0;
      }
      else if (bf_initted < 12) // session key negotiation
      {
        int l=CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE-m_send_pos;
        if (len > l) len=l;
        if (len<0) len=0;
      }

      if (len > bytes_allowed_to_send)
      {
        len=bytes_allowed_to_send;
      }
      if (len > 0)
      {
        int res=::send(m_socket,m_send_buffer+m_send_pos,len,0);
        if (res==-1 && ERRNO != EWOULDBLOCK)
        {
//          debug_printf("connection::run: send(%d) returned error: %d\n",len,ERRNO);
          return STATE_CLOSED;
        }
        if (res>0)
        {
          bytes_allowed_to_send-=res;
          m_send_bytes_total+=res;
          m_send_pos+=res;
          m_send_len-=res;
          if (res == len) active=1;
          if (bf_initted < 3)
          {
            if (m_send_pos >= CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE) 
            {
              //debug_printf("sent all of my pubkey\n");
              bf_initted|=2;
            }
          }
          if (bf_initted < 12)
          {
            if (m_send_pos >= CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE) 
            {
              //debug_printf("sent all of my encrypted session key\n");
              bf_initted|=8;
            }
          }
          if (m_send_pos >= MAX_CONNECTION_SENDSIZE) m_send_pos=0;
        }
      }
    }

    int len=MAX_CONNECTION_RECVSIZE-m_recv_len;
    if (len > 0)
    {
      if (len > bytes_allowed_to_recv) len=bytes_allowed_to_recv;

      char rbuf[MAX_CONNECTION_RECVSIZE];
 
      int res=-1;
      if (len > 0)
      {
        res=::recv(m_socket,rbuf,len,0);
        if (res == 0)
        {        
//          debug_printf("connection::run2: recv(%d) returned 0!\n",len);
          return (m_state=STATE_CLOSED);
        }
        if (res < 0 && ERRNO != EWOULDBLOCK) 
        {
//          debug_printf("connection::run: recv() returned error %d!\n",ERRNO);
          return (m_state=STATE_CLOSED);
        }
      }
      if (res > 0)
      {
        if (res == len) active=1;
       
        if (m_recv_pos+res >= MAX_CONNECTION_RECVSIZE) //wrappy
        {
          int l=m_recv_pos+res-MAX_CONNECTION_RECVSIZE;
          memcpy(m_recv_buffer+m_recv_pos,rbuf,res-l);
          if (l) 
          {
            memcpy(m_recv_buffer,rbuf+res-l,l);
          }
          m_recv_pos=l;
//          debug_printf("wrapped\n");
        }
        else
        {
          memcpy(m_recv_buffer+m_recv_pos,rbuf,res);
          m_recv_pos+=res;
        }
        bytes_allowed_to_recv-=res;
        m_recv_bytes_total+=res;
        m_recv_len+=res;

        if (bf_initted < 3) 
        {
          if (!(bf_initted&1) && m_recv_pos >= CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE) // get our key
          {
            m_recv_len-=CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE;
            init_crypt_gotpkey();
            bf_initted|=1;
          }
        }
        if (bf_initted < 12 && !(bf_initted&4))
        {
          if (m_recv_pos >= CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE) // get our key
          {
            bf_initted|=4;
            init_crypt_decodekey();
            m_recv_len-=CONNECTION_PADSKEYSIZE;

            int msp=m_send_pos, msl=m_send_len;
            if (msp < CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE) // only encrypt after first bytes
            {
              msl -= (CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE-msp);
              msp = CONNECTION_BFPUBKEY+CONNECTION_PKEYHASHSIZE+CONNECTION_PADSKEYSIZE;
            }

            if (msl>0)
            {
//              debug_printf("encrypting %d bytes at %d\n",msl,msp);
              encrypt_buffer(m_send_buffer+msp,msl);
            }
          }
        }
      }
    }
    if (m_state == STATE_CLOSING)
    {
      if (m_send_len < 1)
      {
        return (m_state = STATE_CLOSED);
      }
      return STATE_CLOSING;
    }
    return active?STATE_CONNECTED:STATE_IDLE;
  }
  return m_state;
}

void C_Connection::close(int quick)
{
  if (m_state == STATE_RESOLVING || m_state == STATE_CONNECTING ||
      (m_state != STATE_ERROR && quick))
    m_state=STATE_CLOSED;
  else if (m_state != STATE_CLOSED && m_state != STATE_ERROR)
    m_state=STATE_CLOSING;
}

int C_Connection::send_bytes_in_queue(void)
{
  return m_send_len;
}

int C_Connection::getMaxSendSize(void)
{
  int a=MAX_CONNECTION_SENDSIZE;
  if (g_conspeed<64)a=512; //modems only let sendahead be 512 or so.
  else if (g_conspeed<384)a=2048;
  else if (g_conspeed<1600)a=4096;
  else if (g_conspeed<20000)a=8192;
  if (a > m_remote_maxsend) a=m_remote_maxsend;
//  debug_printf("max_size (%d,%d)\n",a,m_remote_maxsend);
  return a;
}

int C_Connection::send_bytes_available(void)
{
  extern int g_conspeed;
  int a=getMaxSendSize();

  a-=m_send_len;
  if (a<0)a=0;
  a &= ~7;
  return a;
}

int C_Connection::send_bytes(void *data, int length)
{
  char tmp[MAX_CONNECTION_SENDSIZE];
  if (!length) return 0;
  memcpy(tmp,data,length);
  if (bf_initted>=12) encrypt_buffer(tmp,length);
  
  int write_pos=m_send_pos+m_send_len;
  if (write_pos >= MAX_CONNECTION_SENDSIZE) 
  {
    write_pos-=MAX_CONNECTION_SENDSIZE;
  }

  int len=MAX_CONNECTION_SENDSIZE-write_pos;
  if (len > length) 
  {
    len=length;
  }
    
  memcpy(m_send_buffer+write_pos,tmp,len);

  if (length > len)
  {
    memcpy(m_send_buffer,tmp+len,length-len);
  }
  m_send_len+=length;

  return 0;
}


void C_Connection::decrypt_buffer(void *data, int len)
{
  if (len & 7)
  {
    debug_printf("connection::decrypt_buffer(): len=%d (&7)\n",len);
  //  char buf[512];
  //  sprintf(buf,"connection::decrypt_buffer(): len=%d (&7)\n",len);
//    MessageBox(NULL,buf,APP_NAME " BIG BIG ERROR",0);
  }
#if 1
  int x;
  char *t=(char *)data;
  for (x = 0; x < len; x += 8) 
  {
    unsigned long *p=(unsigned long*)(t+x);
    unsigned long o1=p[0], o2=p[1];

    Blowfish_Decrypt(&m_fish,p,p+1);

    p[0]^=m_recv_cbc[0];
    p[1]^=m_recv_cbc[1];

    m_recv_cbc[0]^=o1;
    m_recv_cbc[1]^=o2;  
  }
#endif
}

void C_Connection::encrypt_buffer(void *data, int len)
{
  if (len & 7)
  {
    debug_printf("connection::encrypt_buffer(): len=%d (&7)\n",len);
    //char buf[512];
  //  sprintf(buf,"connection::encrypt_buffer(): len=%d (&7)\n",len);
//    MessageBox(NULL,buf,APP_NAME " BIG BIG ERROR",0);
  }
#if 1
  int x;
  char *t=(char *)data;
  for (x = 0; x < len; x += 8) 
  {
    unsigned long *p=(unsigned long*)(t+x);
    p[0]^=m_send_cbc[0];
    p[1]^=m_send_cbc[1];

    Blowfish_Encrypt(&m_fish,p,p+1);

    m_send_cbc[0]^=p[0];
    m_send_cbc[1]^=p[1];

  }
#endif
}



int C_Connection::recv_bytes_available(void)
{
  if (bf_initted < 12) return 0;
  return m_recv_len&(~7);
}

int C_Connection::recv_bytes(void *data, int maxlength)
{  
  if (bf_initted < 12) return -1;
  if (maxlength > m_recv_len)
  {
    maxlength=m_recv_len;
  }
  int read_pos=m_recv_pos-m_recv_len;
  if (read_pos < 0) 
  {
    read_pos += MAX_CONNECTION_RECVSIZE;
  }
  int len=MAX_CONNECTION_RECVSIZE-read_pos;
  if (len > maxlength)
  {
    len=maxlength;
  }
  memcpy(data,m_recv_buffer+read_pos,len);
  if (len < maxlength)
  {
    memcpy((char*)data+len,m_recv_buffer,maxlength-len);
  }

  decrypt_buffer(data,maxlength);

  m_recv_len-=maxlength;
  return maxlength;
}


unsigned long C_Connection::get_interface(void)
{
  if (m_socket==-1) return 0;
  struct sockaddr_in sin;
  memset(&sin,0,sizeof(sin));
  socklen_t len=16;
  if (::getsockname(m_socket,(struct sockaddr *)&sin,&len)) return 0;
  return (unsigned long) sin.sin_addr.s_addr;
}
