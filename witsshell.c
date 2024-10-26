#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>

char *paths[32] = {"/bin/", "/usr/bin/", NULL};
int pathCount = 2;

int pipefd[2]; 
char buffer[1024];

void ErrorMessage(){
	char error_message[30] = "An error has occurred\n";
	write(STDERR_FILENO, error_message, strlen(error_message));
}


char* Control(char *line, char delimiter) {
    int len = strlen(line);
    int new_len = len;
    int i, j;

    // First pass: Count how much space is needed
    for (i = 0; i < len; i++) {
        if (line[i] == delimiter) {
            // Check if the delimiter already has spaces around it
            if ((i > 0 && line[i-1] != ' ') || (i < len-1 && line[i+1] != ' ')) {
                new_len += 2;  // One for space before and one for space after
            }
        }
    }

    // Allocate memory for the new string
    char *new_line = (char*)malloc(new_len + 1);  // +1 for null terminator
    if (new_line == NULL) {
        ErrorMessage("Memory allocation failed\n");
        exit(1);
    }

    // Second pass: Build the new string with spaces around the delimiter
    j = 0;
    for (i = 0; i < len; i++) {
        if (line[i] == delimiter) {
            // Only add spaces if they don't already exist
            if (i > 0 && line[i-1] != ' ') {
                new_line[j++] = ' ';
            }
            new_line[j++] = delimiter;
            if (i < len-1 && line[i+1] != ' ') {
                new_line[j++] = ' ';
            }
        } else {
            new_line[j++] = line[i];
        }
    }
    
    new_line[j] = '\0';  // Null-terminate the new string

    return new_line;
}

void printargs(char **arr, int i){
	// Print argument array
        printf("Argument array:\n");
        for (int j = 0; j<i+1; j++) {
            printf("arr[%d] = '%s'\n", j, arr[j]);
        }
}

void cd_command(char **args, int arg_count) {
    if (arg_count > 2) {  // More than one argument is invalid for 'cd'
        ErrorMessage();
        return;
    }

    const char *dir = (arg_count == 1) ? getenv("HOME") : args[1];  // Default to HOME if no args
    if (chdir(dir) != 0) {  // Attempt to change the directory
        ErrorMessage();
    }
}



void freeing(char **args) {
    int i = 0;
    while (args[i] != NULL) {
        free(args[i]);
        i++;
    }
    free(args);
}

void freepaths() {
    for (int i = 0; i < pathCount; i++) {
        free(paths[i]);
    }
    pathCount = 0;
}



int getPathCount(char *paths[]){
    int count = 0;
    for(int i = 0; paths[i] != NULL; i++)
        count++;

    return count;
}


//right here
void setPath(char **newPaths){
    if(newPaths[1] == NULL){
        paths[0] = NULL;
        pathCount = 0;
        return;
    }

    int i = 1;
    for(i = 1; newPaths[i] !=  NULL; i++)
        paths[i-1] = newPaths[i];
    paths[i] = NULL;
    pathCount = getPathCount(paths);
}

void execute_command(char **args, int detect_redirect, char *filename, bool background) {
    pid_t pid;
    int status;

    // Path handling
    bool foundPath = false;
    for (int i = 0; i < pathCount; i++) {
        char *cmdPath = malloc(strlen(paths[i]) + strlen(args[0]) + 2);
        strcpy(cmdPath, paths[i]);
        strcat(cmdPath, args[0]);
        if (access(cmdPath, X_OK) == 0) {
            args[0] = cmdPath;
            foundPath = true;
            break;
        }
        free(cmdPath);
    }

    if (!foundPath) {
        ErrorMessage();
        return;
    }

    pid = fork();
    if (pid == 0) {
        // Child process
        if (detect_redirect) {
            int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                ErrorMessage();
                exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1) {
                ErrorMessage();
                exit(1);
            }
            close(fd);
        }

        // Execute the command
        if (execv(args[0], args) == -1) {
            ErrorMessage();
            exit(1);
        }
    } else if (pid < 0) {
        ErrorMessage();
    } else {
        if (!background) {
            // Wait for the foreground process to finish
            waitpid(pid, &status, 0);
        } else {
           
        }
    }
}


char *command_line = NULL;
void handle_input(char *line) {
    int s = 64;
    int i = 0;
    char **args = malloc(s * sizeof(char*));
    char *token;
    char *redirect_file = NULL;
    int detect_redirect = 0;
    bool background = false;

    // Handle redirection and piping
    line = Control(line, '|');
    line = Control(line, '>');

    // Check for background process
    char *ampersand = strchr(line, '&');
    if (ampersand != NULL) {
        background = true;
        *ampersand = '\0';  // Remove & from the command
    }

    // Split the line into commands
    char *cmd = strtok(line, "&");
    while (cmd != NULL) {
        command_line = strdup(cmd);
        if (command_line == NULL) {
            ErrorMessage();
            exit(1);
        }

        // Tokenize the command line
        char *temp_line = command_line;
        while ((token = strsep(&temp_line, " \t\n")) != NULL) {
            if (*token == '\0') {
                continue;
            }

            if (strcmp(token, ">") == 0) {
                if (detect_redirect) {
                    ErrorMessage();
                    free(command_line);
                    freeing(args);
                    return;
                }
                detect_redirect = 1;

                token = strsep(&temp_line, " \t\n");
                if (token == NULL || *token == '\0') {
                    ErrorMessage();
                    free(command_line);
                    freeing(args);
                    return;
                }

                redirect_file = strdup(token);
                continue;
            }

            if (detect_redirect && redirect_file != NULL && token != NULL) {
                ErrorMessage();
                free(command_line);
                return;
            }

            if (i >= s) {
                s *= 2;
                args = realloc(args, s * sizeof(char*));
                if (args == NULL) {
                    ErrorMessage();
                    exit(1);
                }
            }

            args[i] = strdup(token);
            if (args[i] == NULL) {
                ErrorMessage();
                exit(1);
            }
            i++;
        }

        args[i] = NULL;

        if (args[0] != NULL) {
            if (strcmp(args[0], "exit") == 0) {
                if (i > 1) {
                    ErrorMessage();
                } else {
                    free(args);
                    exit(0);
                }
            } else if (strcmp(args[0], "cd") == 0) {
                cd_command(args, i);
            } else if (strcmp(args[0], "path") == 0) {
                setPath(args);
            } else {
                execute_command(args, detect_redirect, redirect_file, background);
            }
        }

        free(command_line);
        detect_redirect = 0;
        redirect_file = NULL;
        free(args);
        args = malloc(s * sizeof(char*));  // Reset args array
        if (args == NULL) {
            ErrorMessage();
            exit(1);
        }

        cmd = strtok(NULL, "&");
    }

    free(args);
}


// void wait_for_children() {
//     int status;
//     while (wait(&status) > 0) {
//         // Waiting for all child processes
//     }
// }


void BatchMode(char *filename){
    char *line = NULL;
    size_t len =0;
    FILE *file =  fopen(filename, "r");

        if (file == NULL) {
			 ErrorMessage();
			 exit(1);
        }

    while (getline(&line ,&len , file) != -1) {
        handle_input(line);
        // wait_for_children();
    }

    free(line);
    fclose(file);

}

void interact(){
    
    char *line= NULL;
    size_t len = 0;

     while (1) {
       printf("witsshell> ");
        if (getline(&line, &len, stdin) == -1) {
         // Handle EOF
         printf("\n");
         free(line);
         exit(0);
        }
        handle_input(line);
        // wait_for_children();
    }

}


int main(int MainArgc, char *MainArgv[]){

    if(MainArgc == 2){
        BatchMode(MainArgv[1]);
    }else if(MainArgc == 1) {
        interact();
    }else{
        ErrorMessage();
        exit(1);
    }

    return 0;

}
