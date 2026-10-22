/* update_install.h where the port cannot update itself (all but Windows): Download opens the
 * release's page instead. */
#include "update_install.h"

#include <stdio.h>

int khdays_update_install_possible(void)
{
    return 0;
}

int khdays_update_install_folder(char *out, size_t size)
{
    (void)out;
    (void)size;
    return 0;
}

void khdays_update_install_clean(void)
{
}

int khdays_update_install_run(const char *file, char *why, size_t size)
{
    (void)file;
    snprintf(why, size, "this system's copy does not update itself");
    return 0;
}
