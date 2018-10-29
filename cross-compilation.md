# Cross Compiling for ARM


These commands can be used for compiling click 2.0.1 for ARM 32-bit architecture.
The repository contains the changes to the code base before running the following commands

Note: This has been tested on x86_64 Ubuntu machine to run on raspi armv7.

```
$ export CC=arm-linux-gnueabi-gcc
$ export LINK=arm-linux-gnueabi-g++
$ export CXX=arm-linux-gnueabi-g++

$ ./configure --disable-int64 --disable-linuxmodule --host=arm-linux-gnueabi --build=x86_64 --enable-tools=mixed --disable-dynamic-linking --prefix=$HOME/click-install

$ make -j4

$ make install

```
