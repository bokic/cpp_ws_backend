# sqlite-demo
sqlite-demo is a demo webapp showing some of the cpp_ws_backend abilities.
  - Simple webservices that are fetching data from sqlite db and send data back as JSON.
  - webservice fetching systemd logs and(again) send data back as JSON.
### Building
Clone the project
```sh
git clone https://github.com/bokic/cpp_ws_backend.git
```
Install dependencies
```sh
sudo apt install gcc c++ make cmake libfcgi-dev libsqlite3-dev libjson-c-dev libsystemd-dev
```
Build the project
```sh
cd tests/sqlite-demo/Ubuntu_19.10
./build-all.sh
```
### Installation
```sh
sudo apt install nginx libjson-c4 libsqlite3-0 libfcgi0ldbl
sudo dpkg -i sqlite-demo-fe_0.1.deb sqlite-demo_0.1.deb sqlite-demo-config-nginx_0.1.deb
```