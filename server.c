#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "shared_state.h"
#include "scheduler.h"
#include "logger.h"
#include "player.h"

int sharedMemoryID;
SharedGameState *gameState;
volatile bool serverRunning = true;

void cleanup(int sig){
    printf("Server shutting down...\n");

    sem_destroy(&gameState->turnSemaphore);
    sem_destroy(&gameState->turnCompleteSemaphore);
    sem_destroy(&gameState->logReadySemaphore);
    sem_destroy(&gameState->turnCompleteSemaphore);
    pthread_mutex_destroy(&gameState->mutex);

    shmdt(gameState);
    shmctl(sharedMemoryID, IPC_RMID, NULL);
    printf("Cleaned up shared memory and semaphores.\n");

    exit(0);
}

int main(){
    key_t key = ftok("server.c", 65);

    sharedMemoryID = shmget(key, sizeof(SharedGameState), 0666|IPC_CREAT);//Size of - calcute the size needed for shared memory, 0666 - read and write permission, IPC_CREAT - create if not exists
    if(sharedMemoryID == -1){
        perror("Shared Memory get failed");
        exit(1);
    }

    gameState = (SharedGameState*) shmat(sharedMemoryID, NULL, 0);//Connect the program to the shared memory
    if(gameState == (void*) -1){
        perror("Shared Memory attach failed");
        exit(1);
    }

    printf("Initializing system environment...\n");
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);//Prepare the mutex attribute
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);//Allow the mutex to shared between processes
    pthread_mutex_init(&gameState->mutex, &attr);//Create the mutex in shared memory

    pthread_mutex_lock(&gameState->mutex);//Lock the mutex to initialize the game state safely
    initGameState(gameState);
    pthread_mutex_unlock(&gameState->mutex);//Unlock the mutex after initialization

    sem_init(&gameState->turnSemaphore, 1, 0);//Create semaphore in shared memory, 1 - shared between processes, only one player can do things at a time
    sem_init(&gameState->turnCompleteSemaphore, 1, 0);//Create turn complete semaphore in shared memory, 1 - shared between processes, wait until turn is complete

    pthread_mutexattr_t logAttr;
    pthread_mutexattr_init(&logAttr);//Prepare the mutex logAttribute
    pthread_mutexattr_setpshared(&logAttr, PTHREAD_PROCESS_SHARED);//Allow the mutex to shared between processes
    pthread_mutex_init(&gameState->logQueueMutex, &logAttr);//Create the log queue mutex in shared memory
    sem_init(&gameState->logReadySemaphore, 1, 0);//Create log ready semaphore in shared memory, 1 - shared between processes, initial value 0
    sem_init(&gameState->logItemsSemaphore, 1, 0);//Create log items semaphore in shared memory, 1 - shared between processes, initial value 0
    sem_init(&gameState->logSpacesSemaphore, 1, LOG_QUEUE_SIZE);//Create log spaces semaphore in shared memory, 1 - shared between processes, initial value LOG_QUEUE_SIZE
    printf("System environment initialized complete\n");
    pushLogEvent(gameState,LOG_SERVER,"Server environment initialized\n");
    pthread_t loggerThreadID;
    pthread_create(&loggerThreadID, NULL, loggerThread, gameState);
    sem_wait(&gameState->logReadySemaphore);//Wait log thread ready

    printf("Waiting for player to join...\n");
    gameState->players[0].wantToJoin = true;
    gameState->players[0].readyToStart = true;
    gameState->players[1].wantToJoin = true;
    gameState->players[1].readyToStart = true;
    //Waiting for player to join
    while(!gameState->gameStarted){
        printf("gameStarted = %d\n", gameState->gameStarted);printf(&gameState->gameStarted);
        if(gameState->playerCount == MAX_PLAYERS){
            printf("Players are full");
        }else{
            for(int i=0; i < MAX_PLAYERS; i++){
                if(gameState->players[i].wantToJoin && !gameState->players[i].connected){
                    pid_t pid = fork();
                    
                    if(pid == 0){
                        //child
                        handlePlayer(gameState,i);
                        _exit(0);
                    }

                    gameState->players[i].connected = true;
                    gameState->players[i].pid = pid;
                    gameState->players[i].playerID = i;
                    gameState->playerCount++;
                    char playerConnectMessage[LOG_MSG_LENGTH];
                    snprintf(playerConnectMessage, LOG_MSG_LENGTH,"Player %d joined\n", i);
                    pushLogEvent(gameState,LOG_PLAYER,playerConnectMessage);
                }
            }   
        }

        if(allConnectedPlayerReady(gameState)){
            if(gameState->playerCount < 2){
                printf("Minimum player is 2, cannot start\n");
                sleep(2);
                continue;
            }
            gameState->gameStarted = true;
        }

        sleep(2);
    }

    pthread_t schedulerThreadID;
    pthread_create(&schedulerThreadID, NULL, schedulerThread,gameState);
    printf("Scheduler start\n");

    pushLogEvent(gameState,LOG_GAME,"Game Started, Player 0 go first\n");
    sem_post(&gameState->turnSemaphore);

    signal(SIGINT, cleanup);
    while (1) {
        sleep(1);
    }
}

