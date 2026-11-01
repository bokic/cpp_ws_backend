#!/usr/bin/env bash
# ==============================================================================
# Jira-Like Demo Launcher Script
# Starts the C++ FastCGI backend and the Node.js dev gateway (Angular SPA + API proxy)
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Default configurations (can be overridden via environment variables or flags)
PORT="${PORT:-4200}"
FCGI_PORT="${FCGI_PORT:-9002}"
WORKERS="${WORKERS:-4}"
DB_PATH="${DB_PATH:-$SCRIPT_DIR/db/jiralike.db}"
BLOBS_DIR="${BLOBS_DIR:-$SCRIPT_DIR/blobs}"
FORCE_BUILD=false
BACKEND_ONLY=false

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Launches the Jira-Like Demo (C++ FastCGI backend + Node.js dev-gateway).

Options:
  -p, --port <port>       HTTP gateway port for web app (default: 4200, env: PORT)
  -f, --fcgi-port <port>  FastCGI backend port (default: 9002, env: FCGI_PORT)
  -w, --workers <count>   FastCGI worker thread count (default: 4, env: WORKERS)
  -d, --db <path>         Path to SQLite database file (default: db/jiralike.db, env: DB_PATH)
  -u, --blobs <dir>       Path to blob attachments directory (default: blobs, env: BLOBS_DIR)
  -b, --build             Force rebuild backend and frontend before starting
  --backend-only          Start only the C++ FastCGI backend (e.g., if using system Nginx)
  -h, --help              Show this help message and exit

Examples:
  ./run.sh                     # Run with default settings (http://localhost:4200)
  ./run.sh -p 8080 -w 8        # Run webapp on port 8080 with 8 backend workers
  ./run.sh --build             # Rebuild and run
  ./run.sh --backend-only      # Run only FastCGI server on 127.0.0.1:9002
EOF
    exit 0
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -f|--fcgi-port)
            FCGI_PORT="$2"
            shift 2
            ;;
        -w|--workers)
            WORKERS="$2"
            shift 2
            ;;
        -d|--db)
            DB_PATH="$2"
            shift 2
            ;;
        -u|--blobs)
            BLOBS_DIR="$2"
            shift 2
            ;;
        -b|--build)
            FORCE_BUILD=true
            shift
            ;;
        --backend-only)
            BACKEND_ONLY=true
            shift
            ;;
        -h|--help)
            show_help
            ;;
        *)
            echo "Error: Unknown argument: $1" >&2
            echo "Run '$(basename "$0") --help' for usage." >&2
            exit 1
            ;;
    esac
done

echo "=================================================================="
echo "          Jira-Like Enterprise Project Management Demo           "
echo "=================================================================="

# Ensure storage directories exist
mkdir -p "$(dirname "$DB_PATH")"
mkdir -p "$BLOBS_DIR"

# 1. Check or build C++ backend binary
BINARY="$SCRIPT_DIR/jiralike-demo"
if [ "$FORCE_BUILD" = true ] || [ ! -x "$BINARY" ]; then
    echo ">> Building C++ FastCGI backend..."
    if [ -f "$ROOT_DIR/build.sh" ]; then
        (cd "$ROOT_DIR" && ./build.sh)
    else
        cmake -B "$ROOT_DIR/build" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$ROOT_DIR/build" -j"$(nproc)"
    fi
fi

if [ ! -x "$BINARY" ]; then
    echo "Error: Backend binary '$BINARY' not found or not executable." >&2
    exit 1
fi

# 2. Check or build Angular frontend bundle (unless --backend-only)
FRONTEND_DIST="$SCRIPT_DIR/web/dist/web/browser"
if [ "$BACKEND_ONLY" = false ]; then
    if [ "$FORCE_BUILD" = true ] || [ ! -f "$FRONTEND_DIST/index.html" ]; then
        echo ">> Building Angular SPA frontend..."
        if ! command -v npm >/dev/null 2>&1; then
            echo "Error: 'npm' is required to build the frontend. Please install Node.js." >&2
            exit 1
        fi
        (cd "$SCRIPT_DIR/web" && npm install && npm run build)
    fi

    if [ ! -f "$FRONTEND_DIST/index.html" ]; then
        echo "Error: Frontend bundle '$FRONTEND_DIST/index.html' not found." >&2
        exit 1
    fi
fi

# Process tracking variables
BACKEND_PID=""
GATEWAY_PID=""
CLEANING_UP=false

cleanup() {
    if [ "$CLEANING_UP" = true ]; then
        return
    fi
    CLEANING_UP=true
    echo ""
    echo ">> Shutting down Jira-Like demo..."

    if [ -n "$GATEWAY_PID" ] && kill -0 "$GATEWAY_PID" 2>/dev/null; then
        kill "$GATEWAY_PID" 2>/dev/null || true
    fi

    if [ -n "$BACKEND_PID" ] && kill -0 "$BACKEND_PID" 2>/dev/null; then
        kill "$BACKEND_PID" 2>/dev/null || true
    fi

    wait 2>/dev/null || true
    echo ">> Stopped cleanly."
}

trap cleanup INT TERM EXIT

# 3. Start C++ FastCGI Backend
echo ">> Starting FastCGI backend on 127.0.0.1:$FCGI_PORT (workers: $WORKERS)..."
"$BINARY" -n "127.0.0.1:$FCGI_PORT" -w "$WORKERS" -d "$DB_PATH" -u "$BLOBS_DIR" &
BACKEND_PID=$!

sleep 0.5
if ! kill -0 "$BACKEND_PID" 2>/dev/null; then
    echo "Error: FastCGI backend failed to start or crashed on launch." >&2
    exit 1
fi

# 4. Start HTTP Dev Gateway (if not backend-only)
if [ "$BACKEND_ONLY" = false ]; then
    if ! command -v node >/dev/null 2>&1; then
        echo "Error: 'node' is required to run dev-gateway.js. Please install Node.js." >&2
        exit 1
    fi

    echo ">> Starting HTTP Dev Gateway on port $PORT..."
    PORT="$PORT" FCGI_HOST="127.0.0.1" FCGI_PORT="$FCGI_PORT" node "$SCRIPT_DIR/dev-gateway.js" &
    GATEWAY_PID=$!

    sleep 0.5
    if ! kill -0 "$GATEWAY_PID" 2>/dev/null; then
        echo "Error: Dev gateway failed to start or crashed on launch." >&2
        exit 1
    fi

    echo "=================================================================="
    echo "  🚀 JIRALIKE DEMO IS READY AND RUNNING!"
    echo "  - Web Application:         http://localhost:$PORT"
    echo "  - Interactive API Docs:    http://localhost:$PORT/api/docs"
    echo "  - OpenAPI Specification:   http://localhost:$PORT/api/openapi.json"
    echo "  - FastCGI Listener:        127.0.0.1:$FCGI_PORT ($WORKERS workers)"
    echo "=================================================================="
    echo "  Press Ctrl+C to stop all services"
    echo "=================================================================="

    wait -n "$BACKEND_PID" "$GATEWAY_PID" 2>/dev/null || true
else
    echo "=================================================================="
    echo "  🚀 JIRALIKE FASTCGI BACKEND IS RUNNING!"
    echo "  - FastCGI Listener:        127.0.0.1:$FCGI_PORT ($WORKERS workers)"
    echo "=================================================================="
    echo "  Press Ctrl+C to stop the backend"
    echo "=================================================================="

    wait "$BACKEND_PID" 2>/dev/null || true
fi
