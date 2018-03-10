void wmiscInit(void);
void wmiscShutdown(void);

wchar_t *wmiscBrowseFolder(const wchar_t *initial);

extern HINSTANCE hInst;

void createToolTip(HWND hDlg, HWND hItem, PTSTR pszText);

struct wmiscSysTrayIcon;
struct wmiscSysTrayIcon *wmiscSysTrayAdd(HWND hWnd, UINT msg);
void wmiscSysTrayDel(struct wmiscSysTrayIcon *icon);

void wmiscLVSetColumns(HWND hlv, ...);
void wmiscLVSetColumnWidths(HWND hlv, ...);
void wmiscLVAddRow(HWND hlv, const char *item, ...);
int wmiscLVGetRow(HWND hlv, int row, int maxlen, ...);

extern HANDLE hIcon;
