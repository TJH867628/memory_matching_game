all:
	rm -f server client
	gcc server.c scheduler.c logger.c shared_state.c score.c game.c -o server -pthread
	gcc client.c -o client

clean:
	rm -f server client