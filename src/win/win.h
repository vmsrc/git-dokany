int dokanDllPresent(void);

int winCreate(void);

enum winCmd
{
	winCmd_mount,
	winCmd_umount,
	winCmd_exit
};

int winCmdGet(int mounted);

void winCfgLock(void);
void winCfgUnlock(void);
