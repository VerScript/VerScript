#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opcodes.h"

// Dynamic Thread Structure
typedef struct {
    char threadID[16];
    int pc;
    int active;
} Thread;

// Pointer to our dynamic pool
Thread *threadPool = NULL;
int threadCount = 0;

void spawnThread(const char* id) {
    threadPool = realloc(threadPool, (threadCount + 1) * sizeof(Thread));
    strcpy(threadPool[threadCount].threadID, id);
    threadPool[threadCount].pc = 0;
    threadPool[threadCount].active = 1;
    threadCount++;
    printf("Thread %s spawned.\n", id);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./verscript <file.vrs>\n");
        return 1;
    }

    // Initial thread allocation
    spawnThread("MAIN_THREAD");

    // File loading logic
    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Failed to open input file");
        return 1;
    }

    // Logic placeholder for parsing 'display'
    printf("Terminal-only mode active. Awaiting opcodes.\n");

    fclose(file);
    free(threadPool);
    return 0;
}
