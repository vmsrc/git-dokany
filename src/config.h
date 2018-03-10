enum
{
	cfggui_cononly,
	cfggui_guionly,
	cfggui_congui
};

struct cfg
{
	int cacheSizeMB;
	char *mountPt;
	int daemon;
	int gui;
};

extern struct cfg cfg;

void cfgReset(void);
void cfgSane(void);
int cfgSave(const char *file);
int cfgLoad(const char *file, int append);
int cfgParseOpt(const char *opt);

int cfgAddRepo(const char *repo);
int cfgAddMnt(const char *treeish, const char *mapto, int submodules);

void cfgEnumReset(void);
void cfgEnumNextMount(const char **repo, const char **treeish, const char **mapto, int *submodules);
