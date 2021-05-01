# sqlite-demo
sqlite-demo is a demo webapp showing some of the cpp_ws_backend abilities.
  - Simple webservices that are fetching data from sqlite db and send data back as JSON.
  - webservice fetching systemd logs and(again) send data back as JSON.
### Building
Install dependencies
```sh
sudo apt update
sudo apt install git dialog gcc g++ ninja-build cmake libfcgi-dev libsqlite3-dev libjson-c-dev libsystemd-dev
```
Clone the project
```sh
git clone https://github.com/bokic/cpp_ws_backend.git
```
Build the project
```sh
cd cpp_ws_backend/tests/sqlite-demo/Ubuntu_20.04
./build-all.sh
```
### Installation
```sh
sudo apt install nginx libjson-c4 libsqlite3-0 libfcgi0ldbl
sudo dpkg -i sqlite-demo_0.1.deb sqlite-demo-fe_0.1.deb sqlite-demo-config-nginx_0.1.deb
sudo service nginx start
sudo service sqlite-demo start
sudo echo "127.0.0.1	sqlite-demo.com" >> /etc/hosts
```
### Start
Run http://sqlite-demo.com/ in your browser.
