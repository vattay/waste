/*
    WASTE - listview.h (Class for management of Windows listviews)
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

#ifndef _LISTVIEW_H_
#define _LISTVIEW_H_


#include <commctrl.h>

class W_ListView 
{
  public:
    W_ListView() { m_hwnd=NULL; m_sort_type=0; m_sort_col=0; m_sort_dir=1; m_col=0; }
    W_ListView(HWND hwnd) { m_hwnd=hwnd; m_col=0; }
    ~W_ListView() { };

    void setwnd(HWND hwnd) 
    { 
      m_hwnd=hwnd; 
      ListView_SetExtendedListViewStyle(m_hwnd,LVS_EX_FULLROWSELECT);
    }

    void AddCol(char *text, int w);
    int GetCount(void) { return ListView_GetItemCount(m_hwnd); }
    int GetParam(int p);
    void DeleteItem(int n) { ListView_DeleteItem(m_hwnd,n); }
    void Clear(void) { ListView_DeleteAllItems(m_hwnd); }
    int GetSelected(int x) { return (ListView_GetItemState(m_hwnd, x, LVIS_SELECTED) & LVIS_SELECTED)?1:0; }
    void SetSelected(int x) { ListView_SetItemState(m_hwnd,x,LVIS_SELECTED,LVIS_SELECTED); }
    int InsertItem(int p, char *text, int param);
    int InsertItemSorted(char *text, int param, char *sorttext);
    void SetItemText(int p, int si, char *text);
    void SetItemParam(int p, int param);

    void GetText(int p, int si, char *text, int maxlen) { ListView_GetItemText(m_hwnd, p, si, text, maxlen); }
    int FindItemByParam(int param)
    {
      LVFINDINFO fi={LVFI_PARAM,0,param};
      return ListView_FindItem(m_hwnd,-1,&fi);
    }
    void SetSortColumn(int col, int bNumeric=0) 
    { 
      if (col == m_sort_col) m_sort_dir=-m_sort_dir;
      else m_sort_dir=1;
      m_sort_col=col; 
      m_sort_type=bNumeric; 
    }
    void SetSortDir(int dir) { m_sort_dir=dir; }
    int GetSortColumn(void) { return m_sort_col; }
    int GetSortDir(void) { return m_sort_dir; }
    int GetColumnWidth(int col);
    void Resort(void);
    HWND getwnd(void) { return m_hwnd; }

  protected:
    HWND m_hwnd;
    int m_col;
    int m_sort_col;
    int m_sort_dir;
    int m_sort_type;
    static int CALLBACK sortfunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort); 
    int _docompare(char *s1, char *s2);
};

#endif//_LISTVIEW_H_
