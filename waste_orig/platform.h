/*
    WASTE - platform.h (Windows / Posixish platform includes and defines)
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

#ifndef _PLATFORM_H_
#define _PLATFORM_H_


#ifdef _WIN32

#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <process.h>
#include <time.h>
#include <commctrl.h>
#include <shlobj.h>
#include <ctype.h>

#define DIRCHAR '\\'

#define sprintf wsprintf
#define safe_strncpy lstrcpyn
#define socklen_t int 

#else

#define THREAD_SAFE
#define _REENTRANT
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#define DIRCHAR '/'
#define CharNext(x) (x+1)
#define CharPrev(s,x) ((s)<(x)?(x)-1:(s))
#define DeleteFile(x) unlink(x)
#define RemoveDirectory(x) rmdir(x)
#define CreateDirectory(x,y) mkdir(x,0755)
#define Sleep(x) usleep(( x )*1000)
#define stricmp(x,y) strcasecmp(x,y)
#define strnicmp(x,y,z) strncasecmp(x,y,z)
static inline char *safe_strncpy(char *out, char *in, int maxl) { strncpy(out,in,maxl); out[maxl-1]=0; }

static inline unsigned int GetTickCount()
{
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return (tv.tv_sec * 1000) + (tv.tv_usec/1000);
}

#define MAX_PATH 1024

//macfag
//#ifndef socklen_t
//#define socklen_t int
//#endif

#endif

#ifndef min
#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((y)<(x)?(x):(y))
#endif

#endif // _PLATFORM_H_
