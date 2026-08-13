/* whoami.c — KratosOS Native Whoami Utility (/usr/bin/whoami)
 *
 * Usage:
 *   whoami
 *
 * Prints the effective username of the current user.
 */

#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    uid_t euid = geteuid();
    struct passwd *pw = getpwuid(euid);
    if (pw) {
        puts(pw->pw_name);
        return 0;
    }
    fprintf(stderr, "whoami: cannot find name for user ID %u\n", euid);
    return 1;
}
