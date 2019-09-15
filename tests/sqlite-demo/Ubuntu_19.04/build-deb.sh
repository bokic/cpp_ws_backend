#!/bin/bash

ARCH="$(dpkg --print-architecture)"
PROJECT_NAME=sqlite-demo
VER_MAJOR=0
VER_MINOR=1
VER_BUILD=1

VERSION=${VER_MAJOR}.${VER_MINOR}-${VER_BUILD}

PROJECT_DIR=${PROJECT_NAME}_${VERSION}

rm -f ${PROJECT_NAME}_*.deb

rm -rf ${PROJECT_NAME}_*

cd ..
./build.sh
cd Ubuntu_19.04

mkdir -p ${PROJECT_DIR}/opt/${PROJECT_NAME}
mkdir -p ${PROJECT_DIR}/DEBIAN

cp DEBIAN/* ${PROJECT_DIR}/DEBIAN
cp ../sqlite-demo ${PROJECT_DIR}/opt/${PROJECT_NAME}
cp ../chinook.db ${PROJECT_DIR}/opt/${PROJECT_NAME}

sed -i -e "s/{VERSION}/${VERSION}/g" ${PROJECT_DIR}/DEBIAN/control
sed -i -e "s/{ARCH}/${ARCH}/g" ${PROJECT_DIR}/DEBIAN/control

dpkg-deb --build ${PROJECT_DIR}

rm -rf ${PROJECT_DIR}
