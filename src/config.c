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
};

struct cfgRepo
{
	char *repo;
	struct cfgMnt *firstMnt, *lastMnt;
	struct cfgRepo *next;
};

static struct cfgRepo *firstRepo, *lastRepo;

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
	memset(&cfg, 0, sizeof(cfg));
	cfg.gui=1;
	cfg.cacheSizeMB=200;
	strcpy(cfg.drive, "Y");
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
	if (toUpper(cfg.drive[0])<'A' || toUpper(cfg.drive[0]>'Z')) {
		printf("Incorrect drive letter specified, trying 'Z:'\n");
		cfg.drive[0]='Z';
		cfg.drive[1]=':';
		cfg.drive[2]='\0';
	}
	cfg.drive[1]=':';
	cfg.drive[2]='\0';
}

int cfgParseOpt(const char *opt)
{
	if (!strncmp(opt, "repo=", 5)) {
		return cfgAddRepo(opt + 5);
	} else if (!strncmp(opt, "drive=", 6)) {
		const char *drive=opt + 6;
		if (!drive[0] || drive[1] && drive[2] || drive[1] && drive[1]!=':') {
			printf("Incorrect drive format. Use L or L:, where L is a drive letter\n");
		}
		cfg.drive[0]=drive[0];
		cfg.drive[1]=':';
		cfg.drive[2]='\0';
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
			int res=cfgAddMnt(treeish, mappath);
			treeish[0]='\0';
			mappathValid=0;
			return res;
		}
		return 0;
	} else if (!strncmp(opt, "path=", 5)) {
		strcpy(mappath, opt + 5);
		mappathValid=1;
		if (strlen(treeish) && mappathValid) {
			int res=cfgAddMnt(treeish, mappath);
			treeish[0]='\0';
			mappathValid=0;
			return res;
		}
		return 0;
	} else if (!strncmp(opt, "cfg=", 4)) {
		return cfgLoad(opt + 4, 1);
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

int cfgAddMnt(const char *treeish, const char *mapto)
{
	if (!lastRepo) {
		printf("No repo specified, not mapping '%s' to '%s'\n", treeish, mapto);
		return -1;
	}
	struct cfgMnt *mnt=malloc(sizeof(*mnt));
	mnt->treeish=strdup(treeish);
	mnt->mapto=strdup(mapto);
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

void cfgEnumNextMount(const char **repo, const char **treeish, const char **mapto)
{
	if (!currentRepo)
		currentMnt=NULL;
	*repo=NULL;
	*treeish=NULL;
	*mapto=NULL;
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
	}
}

int cfgSave(const char *file)
{
	FILE *f=fopen(file, "wt");
	if (!f) {
		printf("Could not save configuration file '%s'\n", file);
		return -1;
	}
	fprintf(f, "gui=%d\n", cfg.gui);
	fprintf(f, "drive=%c:\n", cfg.drive[0]);
	fprintf(f, "cache=%d\n", cfg.cacheSizeMB);
	cfgEnumReset();
	const char *repo, *prevRepo=NULL;
	const char *treeish;
	const char *mapto;
	while (1) {
		cfgEnumNextMount(&repo, &treeish, &mapto);
		if (!repo)
			break;
		if (!prevRepo || strcmp(prevRepo, repo)) {
			prevRepo=repo;
			fprintf(f, "repo=%s\n", repo);
		}
		if (treeish && mapto) {
			fprintf(f, "treeish=%s\n", treeish);
			fprintf(f, "path=%s\n", mapto);
		}
	}
	int res=ferror(f) ? 0 : -1;
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
