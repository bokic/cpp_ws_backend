#!/bin/bash -e

PROJECT_NAME=sqlite-demo
ARCH="$(dpkg --print-architecture)"
VERSION=$(git describe --tags)
PROJECT_DIR=${PROJECT_NAME}_${VERSION}

rm -f ${PROJECT_NAME}_*.deb

rm -rf ${PROJECT_NAME}_*

(cd ../.. && ./build.sh)

mkdir -p ${PROJECT_DIR}/etc/init.d
mkdir -p ${PROJECT_DIR}/opt/${PROJECT_NAME}
mkdir -p ${PROJECT_DIR}/DEBIAN

cp DEBIAN/* ${PROJECT_DIR}/DEBIAN
cp ../../sqlite-demo ${PROJECT_DIR}/opt/${PROJECT_NAME}
cp ../../chinook.db ${PROJECT_DIR}/opt/${PROJECT_NAME}
cp sqlite-demo.init.d ${PROJECT_DIR}/etc/init.d/sqlite-demo

sed -i -e "s/{VERSION}/${VERSION}/g" ${PROJECT_DIR}/DEBIAN/control
sed -i -e "s/{ARCH}/${ARCH}/g" ${PROJECT_DIR}/DEBIAN/control

dpkg-deb --build ${PROJECT_DIR}

rm -rf ${PROJECT_DIR}
rm -f ../../sqlite-demo
