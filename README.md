# MP3 streaming tests

Some basic MP3 streaming tests using libCURL, the mpg123 library and the OpenAL API. The primary target is the Raspberry Pi - but these tests should compile and run on most / all Linux systems.

# Building

## Dependencies

The main dependencies are:

  - libCURL
  - mpg123
  - OpenAL

To install these on a Debian-like system, try the following as root:

    apt-get install libcurl4-gnutls-dev libmpg123-dev libopenal-dev

### Installing OpenAL library

If using the apt repositories for Raspbian GNU/Linux, you may have problems with version 1:1.17.2-4 of `libopenal-dev`. Attempting playback with this version yielded an "Illegal instruction" error. If this is the case, you may need to compile OpenAL from scratch.

First of all install [CMake](https://cmake.org/ "CMake website").
    sudo apt-get install cmake
or
    su
    apt-get install cmake

Then download a recent version of OpenAL soft from either the [website](http://kcat.strangesoft.net/openal.html#download) or [Github repository](https://github.com/kcat/openal-soft). If using the tarball from the website, for example, the following should work.

    tar xf openal-soft-1.18.2.tar.bz2
    cd openal-soft-1.18.2
    cd build
    cmake ..
    make
    sudo make install

## Using GNU make

Simple type

    make openal-test

to build the program, and

    make clean

to remove it. (There is no `make install` option currently.)

## Using xmake

If you prefer to use `xmake` ( https://xmake.io/ ), use

    xmake -r

to build or re-build the project.

# Running

The binary should be invoked as follows.

    ./openal-test <url>

Example:

    ./openal-test https://designtrail.net/~david/audio/test1.mp3

## Controls

Type `p` to pause / resume. Type `q` to exit.


