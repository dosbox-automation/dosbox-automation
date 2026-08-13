// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_SHELL_BANNER_H
#define DOSBOX_SHELL_BANNER_H

#include "dosbox.h"

// Compact two-line startup banner, replaces the old welcome box.
// Shared between the shell message registration and the width tests;
// every rendered line must stay within ShellBannerMaxColumns
// (tests/shell_banner_tests.cpp enforces this, translations included).

constexpr int ShellBannerMaxColumns = 78;

// Format arguments: detailed version string, webserver status tag
// [color=white] sets the ANSI bright attribute which [color=light-gray]
// alone does not clear; every bright-to-dim transition needs a [reset].
constexpr char ShellBannerFormat[] =
        "[color=yellow]" DOSBOX_PROJECT_NAME
        " [color=white]%s[reset] %s\n"
        "[color=white]https://www.dosbox-automation.org[reset][color=light-gray]"
        " - command list: [color=white]HELP[reset][color=light-gray],"
        " instructions: [color=white]MANUAL[reset]\n"
        "\n";

constexpr char ShellBannerWebserverEnabledTag[] = "[color=light-green][webserver enabled][reset]";

constexpr char ShellBannerWebserverDisabledTag[] = "[color=dark-gray][webserver disabled][reset]";

#endif // DOSBOX_SHELL_BANNER_H
