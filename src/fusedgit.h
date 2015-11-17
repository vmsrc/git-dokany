typedef struct fusedgit *fusedgit_t;
typedef struct fusedgit_tmp_repo *fusedgit_repo_t;

fusedgit_t fusedgit_create(int threads, unsigned long long cacheSize);
void fusedgit_destroy(fusedgit_t fgit);

fusedgit_repo_t fusedgit_addrepo(fusedgit_t fusedgit, const char *repoPath);
int fusedgit_addtree(fusedgit_repo_t repo, const char *treeish, const char *path);
void fusedgit_releaserepo(fusedgit_repo_t repo);

int fusedgit_mount(fusedgit_t fusedgit, const char *mountpoint);

int fusedgit_umount(fusedgit_t fusedgit);
