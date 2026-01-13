#include "logger.h"
#include "shared_state.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

void* loggerThread(void *arg){
    SharedGameState *gameState = (SharedGameState*) arg;
    
    //open game.log fileS *gamegameState = 
    FILE *logFile = fopen("game.log", "a");
    if(logFile == NULL){
        perror("Failed to open log file");
        return NULL;
    }
    
    printf("Logger thread started: TID=%lu\n", pthread_self());

    sem_post(&gameState->logReadySemaphore);//Signal that logger is ready

    while(serverRunning){
        sem_wait(&gameState->logItemsSemaphore);//Wait for a log event in the log queue
        int logQueueLength =
            (gameState->logQueueTail - gameState->logQueueHead + LOG_QUEUE_SIZE)
            % LOG_QUEUE_SIZE;

        pthread_mutex_lock(&gameState->logQueueMutex);//Lock the shared gameState to read log info
        LogEvent logEvent = gameState->logQueue[gameState->logQueueHead];//Get the log event from the front of the queue
        gameState->logQueueHead = (gameState->logQueueHead + 1) % LOG_QUEUE_SIZE;//Move the head forward so the current event is removed from the queue
        pthread_mutex_unlock(&gameState->logQueueMutex);//Unlock the log queue

        time_t now = time(0);
        char timeString[32];
        snprintf(timeString, sizeof(timeString), "%s", ctime(&now));
        timeString[strcspn(timeString,"\n")] = '\0';

        switch(logEvent.type){
            case LOG_SERVER:
                fprintf(logFile, "[%s][SERVER] %s",timeString, logEvent.message);
                break;
            case LOG_PLAYER:
                fprintf(logFile, "[%s][PLAYER] %s",timeString,logEvent.message);
                break;
            case LOG_GAME:
                fprintf(logFile, "[%s][GAME] %s",timeString,logEvent.message);
                break;
            case LOG_TURN:
                fprintf(logFile, "[%s][TURN] %s",timeString,logEvent.message);
                break;
            case LOG_NONE:
                //No log to process
                break;
            default:
                fprintf(logFile, "[%s][UNKNOWN] %s",timeString,logEvent.message);
                break;
        }

        fflush(logFile);//Ensure the message is written immediately
        sem_post(&gameState->logSpacesSemaphore);//Signal that there is space in the log queue
    }

    fprintf(logFile,"[SERVER] Server shutdown.");
    fclose(logFile);
    return NULL;
}

void pushLogEvent(SharedGameState *gameState, LogType type, const char *message){
    sem_wait(&gameState->logSpacesSemaphore);//Wait for space in the log queue
    pthread_mutex_lock(&gameState->logQueueMutex);//Lock the log queue
    LogEvent *event = &gameState->logQueue[gameState->logQueueTail];//Push the log event from back of the queue
    event->type = type;
    strncpy(event->message, message, LOG_MSG_LENGTH);
    gameState->logQueueTail = (gameState->logQueueTail + 1) % LOG_QUEUE_SIZE;//Move the tail forward so the current event becomes the next to be written
    pthread_mutex_unlock(&gameState->logQueueMutex);//Unlock the log queue
    printf("Pushed log event: [%d] %s\n", type, message);
    sem_post(&gameState->logItemsSemaphore);//Signal that there is a new log item
}