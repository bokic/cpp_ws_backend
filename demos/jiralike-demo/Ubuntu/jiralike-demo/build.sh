#!/bin/bash -e
PKG_DIR="pkg_build"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/opt/jiralike-demo/db/migrations"
mkdir -p "$PKG_DIR/var/lib/jiralike-demo/db"
mkdir -p "$PKG_DIR/var/lib/jiralike-demo/blobs"
mkdir -p "$PKG_DIR/etc/systemd/system"

cp -r DEBIAN/* "$PKG_DIR/DEBIAN/"
(cd ../../../.. && ./build.sh)
cp ../../jiralike-demo "$PKG_DIR/opt/jiralike-demo/jiralike-demo"
cp ../../db/migrations/*.sql "$PKG_DIR/opt/jiralike-demo/db/migrations/"
cp ../../jiralike-demo.service "$PKG_DIR/etc/systemd/system/jiralike-demo.service"

dpkg-deb --build "$PKG_DIR" jiralike-demo_0.1.deb
rm -rf "$PKG_DIR"
