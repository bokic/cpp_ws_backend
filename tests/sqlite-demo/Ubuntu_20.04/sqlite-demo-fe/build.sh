#!/bin/bash -e

PROJECT_NAME=sqlite-demo-fe
VERSION=$(git describe --tags)
PROJECT_DIR=${PROJECT_NAME}_${VERSION}

rm -f ${PROJECT_NAME}_*.deb

rm -rf ${PROJECT_NAME}_*

mkdir -p ${PROJECT_DIR}/srv/${PROJECT_NAME}
mkdir -p ${PROJECT_DIR}/DEBIAN

cp DEBIAN/* ${PROJECT_DIR}/DEBIAN
cp ../../www/* ${PROJECT_DIR}/srv/${PROJECT_NAME} -r

sed -i -e "s/{VERSION}/${VERSION}/g" ${PROJECT_DIR}/DEBIAN/control

dpkg-deb --build ${PROJECT_DIR}

rm -rf ${PROJECT_DIR}
