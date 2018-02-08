OPTS = `curl-config --cflags` `curl-config --libs` -lmpg123 -lopenal

openal-test:
	g++ -ggdb -o openal-test openal-test.cpp player.cpp $(OPTS)

clean:
	rm -f openal-test

