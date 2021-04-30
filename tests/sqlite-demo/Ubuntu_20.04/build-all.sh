#!/bin/bash -e

rm -f *.deb

(cd sqlite-demo              ;./build.sh;mv *.deb ..)
(cd sqlite-demo-config-nginx ;./build.sh;mv *.deb ..)
(cd sqlite-demo-fe           ;./build.sh;mv *.deb ..)
