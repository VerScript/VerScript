#include <stdio.h>
#include <stdlib.h>
#include "opcodes.h"

// Define the Thread Pool size as discussed
#define MAX_THREADS 32

typedef struct {
    char threadID[16];
    int pc;
    int status;
} Thread;

Thread threadPool[MAX_THREADS];

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./verscript <file.vrs>\n");
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open input file");
        return 1;
    }

    printf("VerScript Compiler: Loading %s...\n", argv[1]);

    // Initialize Thread Pool
    for (int i = 0; i < MAX_THREADS; i++) {
        threadPool[i].status = 0; // 0 = Dead
        threadPool[i].pc = 0;
    }

    // Read file into buffer
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (buffer) {
        fread(buffer, 1, length, file);
        buffer[length] = '\0';
    }
    fclose(file);

    printf("System initialized. Loaded %ld bytes.\n", length);

    // Free memory
    free(buffer);
    return 0;
}
