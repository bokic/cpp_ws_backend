#!/bin/bash -e

rm -f *.deb

(cd jiralike-demo              ; ./build.sh; mv *.deb ..)
(cd jiralike-demo-config-nginx ; ./build.sh; mv *.deb ..)
(cd jiralike-demo-fe           ; ./build.sh; mv *.deb ..)
