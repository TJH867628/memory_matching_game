all:
	gcc server.c scheduler.c logger.c shared_state.c player.c score.c -o server -pthread
	gcc client.c -o client


clean:
	rm -f server client