#pragma once

#include "wsserver.h"
#include "worker.h"

static backend::route routeMap[] = {
    {"/ws/db/tables",        backend::POST, request_ws_db_tables       },
    {"/ws/jsGrid/customers", backend::POST, request_ws_jsGrid_customers},
};
