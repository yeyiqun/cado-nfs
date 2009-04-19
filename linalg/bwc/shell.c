#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE         /* asprintf */
#define _DARWIN_C_SOURCE    /* for asprintf. _ANSI_SOURCE must be undefined */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "parallelizing_info.h"
#include "select_mpi.h"

#include "params.h"
#include "bw-common-mpi.h"
#include "filenames.h"

int command_argc;
char ** command_argv;

void * shell_prog(parallelizing_info_ptr pi, void * arg MAYBE_UNUSED)
{
    char ** argv = malloc((command_argc + 5) * sizeof(char *));
    int i;
    size_t len = 1;
    for(i = 0 ; i < command_argc ; i++) {
        argv[i] = command_argv[i];
        len += 1 + strlen(argv[i]);
    }
    asprintf(&(argv[i]), "%d", pi->wr[0]->jrank);
    len += 1 + strlen(argv[i]);
    i++;
    asprintf(&(argv[i]), "%d", pi->wr[1]->jrank);
    len += 1 + strlen(argv[i]);
    i++;
    asprintf(&(argv[i]), "%d", pi->wr[0]->trank);
    len += 1 + strlen(argv[i]);
    i++;
    asprintf(&(argv[i]), "%d", pi->wr[1]->trank);
    len += 1 + strlen(argv[i]);
    i++;
    argv[i++]=NULL;

    char * cmdline = malloc(len);
    size_t k = 0;
    for(i = 0 ; i < command_argc + 4 ; i++) {
        snprintf(cmdline + k, len-k, " %s", argv[i]);
        k += 1 + strlen(argv[i]);
    }

    pid_t child = fork();

    if (child == 0) {
        execvp(argv[0], argv);
    } else {
        int status;
        wait(&status);
        if (WIFEXITED(status)) {
            int rc;
            if ((rc = WEXITSTATUS(status)) != 0) {
                fprintf("Command%s exited with status %d\n", cmdline, rc);
            }
        } else if (WIFSIGNALED(status)) {
            int sig;
            fprintf("Command%s exited with signal %d\n", cmdline, WTERMSIG(status));
        } else {
            fprintf("Command%s fooleed wait() !\n", cmdline);
        }
    }
    serialize(pi->m);
    for(i = 0 ; i < command_argc + 4 ; i++) {
        free(argv[i]);
    }
    free(argv);
    free(cmdline);
    return NULL;
}

void usage()
{
    fprintf(stderr, "Usage: ./shell <options> -- <shell command>\n");
    fprintf(stderr, "Relevant options: wdir cfg mpi thr\n");
    fprintf(stderr, "Executes <shell command> plus row and column indices\n");
    exit(1);
}

int main(int argc, char * argv[])
{
    param_list pl;
    param_list_init(pl);
    param_list_add_key(pl, "mn", "0", PARAMETER_FROM_FILE);
    bw_common_init_mpi(bw, pl, &argc, &argv);
    // if (param_list_warn_unused(pl)) usage();
    param_list_clear(pl);

    command_argc = argc;
    command_argv = argv;

    pi_go(shell_prog, bw->mpi_split[0], bw->mpi_split[1], bw->thr_split[0], bw->thr_split[1], 0);

    bw_common_clear_mpi(bw);
    return 0;
}

