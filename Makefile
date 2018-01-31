OPTS = -pthread `curl-config --cflags` `curl-config --libs` -lmpg123

test-stream:
	gcc -ggdb -o test-stream test-stream.c $(OPTS)

clean:
	rm -f test-stream

