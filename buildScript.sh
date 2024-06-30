#!/bin/bash

#BUILD SPOT
cd spot/ && ./configure --disable-python && make && make check && make install

#BUILD IPASIRUP-BMC
./configure  CXXFLAGS="-L/usr/local/lib -lspot  -L/usr/local/lib -lbddx -lz" && make && cp build/cadical .

