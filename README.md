# MP3 streaming tests

Some basic tests with MP3 streaming using libCURL, the mpg123 library and the OpenAL API.

# Building

## Dependencies

The main dependencies are:

  - libCURL
  - mpg123
  - OpenAL

To install these on a Debian-like system, try the following as root:

    apt-get install libcurl4-gnutls-dev libmpg123-dev libopenal-dev

Note for Raspberry Pi: If using the apt repositories for Raspbian GNU/Linux, you may have problems with version 1:1.17.2-4 of libopenal-dev. Attempting playback with this version yielded an "Illegal instruction" error. If this is the case, you may need to compile OpenAL from scratch.

## Running make

Simple type

    make openal-test

to build the program, and

    make clean

to remove it. (There is no `make install` option currently.)

# Running

The binary should be invoked as follows.

    ./openal-test <url>

Example:

    ./openal-test https://designtrail.net/~david/audio/test1.mp3

## Controls

Type `p` to pause / resume. Type `q` to exit.

