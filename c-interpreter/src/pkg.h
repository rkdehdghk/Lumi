/* pkg.h — Lumi 외부 패키지 매니저 (lumipm) 헤더 */
#ifndef LUMI_PKG_H
#define LUMI_PKG_H

#include "lumi.h"

int run_pkg_manager(int argc, char **argv);
char *get_package_entry(const char *dir, const char *pkg_name);

#endif /* LUMI_PKG_H */
