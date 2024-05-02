
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

extern int EditorMain(int argc, char** argv);
extern int LsMain(int argc, char** argv);

int main(void) {
    /*
    pid_t child = fork();
    if (child == 0) {
        execve("sys:/shell.exe", ...);
        return -1;
    }c
    
    while (true) {
        wait(0);
    }
    */

    chdir("drv0:/");
    
    char line[300];
    char cwd_buffer[300];
    while (true) {
        getcwd(cwd_buffer, 299);
        printf("%s>", cwd_buffer);
        fflush(stdout);
        fgets(line, 299, stdin);

        errno = 0;

        if (!strcmp(line, "fork\n")) {
            printf("About to fork()...\n");
            pid_t ret = fork();
            printf("Fork returned %d!\n", ret);
            while (true) {
                ;
            }
        
        } else if (!strcmp(line, "clear\n")) {
            printf("\x1B[2J");
            fflush(stdout);

        } else if (!strcmp(line, "ed\n")) {
            EditorMain(1, NULL);

        } else if (!strcmp(line, "ls\n") || !strcmp(line, "dir\n")) {
            LsMain(1, NULL);

        } else if (line[0] == 'c' && line[1] == 'd' && line[2] == ' ') {
            line[strlen(line) - 1] = 0;
            int res = chdir(line + 3);
            if (res == -1) {
                printf("%s\n\n", strerror(errno));
            }

        } else {
            printf("Command not found: %s\n", line);
        }
    }

    return 0;
}