/*
    WASTE - util.h (General utility code)
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
#ifndef _UTIL_H_
#define _UTIL_H_

#include "rsa/global.h"
#include "rsa/rsaref.h"
#include "itemlist.h"
#ifdef _WIN32
#include "listview.h"
#endif
#include "sha.h"

// GUID
typedef struct 
{
  unsigned char idc[16];
} T_GUID;

void CreateID128(T_GUID *id);
void MakeID128Str(T_GUID *id, char *str);
int MakeID128FromStr(char *str, T_GUID *id);// return 0 on success


// RNG
void MYSRANDUPDATE(unsigned char *buf=NULL, int bufl=0);
void MYSRAND();
extern R_RANDOM_STRUCT g_random;

// string utilities
char *extension(char *fn);


#ifdef _WIN32
// windows shit
extern int toolwnd_state;
extern int systray_state;
BOOL systray_del(HWND hwnd);
BOOL systray_add(HWND hwnd, HICON icon);
void DoFlashWindow(HWND hwndParent, int timeoutval);
HWND CreateTooltip(HWND hWnd, LPSTR strTT);
void toolWindowSet(int twstate);
void SetWndTitle(HWND hwnd, char *titlebase);
#endif

void removeInvalidFNChars(char *filename); // careful, removes colons too, so 
// when using on a full path, use +2. =)


#ifdef _WIN32
int doLoadKey(HWND hwndParent, char *pstr, char *keyfn, R_RSA_PRIVATE_KEY *key);
int kg_writePrivateKey(char *fn, R_RSA_PRIVATE_KEY *key, R_RANDOM_STRUCT *rnd, char *passhash);
void reloadKey(char *passstr, HWND hwndParent);

#else

int doLoadKey(char *pstr, char *keyfn, R_RSA_PRIVATE_KEY *key);
void reloadKey(char *passstr);

#endif


// access control shit
typedef struct 
{
  unsigned int ip;
  char maskbits;
  char allow;
} ACitem;

extern ACitem *g_aclist;
extern int g_aclist_size;
#ifdef _WIN32
void updateACList(W_ListView *lv);
#else
void updateACList();
#endif

int ACStringToStruct(char *t, ACitem *i);
int allowIP(unsigned int a);
int isaccessable(char *host);


// conspeed
extern char *conspeed_strs[5];
extern int conspeed_speeds[5];
int get_speedstr(int kbps, char *str);

// debug
void debug_printf(char *text,...);


extern R_RSA_PRIVATE_KEY g_key;
extern unsigned char g_pubkeyhash[SHA_OUTSIZE];

typedef struct 
{
  unsigned char hash[SHA_OUTSIZE];
  char name[16];
  R_RSA_PUBLIC_KEY pk;
} PKitem;

extern C_ItemList<PKitem> g_pklist, g_pklist_pending;

#ifdef _WIN32
void copyMyPubKey(HWND hwndOwner);
#endif

void savePKList();
int loadPKList(char *fn=NULL); // returns num of keys loaded
char *findPublicKey(unsigned char *hash, R_RSA_PUBLIC_KEY *out); // NULL on err
int findPublicKeyFromKey(R_RSA_PUBLIC_KEY *key); // 1 on found

//crc
unsigned long crc32(unsigned long crc, unsigned char *buf, unsigned int len);

#endif
