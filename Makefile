OPTS = `curl-config --cflags` `curl-config --libs` -lmpg123 -lopenal

test-stream:
	gcc -ggdb -o test-stream test-stream.c $(OPTS) -pthread

openal-test:
	gcc -ggdb -o openal-test openal-test.c $(OPTS)

clean:
	rm -f test-stream
	rm -f openal-test

