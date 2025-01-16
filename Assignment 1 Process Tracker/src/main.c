#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void check_error_direcrtory(DIR* value, const char* successMessage, const char* errorMessage) {
    if (value != NULL) {
        printf("%s", successMessage);
        return;
    }
    int error = errno;
    perror(errorMessage);
    exit(error);
}

void check_error_entry(struct dirent* value, const char* successMessage, const char* errorMessage) {
    if (value != NULL) {
        printf("%s", successMessage);
        return;
    }
    int error = errno;
    perror(errorMessage);
    exit(error);
}

void check_error_int(int value, const char* successMessage, const char* errorMessage) {
    if (value != -1) {
        printf("%s", successMessage);
        return;
    }
    int error = errno;
    perror(errorMessage);
    exit(error);
}

int main() {
    //Set directory variables
    DIR *dir;
    struct dirent *dir_entry;
    char path[256];

    // Print header:
    printf("  PID CMD\n");

    // Open directory and check if error
    dir = opendir("/proc");
    check_error_direcrtory(dir, "", "Proc didn't open\n");

    // Open entry and check if error
    dir_entry = readdir(dir);
    check_error_entry(dir_entry, "", "Entry not opened\n");
    
    // Go to first numbered directory
    while ((dir_entry = readdir(dir)) != NULL) {
        //printf("%s\n", dir_entry->d_name);
        if (!strcmp(dir_entry->d_name, "1")) {
            break;
        }
    }

    // String variables
    char buffer[4096];

    do {
        // Print PID:
        int PID = atoi(dir_entry->d_name);
        if (PID < 10) {
            printf(" ");
        } 
        if (PID < 100) {
            printf(" ");
        }
        if (PID < 1000) {
            printf(" ");
        }
        if (PID < 10000) {
            printf(" ");
        }
        printf("%d ", PID);

        // Set path
        path[0] = '\0';
        strcat(path, "/proc/");
        strcat(path,dir_entry->d_name);
        strcat(path,"/status");

        // Open path
        int fd = open(path, O_RDONLY);
        check_error_int(fd, "", "File Not Opened");

        // Get name
        ssize_t bytes_read;
        path[0] = '\0';
        buffer[0] = '\0';
        bytes_read = read(fd, buffer, sizeof(buffer));

        int i = 0;
        while (buffer[i] != '\n') {
            if (i > 5) {
                printf("%c", buffer[i]);
            }
            i++;
        }
        printf("\n");

        close(fd);
    }
    while ((dir_entry = readdir(dir)) != NULL);

    closedir(dir);

    return 0;
}
