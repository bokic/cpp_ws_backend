#!/bin/bash -e

PROJECT_NAME=sqlite-demo-config-nginx
VERSION=$(git describe --tags)
PROJECT_DIR=${PROJECT_NAME}_${VERSION}

rm -f ${PROJECT_NAME}_*.deb

rm -rf ${PROJECT_NAME}_*

mkdir -p ${PROJECT_DIR}/etc/nginx/sites-available
mkdir -p ${PROJECT_DIR}/DEBIAN

cp DEBIAN/* ${PROJECT_DIR}/DEBIAN
cp 000-sqlite-demo.com ${PROJECT_DIR}/etc/nginx/sites-available

sed -i -e "s/{VERSION}/${VERSION}/g" ${PROJECT_DIR}/DEBIAN/control

dpkg-deb --build ${PROJECT_DIR}

rm -rf ${PROJECT_DIR}
