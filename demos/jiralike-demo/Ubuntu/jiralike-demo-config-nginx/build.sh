#!/bin/bash -e
PKG_DIR="pkg_build"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN"
mkdir -p "$PKG_DIR/etc/nginx/sites-available"

cp -r DEBIAN/* "$PKG_DIR/DEBIAN/"
cp ../../nginx/000-jiralike-demo.com "$PKG_DIR/etc/nginx/sites-available/000-jiralike-demo.com"

dpkg-deb --build "$PKG_DIR" jiralike-demo-config-nginx_0.1.deb
rm -rf "$PKG_DIR"
