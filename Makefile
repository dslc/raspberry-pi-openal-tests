OPTS = `curl-config --cflags` `curl-config --libs` -lmpg123 -lopenal

openal-test:
	gcc -ggdb -o openal-test openal-test.c $(OPTS)

clean:
	rm -f openal-test

