OPTS = `curl-config --cflags` `curl-config --libs` -lmpg123 -lopenal

openal-test:
	g++ -ggdb -std=c++14 -o openal-test openal-test.cpp player.cpp ring_buffer.cpp $(OPTS)

clean:
	rm -f openal-test

