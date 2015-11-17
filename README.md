# git-dokany

git-dokany implements git filesystem that enables read only access to git version controlled files.  
git-dokany uses the [Dokany](https://dokan-dev.github.io/) user mode file system library ([github page](https://github.com/dokan-dev/dokany)).  
Git repository access is available using [libgit2](https://libgit2.github.com/) and its dependency libraries.

![alttext](screenshot.png)



## Prerequisites

[Install](https://github.com/dokan-dev/dokany/wiki/Installation) the Dokany filesystem driver, 64-bit version, 0.8.0 or later.  
The user mode library is not needed.


## Installing

Just download and run git-dokany.exe.  
Type git-dokany.exe --help to display the command line options. Or just double click to start in GUI mode.
