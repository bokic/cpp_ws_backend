# Stage 1: Build C++23 FastCGI Backend
FROM ubuntu:24.04 AS backend-builder
RUN apt-get update && apt-get install -y \
    build-essential cmake libfcgi-dev libsqlite3-dev libjson-c-dev libsystemd-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target jiralike-demo -j$(nproc)

# Stage 2: Build Angular SPA Frontend
FROM node:20-alpine AS frontend-builder
WORKDIR /web
COPY demos/jiralike-demo/web/package*.json ./
RUN npm install
COPY demos/jiralike-demo/web/ ./
RUN npm run build

# Stage 3: Runtime Image (Nginx + FastCGI Backend)
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y \
    nginx libfcgi0ldbl libsqlite3-0 libjson-c5 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/jiralike-demo
COPY --from=backend-builder /src/demos/jiralike-demo/jiralike-demo /opt/jiralike-demo/jiralike-demo
COPY demos/jiralike-demo/db/migrations /opt/jiralike-demo/db/migrations
COPY --from=frontend-builder /web/dist/web/browser /opt/jiralike-demo-fe
COPY demos/jiralike-demo/nginx/000-jiralike-demo.com /etc/nginx/sites-available/default

RUN mkdir -p /var/lib/jiralike-demo/db /var/lib/jiralike-demo/blobs

EXPOSE 80
CMD sh -c "/opt/jiralike-demo/jiralike-demo -n 127.0.0.1:9002 -w 4 -d /var/lib/jiralike-demo/db/jiralike.db -u /var/lib/jiralike-demo/blobs & nginx -g 'daemon off;'"
