#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>

#define MIN_PLAYERS 2
#define MAX_PLAYERS 4
#define MAX_CARDS 24
#define LOG_MSG_LENGTH 256
#define LOG_QUEUE_SIZE 50

extern volatile bool serverRunning;

//Card
typedef struct{
    int cardID;
    int faceValue;
    bool isFlipped;
    bool isMatched;
}Card;

//Player
typedef struct{
    int playerID;
    int socket;
    int score;
    pid_t pid;
    bool connected;
    bool wantToJoin;
    bool readyToStart;
    bool pendingAction;
    int pendingCardIndex;
    int firstFlipIndex;
    int secondFlipIndex;
}Player;

// Persistent Score Entry
typedef struct {
    int playerID;
    int wins;
} ScoreEntry;

// Score Table
typedef struct {
    ScoreEntry entries[MAX_PLAYERS];
    int count;
    pthread_mutex_t scoreMutex;
} scoreBoard;

//Log Event Type
typedef enum{
    LOG_NONE,
    LOG_SERVER,//Server Start/Stop
    LOG_PLAYER,//Player Join/Leave
    LOG_GAME,//Game Start/Reset/End Card Flip/Match/Mismatch
    LOG_TURN,//Scheduler Turn Change
}LogType;

typedef struct{
    LogType type;
    char message[LOG_MSG_LENGTH];
}LogEvent;

//Shared Game State
typedef struct{
    pthread_mutex_t mutex;
    sem_t turnSemaphore;
    sem_t turnCompleteSemaphore;
    sem_t logReadySemaphore;

    LogEvent logQueue[LOG_QUEUE_SIZE];
    int logQueueHead;
    int logQueueTail;
    pthread_mutex_t logQueueMutex;
    sem_t logItemsSemaphore;//Use to signal there is items in the log queue
    sem_t logSpacesSemaphore;//Use to signal there is space in the log 
    
    bool gameStarted;
    bool boardNeedsBroadcast;
    int playerCount;
    int currentTurn;
    int boardRows;
    int boardCols;
    int totalPairs;
    int matchedPaires;
    Player players[MAX_PLAYERS];
    Card cards[MAX_CARDS];
    scoreBoard scoreBoard;
}SharedGameState;

typedef enum{
    ACTION_FLIP,
    ACTION_JOIN,
    ACTION_READY,
    ACTION_QUIT
}PlayerActionType;

typedef struct{
    PlayerActionType type;
    int cardIndex;
    int playerID;
}PlayerAction;

typedef struct{
    int currentPlayerID;
    int cardIndex;
    int totalPlayers;
}PlayerTurn;

void initGameState(SharedGameState *state);
void resetGameState(SharedGameState *state);
void printGameState(SharedGameState *state);
void setupBoard(SharedGameState *state, int rows, int cols);
void formatOfBoard(SharedGameState *state, char *buffer, size_t bufsize);
void sendBoardStateToAll(SharedGameState *state);

#endif
