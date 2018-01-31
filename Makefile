
test1:
	gcc -ggdb -o test1-decode-mp2 test1-decode-mp2.c -pthread -lavutil -lavformat -lavresample -lavcodec -lswscale `curl-config --cflags` `curl-config --libs`

test2:
	gcc -ggdb -o test2-decode-mp3 test2-decode-mp3.c -pthread `curl-config --cflags` `curl-config --libs` -lmpg123

clean:
	rm -f test1
	rm -f test2

