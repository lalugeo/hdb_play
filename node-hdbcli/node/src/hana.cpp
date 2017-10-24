// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"
#include "hana_utils.h"

using namespace v8;

DBCAPIInterface api;
unsigned openConnections = 0;
uv_mutex_t api_mutex;

void executeWork( uv_work_t *req )
/********************************/
{
    executeBaton *baton = static_cast<executeBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

   if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    if( baton->dbcapi_stmt_ptr == NULL && baton->stmt.length() > 0 ) {
	baton->dbcapi_stmt_ptr = api.dbcapi_prepare( baton->obj->conn,
						 baton->stmt.c_str() );
	if( baton->dbcapi_stmt_ptr == NULL ) {
	    baton->err = true;
	    getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	    return;
	}
	baton->prepared_stmt = true;

    } else if( baton->dbcapi_stmt_ptr == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    if( !api.dbcapi_reset( baton->dbcapi_stmt_ptr) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    if (!checkParameterCount(baton->error_code, baton->error_msg, baton->sql_state,
        baton->provided_params, baton->dbcapi_stmt_ptr)) {
        baton->err = true;
        return;
    }

    getBindParameters(baton->provided_params, baton->params, baton->dbcapi_stmt_ptr);

    bool sendParamData = false;
    if (!bindParameters(baton->obj->conn, baton->dbcapi_stmt_ptr, baton->params,
        baton->error_code, baton->error_msg, baton->sql_state, sendParamData)) {
        baton->err = true;
        return;
    }

    if (sendParamData) {
        baton->send_param_data = true;
        if (baton->obj_stmt->execBaton != NULL && baton->obj_stmt->execBaton != baton) {
            delete baton->obj_stmt->execBaton;
        }
        baton->obj_stmt->execBaton = baton;
    }

    dbcapi_bool success_execute = api.dbcapi_execute( baton->dbcapi_stmt_ptr );

    if (success_execute && baton->obj_stmt != NULL) {
        copyParameters( baton->obj_stmt->params, baton->params );
    }

    if( !success_execute ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    if( !fetchResultSet( baton->dbcapi_stmt_ptr, baton->rows_affected, baton->col_names,
			 baton->string_vals, baton->num_vals, baton->int_vals,
			 baton->string_len, baton->col_types, baton->col_native_types) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }
}

void executeAfter( uv_work_t *req )
/*********************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    executeBaton *baton = static_cast<executeBaton*>( req->data );
    Persistent<Value> ResultSet;
    fillResult( baton, ResultSet );
    ResultSet.Reset();

    scoped_lock	lock( baton->obj->conn_mutex );

    if( baton->dbcapi_stmt_ptr != NULL && baton->prepared_stmt && baton->del_stmt_ptr ) {
	api.dbcapi_free_stmt( baton->dbcapi_stmt_ptr );
	baton->dbcapi_stmt_ptr = NULL;
    }

    if ( !baton->send_param_data ) {
        delete baton;
    }

    delete req;
}

bool cleanAPI()
/*************/
{
    if (openConnections == 0) {
        if (api.initialized) {
            api.dbcapi_fini();
            dbcapi_finalize_interface(&api);
            return true;
        }
    }
    return false;
}

void init( Local<Object> exports )
/********************************/
{
    uv_mutex_init(&api_mutex);
    Isolate *isolate = exports->GetIsolate();
    Statement::Init( isolate );
    Connection::Init( isolate );
    ResultSet::Init( isolate );
    NODE_SET_METHOD( exports, "createConnection", Connection::NewInstance );
    NODE_SET_METHOD( exports, "createClient", Connection::NewInstance );

    if (api.initialized == false) {
        scoped_lock api_lock(api_mutex);
        if (api.initialized == false) {
            unsigned int max_api_ver;
            char * env = getenv("DBCAPI_API_DLL");
            if (!dbcapi_initialize_interface(&api, env) ||
                !api.dbcapi_init("Node.js", _DBCAPI_VERSION, &max_api_ver)) {
                std::string sqlState = "HY000";
                std::string errText = "Failed to load DBCAPI.";
                throwError(JS_ERR_INITIALIZING_DBCAPI, errText, sqlState);
            }
        }
    }
}

NODE_MODULE( DRIVER_NAME, init )
