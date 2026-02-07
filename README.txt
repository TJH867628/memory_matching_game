========================================
Memory Matching Multiplayer Game (C)
========================================

This project is a multiplayer Memory Matching Card Game developed in C using:

• TCP Socket Programming
• POSIX Threads
• System V Shared Memory
• Mutexes and Semaphores for synchronization
• ZeroTier Virtual Network for remote multiplayer connection

Players connect to the server over a ZeroTier virtual LAN and play the game turn-by-turn.

--------------------------------------------------
1. REQUIREMENTS
--------------------------------------------------

• Linux environment
• GCC compiler
• ZeroTier installed and connected to the same network

All players must join the same ZeroTier network before running the game.

--------------------------------------------------
2. HOW TO COMPILE
--------------------------------------------------

In the project directory, run:

    make

Or compile manually:

    gcc server.c scheduler.c logger.c shared_state.c player.c score.c game.c -o server -pthread
    gcc client.c -o client

--------------------------------------------------
3. HOW TO RUN
--------------------------------------------------

Step 1 – Ensure ZeroTier is connected

Check your ZeroTier IP address:

    zerotier-cli listnetworks

Example ZeroTier IP:
    172.24.170.145

Step 2 – Start the server on the host machine:

    ./server

The server listens on port 8080.

Step 3 – Other players (remote machines) run the client:

    ./client

The client connects to the server using the server's ZeroTier IP address.

(Note: In client.c, set the server IP to the ZeroTier IP of the server.)

--------------------------------------------------
4. EXAMPLE COMMANDS (Client Side)
--------------------------------------------------
When running:

    Enter name (no spaces):

Type: 
    
    Chai

When prompted:

    Please type 1 to READY:

Type:

    1

When it is your turn:

    Enter card index:

Type:

    <card_number>
    <card_number>

Example:

    3
    8

--------------------------------------------------
5. GAME RULES SUMMARY
--------------------------------------------------

• The board contains pairs of hidden cards.
• Each player takes turns.
• On your turn, flip two cards using the FLIP command.
• If the cards match:
      → They remain revealed
      → You score a point
• If they do not match:
      → They flip back after 2 seconds
      → Turn passes to the next player
• The game ends when all pairs are matched.

--------------------------------------------------
6. GAME MODES SUPPORTED
--------------------------------------------------

• Multiplayer mode (3–4 players)
• Remote play via ZeroTier virtual LAN
• Turn-based gameplay
• Automatic restart if a player disconnects
• Shared memory game state
• Real-time board updates to all players
• Logging system for player actions

--------------------------------------------------
7. NOTES
--------------------------------------------------

• Server must be started before clients.
• All players must be in the same ZeroTier network.
• Maximum supported players: 4
• If a player disconnects, the game stops and waits for remaining players to READY again.

--------------------------------------------------
END OF README
--------------------------------------------------
