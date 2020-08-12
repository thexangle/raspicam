#!/bin/bash

#remove existing build dir (if any)
sudo rm -r build

#create it again
mkdir build

#move there
cd build

#invoke cmake with the right options
cmake -DBUILD_SHARED_LIBS=OFF ..

#build and install libs
make -j4
sudo make install

