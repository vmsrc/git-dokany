#include "windows.h"
#include "shobjidl.h"
#include "objbase.h"
#include "shellapi.h"

#include "win_misc.h"

#include "resource.h"

static IFileOpenDialog *pFileOpen;

HANDLE hIcon;

void wmiscInit(void)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (hr>=0)
		// Create the FileOpenDialog object.
		hr=CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void**)(&pFileOpen));
	if (hr<0)
		pFileOpen=NULL;
	INITCOMMONCONTROLSEX icex;
	icex.dwSize=sizeof(icex);
	icex.dwICC=ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&icex);
	hIcon=LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON));
}

void wmiscShutdown(void)
{
	if (pFileOpen)
		pFileOpen->lpVtbl->Release(pFileOpen);
	pFileOpen=NULL;
	CoUninitialize();
}

wchar_t *wmiscBrowseFolder(const wchar_t *initial)
{
	wchar_t *res=NULL;
	HRESULT hr=-1;
	if (pFileOpen)
		hr=pFileOpen->lpVtbl->SetOptions(pFileOpen, FOS_PICKFOLDERS);
	IShellItem *initShItem=NULL;
	if (hr>=0 && initial) {
		HRESULT hr=SHCreateItemFromParsingName(initial, NULL, &IID_IShellItem, &initShItem);
		if (hr>=0 && initShItem)
			hr=pFileOpen->lpVtbl->SetFolder(pFileOpen, initShItem);
	}
	if (hr>=0)
		hr = pFileOpen->lpVtbl->Show(pFileOpen, NULL);
	IShellItem *pItem=NULL;
        if (hr>=0)
		hr = pFileOpen->lpVtbl->GetResult(pFileOpen, &pItem);
	PWSTR pszFilePath;
	if (hr>=0) {
		hr=pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath);
		if (hr>=0)
			res=wcsdup(pszFilePath);
		pItem->lpVtbl->Release(pItem);
	}
	return res;
}

void createToolTip(HWND hDlg, HWND hItem, PTSTR pszText)
{
	HWND hwndTip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL,
		WS_POPUP|TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		hDlg, NULL, 
		hInst, NULL);
    
	if (hwndTip) {
		TOOLINFO toolInfo = { 0 };
		toolInfo.cbSize = TTTOOLINFO_V1_SIZE;
		toolInfo.hwnd = hDlg;
		toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		toolInfo.uId = (UINT_PTR)hItem;
		toolInfo.lpszText = pszText;
		SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
		SendMessage(hwndTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 32700);
		SendMessage(hwndTip, TTM_SETMAXTIPWIDTH, 0, 600);
	}
}

struct wmiscSysTrayIcon {
	NOTIFYICONDATA nid;
};

struct wmiscSysTrayIcon *wmiscSysTrayAdd(HWND hWnd, UINT msg)
{
	struct wmiscSysTrayIcon *icon=malloc(sizeof(*icon));
	memset(icon, 0, sizeof(*icon));
	icon->nid.cbSize=sizeof(icon->nid); 
	icon->nid.hWnd=hWnd;
	icon->nid.uID=0; 
	icon->nid.uFlags=NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP|NIF_ICON;// NIF_ICON|NIF_MESSAGE|NIF_TIP; 
	icon->nid.hIcon= hIcon;
	icon->nid.dwState=NIS_SHAREDICON;
	icon->nid.dwStateMask=NIS_SHAREDICON;
	wcscpy(icon->nid.szTip, L"git-dokany drive");
	icon->nid.uCallbackMessage=msg; 
	Shell_NotifyIcon(NIM_ADD, &icon->nid);
	return icon;
}

void wmiscSysTrayDel(struct wmiscSysTrayIcon *icon)
{
	Shell_NotifyIcon(NIM_DELETE, &icon->nid);
	free(icon);
}

void wmiscLVSetColumns(HWND hlv, ...)
{
	va_list ap;
	va_start(ap, hlv);
	LVCOLUMNA lvc={0};
	lvc.mask=LVCF_TEXT;
	lvc.iSubItem=0;
	while (lvc.pszText=va_arg(ap, char *)) {
		SendMessageA(hlv, LVM_INSERTCOLUMNA, lvc.iSubItem, (LPARAM)&lvc);
		lvc.iSubItem++;
	}
	for (int i=0; i<lvc.iSubItem; i++)
		SendMessageA(hlv, LVM_SETCOLUMNWIDTH, i, LVSCW_AUTOSIZE_USEHEADER);
	va_end(ap);
}

void wmiscLVSetColumnWidths(HWND hlv, ...)
{
	va_list ap;
	va_start(ap, hlv);
	int width;
	int subItem=0;
	while (width=va_arg(ap, int))
		SendMessageA(hlv, LVM_SETCOLUMNWIDTH, subItem++, width);
	va_end(ap);
}

void wmiscLVAddRow(HWND hlv, const char *item, ...)
{
	LVITEMA lvi={0};
	lvi.mask=LVIF_TEXT;
	lvi.iItem=INT_MAX;
	lvi.iSubItem=0;
	lvi.pszText=(char *)item;
	LRESULT n=SendMessageA(hlv, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
	if (n>=0) {
		int s=1;
		char *p;
		va_list ap;
		va_start(ap, item);
		while (p=va_arg(ap, char *)) {
			LV_ITEMA lvi;
			lvi.iSubItem = (s++);
			lvi.pszText = (p);
			SendMessageA(hlv, LVM_SETITEMTEXTA, (WPARAM)(n), (LPARAM)&lvi);
		}
		va_end(ap);
	}
}

int wmiscLVGetRow(HWND hlv, int row, int maxlen, ...)
{
	LVITEMA lvi={0};
	lvi.mask=LVIF_TEXT;
	lvi.iItem=row;
	lvi.iSubItem=0;
	lvi.cchTextMax=maxlen;
	LRESULT n=FALSE;
	char *p;
	va_list ap;
	va_start(ap, maxlen);
	while (p=va_arg(ap, char *)) {
		lvi.pszText=p;
		n=SendMessageA(hlv, LVM_GETITEMA, 0, (LPARAM)&lvi);
		lvi.iSubItem++;
		if (!n)
			break;
	}
	va_end(ap);
	return n ? 0 : -1;
}
