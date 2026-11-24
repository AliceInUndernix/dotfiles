#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kspkan.h"

/* The one place where the network is touched.  It runs curl.
   Returns 0 on success.  quiet=0 lets curl draw a progress bar. */
int fetch_url(const char *url, const char *out, int quiet)
{
    char *argv[24];
    pid_t pid;
    int status, n, rc;

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    {
        return -1;
    }
    n = 0;
    argv[n] = "curl";
    n++;
    argv[n] = "-f";
    n++;
    argv[n] = "-L";
    n++;
    argv[n] = quiet ? "-sS" : "-#";
    n++;
    argv[n] = "--max-redirs";
    n++;
    argv[n] = "10";
    n++;
    argv[n] = "--connect-timeout";
    n++;
    argv[n] = "30";
    n++;
    argv[n] = "--proto";
    n++;
    argv[n] = "=http,https";
    n++;
    argv[n] = "--proto-redir";
    n++;
    argv[n] = "=http,https";
    n++;
    argv[n] = "-A";
    n++;
    argv[n] = "kspkan";
    n++;
    argv[n] = "-o";
    n++;
    argv[n] = (char *)out;
    n++;
    argv[n] = "--url";
    n++;
    argv[n] = (char *)url;
    n++;
    argv[n] = NULL;
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp("curl", argv);
        _exit(127);
    }
    rc = (int)waitpid(pid, &status, 0);
    if (rc < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(out);
        return -1;
    }
    return 0;
}
