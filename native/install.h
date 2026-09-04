#ifndef INSTALL_H
#define INSTALL_H
#include <stddef.h>
int lc_make_shortcut(const char *target, const char *args, const char *workdir, const char *desc, const char *linkPath);
void lc_install_dir(char *out, size_t n);
int lc_is_installed(void);
int lc_install(const char *srcExe, int startup, int screensaver, int startmenu);
int lc_uninstall(void);
int lc_setup_gui(const char *exePath, const char *exeDir);
#endif
