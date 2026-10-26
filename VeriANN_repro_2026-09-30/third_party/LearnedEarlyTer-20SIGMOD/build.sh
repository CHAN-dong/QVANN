#!/bin/sh

make clean
cd python/
make clean
cd ..
make -j4
sudo make install
make py

