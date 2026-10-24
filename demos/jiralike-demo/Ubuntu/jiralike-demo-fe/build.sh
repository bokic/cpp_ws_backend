#!/bin/bash -e
PKG_DIR="pkg_build"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/opt/jiralike-demo-fe"

cp -r DEBIAN/* "$PKG_DIR/DEBIAN/"
(cd ../../web && npm install && npm run build)
cp -r ../../web/dist/web/browser/* "$PKG_DIR/opt/jiralike-demo-fe/"

dpkg-deb --build "$PKG_DIR" jiralike-demo-fe_0.1.deb
rm -rf "$PKG_DIR"
