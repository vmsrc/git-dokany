#include <stdio.h>
#include <stdarg.h>

#include "windows.h"
#include "commctrl.h"

#include "win.h"
#include "win_misc.h"
#include "../config.h"

int dokanDllPresent(void)
{
	return NULL!=LoadLibrary(L"dokan1.dll");
}

static int created;
static CRITICAL_SECTION csCfg;

void winCfgLock(void)
{
	if (created)
		EnterCriticalSection(&csCfg);
}

void winCfgUnlock(void)
{
	if (created)
		LeaveCriticalSection(&csCfg);
}

struct dlgItem {
	DLGITEMTEMPLATE item;
	WORD *clas, *text, *data;
	struct dlgItem *next;
};

struct dlgTemplate {
	DLGTEMPLATE templ;
	size_t bytes;
	WORD *menu, *clas, *title;
	short ptsize;
	WORD *font;
	struct dlgItem *first, *last;
};

static struct dlgTemplate *newDlgTemplate(DWORD style, DWORD extStyle, int x, int y, int cx, int cy,
	const WORD *menu, const WORD *clas, const WORD *title, short ptsize, const WORD *font)
{
	struct dlgTemplate *res=malloc(sizeof(struct dlgTemplate));
	res->templ.style=style;
	res->templ.dwExtendedStyle=extStyle;
	res->templ.cdit=0;
	res->templ.x=x;
	res->templ.y=y;
	res->templ.cx=cx;
	res->templ.cy=cy;
	res->bytes=sizeof(struct dlgTemplate);
	res->font=NULL;
	if (font) {
		res->ptsize=ptsize;
		res->font=wcsdup(font);
		res->bytes+=2 + 2*wcslen(font) + 2;
	}
	if (!menu || menu[0]==0) {
		res->menu=NULL;
		res->bytes+=2;
	} else if (menu[0]==0xffff) {
		res->menu=malloc(4);
		res->menu[0]=menu[0];
		res->menu[1]=menu[1];
		res->bytes+=2*2;
	} else {
		res->menu=wcsdup(menu);
		res->bytes+=2*wcslen(menu) + 2;
	}
	if (!clas || clas[0]==0) {
		res->clas=NULL;
		res->bytes+=2;
	} else if (clas[0]==0xffff) {
		res->clas=malloc(4);
		res->clas[0]=clas[0];
		res->clas[1]=clas[1];
		res->bytes+=2*2;
	} else {
		res->clas=wcsdup(clas);
		res->bytes+=2*wcslen(clas) + 2;
	}
	res->title=NULL;
	res->bytes+=2;
	if (title) {
		res->title=wcsdup(title);
		res->bytes+=wcslen(title);
	}
	res->first=res->last=NULL;
	return res;
}

static WORD addItem(
	struct dlgTemplate *t, DWORD style, DWORD extStyle, short x, short y, short cx, short cy,
	const WORD *clas, const WORD *text, const WORD *data
	)
{
	if (t->bytes&3)
		t->bytes+=4 - (t->bytes&3);
	struct dlgItem *item=malloc(sizeof(struct dlgItem));
	item->item.style=style;
	item->item.dwExtendedStyle=extStyle;
	item->item.x=x;
	item->item.y=y;
	item->item.cx=cx;
	item->item.cy=cy;
	item->item.id=t->templ.cdit++ + 100;
	t->bytes+=sizeof(struct dlgItem);
	if (clas[0]==0xffff) {
		item->clas=malloc(2*2);
		item->clas[0]=clas[0];
		item->clas[1]=clas[1];
		t->bytes+=2*2;
	} else {
		t->bytes+=wcslen(clas)*2 + 2;
		item->clas=wcsdup(clas);
	}
	if (text[0]==0xffff) {
		item->text=malloc(2*2);
		item->text[0]=text[0];
		item->text[1]=text[1];
		t->bytes+=2*2;
	} else {
		t->bytes+=wcslen(clas)*2 + 2;
		item->text=wcsdup(text);
	}
	if (data && data[0]>2) {
		item->data=malloc(data[0]);
		memcpy(item->data, data, data[0]);
		t->bytes+=data[0];
	} else {
		t->bytes+=2;
		item->data=NULL;
	}
	item->next=NULL;
	if (t->last)
		t->last->next=item;
	else
		t->first=item;
	t->last=item;
	return item->item.id;
}

LPDLGTEMPLATE buildDlgTemplate(struct dlgTemplate *t)
{
	char *res=malloc(t->bytes + 3);
	char *p=res;
	memcpy(p, &t->templ, sizeof(t->templ));
	p+=sizeof(t->templ);
	if (!t->menu || t->menu[0]==0) {
		*(WORD *)p=0;
		p+=2;
	} else if (t->menu[0]==0xffff) {
		*(WORD *)p=0xffff;
		p+=2;
		*(WORD *)p=t->menu[1];
		p+=2;
	} else {
		wcscpy((wchar_t *)p, t->menu);
		p+=wcslen(t->menu)*2 + 2;
	}
	free(t->menu);
	if (!t->clas || t->clas[0]==0) {
		*(WORD *)p=0;
		p+=2;
	} else if (t->clas[0]==0xffff) {
		*(WORD *)p=0xffff;
		p+=2;
		*(WORD *)p=t->clas[1];
		p+=2;
	} else {
		wcscpy((wchar_t *)p, t->clas);
		p+=wcslen(t->clas)*2 + 2;
	}
	free(t->clas);
	if (!t->title) {
		*(WORD *)p=0;
		p+=2;
	} else {
		wcscpy((wchar_t *)p, t->title);
		p+=wcslen(t->title)*2 + 2;
	}
	free(t->title);
	if (t->font) {
		*(WORD *)p=t->ptsize;
		p+=2;
		wcscpy((wchar_t *)p, t->font);
		p+=wcslen(t->font)*2 + 2;
	}
	free(t->font);
	struct dlgItem *el=t->first;
	while (el) {
		while ((p - res)&3)
			*p++=0;
		memcpy(p, &el->item, sizeof(el->item));
		p+=sizeof(el->item);
#if 0
		while ((p - res)&3)
			*p++=0;
#endif
		if (el->clas[0]==0xffff) {
			*(WORD *)p=0xffff;
			p+=2;
			*(WORD *)p=el->clas[1];
			p+=2;
		} else {
			wcscpy((wchar_t *)p, el->clas);
			p+=wcslen(el->clas)*2 + 2;
		}
		free(el->clas);
		if (el->text[0]==0xffff) {
			*(WORD *)p=0xffff;
			p+=2;
			*(WORD *)p=el->text[1];
			p+=2;
		} else {
			wcscpy((wchar_t *)p, el->text);
			p+=wcslen(el->text)*2 + 2;
		}
		free(el->text);
		if (!el->data || el->data[0]<3) {
			*(WORD *)p=0;
			p+=2;
		} else {
			memcpy(p, el->data, el->data[0]);
			p+=el->data[0];
		}
		free(el->data);
		struct dlgItem *tmp=el;
		el=el->next;
		free(tmp);
	}
	free(t);
	return (LPDLGTEMPLATE)res;
}

static WORD bnSave, bnLoad, bnAppend, bnConsole;
static WORD cbDriveLetter;
static WORD bnMap, bnUnmap;
static WORD edCache;
static WORD edTreeish, edPath;
static WORD bnAdd;
static WORD edBrowseRepo, bnBrowseRepo;
static WORD lvList;
static DWORD logicalDrives;

static enum winCmd winCmd;
static HANDLE hSem;
static HANDLE hDlg;

static void listViewDelSelection(void)
{
	HWND hlv=GetDlgItem(hDlg, lvList);
	int n=-1;
	while (1) {
		n=ListView_GetNextItem(hlv, n, LVNI_SELECTED);
		if (n<0)
			break;
		ListView_DeleteItem(hlv, n--);
	}
}

static void trimws(char *str)
{
	char *p=str;
	while (*p>0 && *p<32)
		++p;
	if (p>str)
		memmove(str, p, strlen(p) + 1);
	size_t len=strlen(str);
	while (len) {
		--len;
		if (str[len]>0 && str[len]<32)
			str[len]='\0';
	}
}

static void addRepoIfUniq(void)
{
	char nrepo[512];
	GetWindowTextA(GetDlgItem(hDlg, edBrowseRepo), nrepo, 512);
	char ntreeish[512];
	GetWindowTextA(GetDlgItem(hDlg, edTreeish), ntreeish, 512);
	char npath[512];
	GetWindowTextA(GetDlgItem(hDlg, edPath), npath, 512);
	trimws(nrepo);
	trimws(ntreeish);
	trimws(npath);

	HWND hlv=GetDlgItem(hDlg, lvList);
	for (int i=0; i<10000; i++) {
		char repo[1050], mapto[1050], treeish[1050];
		if (wmiscLVGetRow(hlv, i, 1024, &treeish, &mapto, &repo, NULL))
			break;
		repo[1024]='\0';
		mapto[1024]='\0';
		treeish[1024]='\0';
		if (!strcmp(repo, nrepo) && !strcmp(mapto, npath) && !strcmp(treeish, ntreeish))
			return;
	}
	wmiscLVAddRow(GetDlgItem(hDlg, lvList), ntreeish, npath, nrepo, NULL);
}

static void applyUIToCfg(void)
{
	cfgReset();
	addRepoIfUniq();
	HWND hBn=GetDlgItem(hDlg, bnConsole);
	LRESULT state=SendMessage(hBn, BM_GETSTATE, 0, 0);
	cfg.gui=(state&BST_CHECKED) ? cfggui_congui : cfggui_guionly;
	char str[1000];
	GetWindowTextA(GetDlgItem(hDlg, edCache), str, 999);
	str[999]='\0';
	sscanf(str, "%d", &cfg.cacheSizeMB);
	{
		LRESULT sel=SendMessage(GetDlgItem(hDlg, cbDriveLetter), CB_GETCURSEL, 0, 0);
		int pos=-1;
		cfg.drive[0]='z';
		for (int i=0; i<26; i++) {
			if (!(logicalDrives & (1<<i))) {
				++pos;
				if (pos==sel) {
					cfg.drive[0]='A' + i;
					break;
				}
			}
		}
		cfg.drive[1]=':';
		cfg.drive[2]='\0';
	}
	{
		HWND hlv=GetDlgItem(hDlg, lvList);
		for (int i=0; i<10000; i++) {
			char repo[1050], mapto[1050], treeish[1050];
			if (wmiscLVGetRow(hlv, i, 1024, &treeish, &mapto, &repo, NULL))
				break;
			repo[1024]='\0';
			mapto[1024]='\0';
			treeish[1024]='\0';
			cfgAddRepo(repo);
			cfgAddMnt(treeish, mapto);
		}
	}
	cfgSane();
}

static char toUpper(char c)
{
	if (c>='a' && c<='z')
		c-='a' - 'A';
	return c;
}

static void applyCfgToUI(void)
{
	HWND hBnConsole=GetDlgItem(hDlg, bnConsole);
	SendMessage(hBnConsole, BM_SETCHECK, cfg.gui==cfggui_guionly ? BST_UNCHECKED : BST_CHECKED, 0);
	HANDLE hCon=GetConsoleWindow();
	ShowWindow(hCon, (cfg.gui==cfggui_guionly) ? SW_HIDE : SW_SHOWNOACTIVATE);
	char str[1000];
	sprintf(str, "%d", cfg.cacheSizeMB);
	SetWindowTextA(GetDlgItem(hDlg, edCache), str);
	{
		int drive=toUpper(cfg.drive[0]) - 'A';
		int i;
		int pos=-1;
		for (i=0; i<26; i++) {
			if (!(logicalDrives & (1<<i))) {
				++pos;
				if (i==drive) {
					++pos;
					break;
				}
			}
		}
		--pos;
		SendMessage(GetDlgItem(hDlg, cbDriveLetter), CB_SETCURSEL, pos, 0);

	}
	{
		cfgEnumReset();
		const char *validRepo=NULL, *validTreeish=NULL, *validMapto=NULL;
		const char *repo=NULL;
		const char *treeish;
		const char *mapto;
		HWND hlv=GetDlgItem(hDlg, lvList);
		ListView_DeleteAllItems(hlv);
		while (1) {
			cfgEnumNextMount(&repo, &treeish, &mapto);
			if (!repo)
				break;
			if (treeish && mapto) {
				wmiscLVAddRow(hlv, treeish, mapto, repo, NULL);
				validRepo=repo;
				validTreeish=treeish;
				validMapto=mapto;
			}
		}
		if (validRepo) {
			SetWindowTextA(GetDlgItem(hDlg, edBrowseRepo), validRepo);
			SetWindowTextA(GetDlgItem(hDlg, edTreeish), validTreeish);
			SetWindowTextA(GetDlgItem(hDlg, edPath), validMapto);
		}
	}
}

static INT_PTR CALLBACK dlgProc(HWND hDlgIn, UINT msg, WPARAM wParam, LPARAM lParam)
{
	hDlg=hDlgIn;
	switch (msg) {
	case WM_USER:
		if (wParam==0 && lParam==WM_LBUTTONUP) {
			if (IsWindowVisible(hDlg))
				ShowWindow(hDlg, SW_HIDE);
			else
				ShowWindow(hDlg, SW_SHOWNORMAL);
		}
		if (wParam==0 && lParam==WM_RBUTTONUP) {
			HMENU menu=CreatePopupMenu();
			InsertMenu(menu, -1, MF_BYPOSITION, 1, L"Exit");
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hDlg);
			int res=TrackPopupMenu(menu, TPM_RIGHTBUTTON|TPM_BOTTOMALIGN|TPM_RETURNCMD|TPM_NONOTIFY, pt.x, pt.y, 0, hDlg, NULL);
			PostMessage(hDlg, WM_NULL, 0, 0);
			DestroyMenu(menu);
			if (1==res)
				PostMessage(hDlg, WM_CLOSE, 0, 0);
		}
		if (wParam==1) {
			EnableWindow(GetDlgItem(hDlg, bnUnmap), lParam!=0);
			EnableWindow(GetDlgItem(hDlg, bnMap), TRUE);
			SetWindowTextA(GetDlgItem(hDlg, bnMap), lParam ? "Re&map" : "&Map");
		}
		return TRUE;
	case WM_INITDIALOG: {
		SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
		SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
		HWND hCb=GetDlgItem(hDlg, cbDriveLetter);
		int sel=-1;
		for (int i=0; i<26; i++) {
			if (!(logicalDrives & (1<<i))) {
				WORD letter[2]={0};
				letter[0]='A' + i;
				SendMessage(hCb, CB_ADDSTRING, 0, (LPARAM)letter);
				++sel;
			}
		}
		createToolTip(hDlg, GetDlgItem(hDlg, bnSave), L"Save current configuration");
		createToolTip(hDlg, GetDlgItem(hDlg, bnLoad), L"Save configuration from file");
		createToolTip(hDlg, GetDlgItem(hDlg, bnAppend), L"Append configuration settings from file");
		createToolTip(hDlg, GetDlgItem(hDlg, bnConsole), L"Show or hide console window");
		createToolTip(hDlg, GetDlgItem(hDlg, cbDriveLetter), L"Drive letter to map to");
		createToolTip(hDlg, GetDlgItem(hDlg, bnMap), L"Apply settings, map specified trees and mount drive");
		createToolTip(hDlg, GetDlgItem(hDlg, bnUnmap), L"Free memory and unmount drive");
		createToolTip(hDlg, GetDlgItem(hDlg, edCache), L"Cached files pool size, MB");
		createToolTip(hDlg, GetDlgItem(hDlg, edTreeish), L"Git tree-ish to map, e.g. HEAD, HEAD~1, master:src/common, or hash");
		createToolTip(hDlg, GetDlgItem(hDlg, edPath),
			L"Path to map the tree-ish tom, relative to selected drive letter.\n"
			L"Special tags <treeish> and <hash> substitute the specified tree-ish and its hash."
		);
		createToolTip(hDlg, GetDlgItem(hDlg, bnAdd), L"Add selected repo, tree-ish and path to the list.\nMap or remap to apply.");
		createToolTip(hDlg, GetDlgItem(hDlg, edBrowseRepo), L"Path to git repository");
		createToolTip(hDlg, GetDlgItem(hDlg, bnBrowseRepo), L"Browse repository path");
		createToolTip(hDlg, GetDlgItem(hDlg, lvList), L"Map list");

		EnableWindow(GetDlgItem(hDlg, bnUnmap), FALSE);

		HWND hlv=GetDlgItem(hDlg, lvList);
		wmiscLVSetColumns(hlv, "tree-ish", "path", "repo", NULL);
		winCfgLock();
		applyCfgToUI();
		winCfgUnlock();
		return TRUE;
	}
	case WM_NOTIFY: {
		if (wParam==lvList) {
			int del=0;
			NMHDR *hdr=(NMHDR *)lParam;
			if (hdr->code==LVN_KEYDOWN) {
				NMLVKEYDOWN *nkd=(NMLVKEYDOWN *)hdr;
				if (nkd->wVKey==VK_DELETE)
					del=1;
			}
			if (hdr->code==NM_RCLICK) {
			HMENU menu=CreatePopupMenu();
				InsertMenu(menu, -1, MF_BYPOSITION, 1, L"Delete selection");
				POINT pt;
				GetCursorPos(&pt);
				SetForegroundWindow(hDlg);
				int res=TrackPopupMenu(menu, TPM_RIGHTBUTTON|TPM_BOTTOMALIGN|TPM_RETURNCMD|TPM_NONOTIFY, pt.x, pt.y, 0, hDlg, NULL);
				PostMessage(hDlg, WM_NULL, 0, 0);
				DestroyMenu(menu);
				if (1==res)
					del=1;
			}
			if (del)
				listViewDelSelection();
		}
		break;
	}
	case WM_SYSCOMMAND: {
		if (wParam==SC_MINIMIZE) {
			ShowWindow(hDlg, SW_HIDE);
			return TRUE;
		}
		break;
	}
	case WM_COMMAND: {
		if (wParam==bnMap || wParam==bnUnmap) {
			EnableWindow((HWND)lParam, FALSE);
			winCmd=(wParam==bnUnmap) ? winCmd_umount : winCmd_mount;
			winCfgLock();
			applyUIToCfg();
			winCfgUnlock();
			ReleaseSemaphore(hSem, 1, NULL);
		}
		if (wParam==bnSave || wParam==bnLoad || wParam==bnAppend) {
			char file[1000]="\0";
			OPENFILENAMEA ofn={
				.lStructSize=sizeof(ofn),
				.hwndOwner=hDlg,
				.hInstance=hInst,
				.lpstrFile=file,
				.nMaxFile=1000,
				.lpstrFilter="Config (*.cfg)\0*.cfg\0All (*.*)\0*.*\0\0",
				.lpstrDefExt="cfg",
				.Flags = (wParam==bnSave) ? OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT : OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST,
			};
			if (wParam==bnSave && GetSaveFileNameA(&ofn)) {
				winCfgLock();
				applyUIToCfg();
				cfgSave(file);
				winCfgUnlock();
			}
			if (wParam!=bnSave && GetOpenFileNameA(&ofn)) {
				winCfgLock();
				cfgLoad(file, wParam==bnAppend);
				applyCfgToUI();
				winCfgUnlock();
			}
		}
		if (wParam==bnBrowseRepo) {
			HWND hBn=(HWND)lParam;
			HWND hEd=GetDlgItem(hDlg, edBrowseRepo);
			WORD path[512];
			GetWindowText(hEd, path, 512);
			WORD *newpath=wmiscBrowseFolder(path);
			if (newpath)
				SetWindowText(hEd, newpath);
		}
		if (wParam==bnConsole) {
			HANDLE hCon=GetConsoleWindow();
			HWND hBn=(HWND)lParam;
			LRESULT state=SendMessage(hBn, BM_GETSTATE, 0, 0);
			ShowWindow(hCon, (state&BST_CHECKED) ? SW_SHOWNOACTIVATE : SW_HIDE);
		}
		if (wParam==bnAdd)
			addRepoIfUniq();
		return TRUE;
	}
	case WM_CLOSE:
		DestroyIcon(hIcon);
		DestroyWindow(hDlg);
		PostQuitMessage(0);
		return TRUE;
	}
	return FALSE;
}

HINSTANCE hInst;

static HWND createDialog(void)
{
	static const WORD
		bnclas[]={ 0xffff, 0x0080 }, // button
		edclas[]={ 0xffff, 0x0081 }, // edit
		stclas[]={ 0xffff, 0x0082 }, // static
		lbclas[]={ 0xffff, 0x0083 }, // listbox
		scclas[]={ 0xffff, 0x0084 }, // scroll
		cbclas[]={ 0xffff, 0x0085 }; // combo

	struct dlgTemplate *t1=newDlgTemplate(
		WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|DS_SETFONT,
		0, CW_USEDEFAULT, CW_USEDEFAULT, 320, 223, NULL, NULL, L"GitDrive", 8, L"Tahoma");
	int x=6, y=3;

	bnSave=addItem(t1, WS_VISIBLE, 0, x, y, 60, 12, bnclas, L"&Save", NULL);
	x+=70;
	bnLoad=addItem(t1, WS_VISIBLE, 0, x, y, 60, 12, bnclas, L"&Load", NULL);
	x+=70;
	bnAppend=addItem(t1, WS_VISIBLE, 0, x, y, 60, 12, bnclas, L"A&ppend", NULL);
	x+=124;
	bnConsole=addItem(t1, WS_VISIBLE|BS_AUTOCHECKBOX|BS_LEFTTEXT, 0, x, y, 40, 12, bnclas, L"&Console", NULL);

	x=6;
	y+=19;

	addItem(t1, WS_VISIBLE, 0, x, y+2, 20, 12, stclas, L"drive", NULL);
	x+=44;
	cbDriveLetter=addItem(t1, WS_VISIBLE|CBS_DROPDOWNLIST, 0, x, y, 25, 300, cbclas, L"", NULL);

	x+=36;
	bnMap=addItem(t1, WS_VISIBLE, 0, x, y, 60, 12, bnclas, L"&Map", NULL);
	x+=70;
	bnUnmap=addItem(t1, WS_VISIBLE|WS_DISABLED, 0, x, y, 60, 12, bnclas, L"&Unmap", NULL);

	x+=70;
	addItem(t1, WS_VISIBLE, 0, x, y+2, 40, 12, stclas, L"cache (MB)", NULL);
	x+=44;
	edCache=addItem(t1, WS_VISIBLE|WS_BORDER|ES_RIGHT|ES_NUMBER, 0, x, y, 40, 12, edclas, L"", NULL);

	x=6;
	y+=19;

	addItem(t1, WS_VISIBLE, 0, x, y+2, 30, 12, stclas, L"repo", NULL);
	x+=44;
	edBrowseRepo=addItem(t1, WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 0, x, y, 190, 12, edclas, L"", NULL);
	x+=200;
	bnBrowseRepo=addItem(t1, WS_VISIBLE, 0, x, y, 60, 12, bnclas, L"&Browse", NULL);
	x+=80;

	x=6;
	y+=15;

	addItem(t1, WS_VISIBLE, 0, x, y+2, 30, 12, stclas, L"tree-ish", NULL);
	x+=44;
	edTreeish=addItem(t1, WS_VISIBLE|WS_BORDER, 0, x, y, 190, 12, edclas, L"HEAD", NULL);
	x=6;
	y+=15;
	addItem(t1, WS_VISIBLE, 0, x, y+2, 40, 12, stclas, L"path", NULL);
	x+=44;
	edPath=addItem(t1, WS_VISIBLE|WS_BORDER, 0, x, y, 190, 12, edclas, L"<treeish>", NULL);
	x+=200;
	bnAdd=addItem(t1, WS_VISIBLE, 0, x, y, 60, 12, bnclas, L"&Add", NULL);

	x=6;
	y+=19;
	lvList=addItem(t1, WS_VISIBLE|WS_BORDER|LVS_REPORT|LVS_NOSORTHEADER, 0, x, y, 306, 130, WC_LISTVIEW, L"", NULL);

	x=6;
	y+=139;

	LPDLGTEMPLATE t=buildDlgTemplate(t1);
	HWND hDlg=CreateDialogIndirectParam(hInst, t, NULL, dlgProc, 0);
	free(t);
	return hDlg;
}

static int myOwnConsole;

static DWORD WINAPI guiThread(LPVOID param)
{
	createDialog();
	if (!hDlg) {
		ReleaseSemaphore(hSem, 1, NULL);
		return 1;
	}
	struct wmiscSysTrayIcon *trayIcon=wmiscSysTrayAdd(hDlg, WM_USER);
	ShowWindow(hDlg, SW_SHOW);
	if (cfg.gui==cfggui_guionly) {
		HANDLE hCon=GetConsoleWindow();
		ShowWindow(hCon, SW_HIDE);
	}
	ReleaseSemaphore(hSem, 1, NULL);
	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0)) {
		if(!IsDialogMessage(hDlg, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	wmiscSysTrayDel(trayIcon);
	winCmd=winCmd_exit;
	ReleaseSemaphore(hSem, 1, NULL);
	if (!myOwnConsole){
		HANDLE hCon=GetConsoleWindow();
		ShowWindow(hCon, SW_SHOW);
	}
	return 0;
}

int winCreate(void)
{
	hInst=GetModuleHandle(NULL);
	HWND hCon=GetConsoleWindow();
	DWORD pid=0;
	GetWindowThreadProcessId(hCon, &pid);
	myOwnConsole=(GetCurrentProcessId()==pid);
	if (!myOwnConsole)
		cfg.gui=cfggui_congui;
	hSem=CreateSemaphore(NULL, 0, 1, NULL);
	InitializeCriticalSection(&csCfg);
	wmiscInit();
	logicalDrives=GetLogicalDrives();
	if (CreateThread(NULL, 0, guiThread, NULL, 0, NULL)) {
		WaitForSingleObject(hSem, INFINITE);
		if (!dokanDllPresent())
			MessageBoxA(
				hDlg,
				"It seems that the DOKANY system driver is not present.\n"
				"This program requires the DOKANY driver to operate.",
				"Warning",
				MB_OK
			);
	}
	created=(hDlg!=NULL);
	return created ? 0 : -1;
}

int winCmdGet(int mounted)
{
	PostMessage(hDlg, WM_USER, 1, mounted);
	if (created)
		WaitForSingleObject(hSem, INFINITE);
	return winCmd;
}
