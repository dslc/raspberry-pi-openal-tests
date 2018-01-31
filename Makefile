
test1:
	gcc -ggdb -o test1-decode-mp2 test1-decode-mp2.c -pthread -lavutil -lavformat -lavresample -lavcodec -lswscale `curl-config --cflags` `curl-config --libs`

clean:
	rm -f test1

