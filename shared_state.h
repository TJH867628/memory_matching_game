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
#define PLAYER_NAME_LENGTH 32

extern volatile bool serverRunning;

/* --- Sub-Structures --- */

typedef struct {
    int cardID;
    int faceValue;
    bool isFlipped;
    bool isMatched;
} Card;

typedef struct {
    int playerID;
    int socket;
    int score;
    char name[PLAYER_NAME_LENGTH];
    pid_t pid;
    bool connected;
    bool wantToJoin;
    bool readyToStart;
    bool pendingAction;
    int flipsDone;
    int firstFlipIndex;
    int secondFlipIndex;
    bool waitingNotified;
} Player;

typedef struct {
    char name[PLAYER_NAME_LENGTH];
    int wins;
} ScoreEntry;

typedef struct {
    ScoreEntry entries[MAX_PLAYERS];
    int count;
    pthread_mutex_t scoreMutex;
} scoreBoard;

/* --- Logging Types --- */

typedef enum {
    LOG_NONE,
    LOG_SERVER,
    LOG_PLAYER,
    LOG_GAME,
    LOG_TURN
} LogType;

typedef struct {
    LogType type;
    char message[LOG_MSG_LENGTH];
} LogEvent;

/* --- Main Shared Game State --- */

typedef struct {
    /* GROUP 1: 8-BYTE ALIGNMENT 
       Mutexes and Semaphores are typically 32-40 bytes and require 
       8-byte alignment. Putting them at offset 0 anchors the struct.
    */
    pthread_mutex_t mutex;
    pthread_mutex_t logQueueMutex;
    sem_t turnSemaphore;
    sem_t turnCompleteSemaphore;
    sem_t flipDoneSemaphore;
    sem_t logReadySemaphore;
    sem_t logItemsSemaphore;
    sem_t logSpacesSemaphore;

    /* GROUP 2: 4-BYTE ALIGNMENT 
       Grouping all integers together prevents "padding drift" 
       between the parent process and forked children.
    */
    int playerCount;         
    int currentTurn;         
    int boardRows;           
    int boardCols;           
    int totalPairs;
    int matchedPaires;
    int logQueueHead;
    int logQueueTail;

    /* GROUP 3: 1-BYTE ALIGNMENT */
    bool gameStarted;
    bool boardNeedsBroadcast;

    /* GROUP 4: COMPLEX TYPES & ARRAYS
       Large memory blocks are placed at the end.
    */
    Player players[MAX_PLAYERS];
    Card cards[MAX_CARDS];
    LogEvent logQueue[LOG_QUEUE_SIZE];
    scoreBoard scoreBoard;

} SharedGameState;

/* --- Helper Enums & Structures --- */

typedef enum {
    ACTION_FLIP,
    ACTION_JOIN,
    ACTION_READY,
    ACTION_QUIT
} PlayerActionType;

typedef struct {
    PlayerActionType type;
    int cardIndex;
    int playerID;
} PlayerAction;

typedef struct {
    int currentPlayerID;
    int cardIndex;
    int totalPlayers;
} PlayerTurn;

/* --- Function Prototypes --- */

void initGameState(SharedGameState *state);
void resetGameState(SharedGameState *state);
void printGameState(SharedGameState *state);
void setupBoard(SharedGameState *state, int rows, int cols);
void formatOfBoard(SharedGameState *state, char *buffer, size_t bufsize);
void sendBoardStateToAll(SharedGameState *state);

#endif