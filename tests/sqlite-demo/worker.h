#pragma once

#include <string>
#include <list>
#include <map>


void request_ws_db_tables(std::map<std::string, std::string> header, std::list<std::string> uri_params);
void request_ws_jsGrid_customers(std::map<std::string, std::string> header, std::list<std::string> uri_params);
