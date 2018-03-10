#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "v_misc.h"

#include "config.h"

struct cfg cfg;

struct cfgMnt
{
	char *treeish;
	char *mapto;
	struct cfgMnt *next;
	int submodules;
};

struct cfgRepo
{
	char *repo;
	struct cfgMnt *firstMnt, *lastMnt;
	struct cfgRepo *next;
};

static struct cfgRepo *firstRepo, *lastRepo;

static int submodules;
static char treeish[1024];
static int mappathValid;
static char mappath[1024];

static void freeMnt(struct cfgMnt *mnt)
{
	free(mnt->treeish);
	free(mnt->mapto);
	free(mnt);
}

static void freeRepo(struct cfgRepo *repo)
{
	free(repo->repo);
	while (repo->firstMnt) {
		struct cfgMnt *tmp=repo->firstMnt->next;
		freeMnt(repo->firstMnt);
		repo->firstMnt=tmp;
	}
	repo->lastMnt=NULL;
	free(repo);
}

void cfgReset(void)
{
	treeish[0]='\0';
	mappath[0]='\0';
	mappathValid=0;
	submodules=0;
	free(cfg.mountPt);
	memset(&cfg, 0, sizeof(cfg));
	cfg.gui=1;
	cfg.cacheSizeMB=200;
	cfg.daemon=0;
	while (firstRepo) {
		struct cfgRepo *tmp=firstRepo->next;
		freeRepo(firstRepo);
		firstRepo=tmp;
	}
	lastRepo=NULL;
}

static char toUpper(char c)
{
	if (c>='a' && c<='z')
		c-='a' - 'A';
	return c;
}

void cfgSane(void)
{
	if (cfg.gui<0 || cfg.gui>2) {
		printf("Unknown GUI option, using gui=2\n");
		cfg.gui=2;
	}
	if (cfg.cacheSizeMB<10) {
		printf("Cache size too small, using 10MB cache\n");
		cfg.cacheSizeMB=10;
	}
#ifdef _WIN32
	if (!cfg.mountPt || strlen(cfg.mountPt)!=2) {
		char *tmp=malloc(3);
		tmp[0]=cfg.mountPt ? cfg.mountPt[0] : 'Y';
		tmp[2]='\0';
		free(cfg.mountPt);
		cfg.mountPt=tmp;
	}
	cfg.mountPt[0]=toUpper(cfg.mountPt[0]);
	cfg.mountPt[1]=':';
	if (cfg.mountPt[0]<'A' || cfg.mountPt[0]>'Z') {
		printf("Incorrect drive letter specified, trying 'Y:'\n");
		cfg.mountPt[0]='Y';
	}
#else
	if (!cfg.mountPt || !cfg.mountPt[0])
		cfg.mountPt=strdup("/mnt");
#endif
}

int cfgParseOpt(const char *opt)
{
	if (!strncmp(opt, "repo=", 5)) {
		return cfgAddRepo(opt + 5);
	} else if (!strncmp(opt, "mount=", 6)) {
		const char *mountPt=opt + 6;
#ifdef _WIN32
		int letter=toUpper(mountPt[0]);
		if (letter<'A' || letter>'Z' || mountPt[1] && mountPt[2] || mountPt[1] && mountPt[1]!=':')
			printf("Incorrect drive format. Use L or L:, where L is a drive letter\n");
#endif
		free(cfg.mountPt);
		cfg.mountPt=strdup(mountPt);
		return 0;
	} else if (!strncmp(opt, "gui=", 4)) {
		sscanf(opt + 4, "%d", &cfg.gui);
		return 0;
	} else if (!strncmp(opt, "cache=", 6)) {
		sscanf(opt + 6, "%d", &cfg.cacheSizeMB);
		return 0;
	} else if (!strncmp(opt, "treeish=", 8)) {
		strcpy(treeish, opt + 8);
		if (strlen(treeish) && mappathValid) {
			int res=cfgAddMnt(treeish, mappath, submodules);
			treeish[0]='\0';
			mappathValid=0;
			return res;
		}
		return 0;
	} else if (!strncmp(opt, "path=", 5)) {
		strcpy(mappath, opt + 5);
		mappathValid=1;
		if (strlen(treeish) && mappathValid) {
			int res=cfgAddMnt(treeish, mappath, submodules);
			treeish[0]='\0';
			mappathValid=0;
			return res;
		}
		return 0;
	} else if (!strncmp(opt, "cfg=", 4)) {
		return cfgLoad(opt + 4, 1);
	} else if (!strncmp(opt, "submodules=", 11)) {
		const char *subm=opt + 11;
		submodules=0;
		if (!strcmp(subm, "y"))
			submodules=1;
		else if (!strcmp(subm, "r"))
			submodules=2;
		return 0;
	} else if (!strncmp(opt, "daemonize=", 10)) {
		sscanf(opt + 10, "%d", &cfg.daemon);
		return 0;
	}
	printf("Unrecognized option '%s'\n", opt);
	return -1;
}

int cfgAddRepo(const char *repo)
{
	if (lastRepo && !strcmp(repo, lastRepo->repo))
		return 0;
	struct cfgRepo *r=malloc(sizeof(*r));
	r->repo=strdup(repo);
	r->firstMnt=r->lastMnt=NULL;
	r->next=NULL;
	if (lastRepo)
		lastRepo->next=r;
	lastRepo=r;
	if (!firstRepo)
		firstRepo=r;
	return 0;
}

int cfgAddMnt(const char *treeish, const char *mapto, int submodules)
{
	if (!lastRepo) {
		printf("No repo specified, not mapping '%s' to '%s'\n", treeish, mapto);
		return -1;
	}
	struct cfgMnt *mnt=malloc(sizeof(*mnt));
	mnt->treeish=strdup(treeish);
	mnt->mapto=strdup(mapto);
	mnt->submodules=submodules;
	mnt->next=NULL;
	if (lastRepo->lastMnt)
		lastRepo->lastMnt->next=mnt;
	lastRepo->lastMnt=mnt;
	if (!lastRepo->firstMnt)
		lastRepo->firstMnt=mnt;
	return 0;
}

struct cfgRepo *currentRepo;
struct cfgMnt *currentMnt;

void cfgEnumReset(void)
{
	currentRepo=firstRepo;
	currentMnt=NULL;
}

void cfgEnumNextMount(const char **repo, const char **treeish, const char **mapto, int *submodules)
{
	if (!currentRepo)
		currentMnt=NULL;
	*repo=NULL;
	*treeish=NULL;
	*mapto=NULL;
	*submodules=0;
	if (currentMnt) {
		currentMnt=currentMnt->next;
		if (!currentMnt)
			currentRepo=currentRepo->next;
	}
	while (!currentMnt && currentRepo) {
		currentMnt=currentRepo->firstMnt;
		if (!currentMnt)
			currentRepo=currentRepo->next;
	}
	// assert (currentMnt && currentRepo || !currentMnt && !currentRepo)
	if (currentMnt) {
		*repo=currentRepo->repo;
		*treeish=currentMnt->treeish;
		*mapto=currentMnt->mapto;
		*submodules=currentMnt->submodules;
	}
}

int cfgSave(const char *file)
{
	cfgSane();
	FILE *f=fopen(file, "wt");
	if (!f) {
		printf("Could not save configuration file '%s'\n", file);
		return -1;
	}
	fprintf(f, "gui=%d\n", cfg.gui);
	fprintf(f, "mount=%s\n", cfg.mountPt);
	fprintf(f, "cache=%d\n", cfg.cacheSizeMB);
	fprintf(f, "daemonize=%d\n", cfg.daemon);
	cfgEnumReset();
	const char *prevRepo=NULL;
	int prevSubmod=0;
	while (1) {
		const char *repo;
		const char *treeish;
		const char *mapto;
		int submodules;
		cfgEnumNextMount(&repo, &treeish, &mapto, &submodules);
		if (!repo)
			break;
		if (submodules!=prevSubmod) {
			prevSubmod=submodules;
			char subc='n';
			if (submodules==1)
				subc='y';
			if (submodules==2)
				subc='r';
			fprintf(f, "submodules=%c\n", subc);
		}
		if (!prevRepo || strcmp(prevRepo, repo)) {
			prevRepo=repo;
			fprintf(f, "repo=%s\n", repo);
		}
		if (treeish && mapto) {
			fprintf(f, "treeish=%s\n", treeish);
			fprintf(f, "path=%s\n", mapto);
		}
	}
	int res=ferror(f) ? -1 : 0;
	if (res)
		printf("Error saving configuration file'%s'\n", file);
	fclose(f);
	return res;
}


int cfgLoad(const char *file, int append)
{
	if (!append)
		cfgReset();
	FILE *f=fopen(file, "rt");
	if (!f) {
		printf("Error opening configuration file '%s'\n", file);
		return -1;
	}
	char buf[2048];
	buf[2047]=0;
	while (fgets(buf, 2047, f)) {
		size_t n=strlen(buf);
		for (size_t i=0; i<n; i++)
			if (buf[i]<32 && buf[i]>=0)
				buf[i]='\0';
		cfgParseOpt(buf);
	}
	int res=ferror(f) ? -1 : 0;
	if (res)
		printf("Error reading configuration file '%s'\n", file);
	fclose(f);
	cfgSane();
	return res;
}
