// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"
#include "hana_utils.h"

using namespace v8;

//DBCAPIInterface api;
//unsigned openConnections = 0;

void DBCAPI_CALLBACK warningCallback(dbcapi_stmt    *stmt,
                                     const char     *warning,
                                     dbcapi_i32     error_code,
                                     const char     *sql_state,
                                     void           *user_data)
/*********************************************/
{
    Connection *obj = (Connection*)user_data;
    warningCallbackBaton *baton = obj->warningBaton;

    if (baton == NULL) {
        return;
    }

    baton->obj = obj;
    baton->err = true;
    baton->error_code = error_code;
    baton->error_msg = warning;
    baton->sql_state = sql_state;
    baton->callback_required = true;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status;
    status = uv_queue_work(uv_default_loop(), req, Connection::warningCallbackWork,
        (uv_after_work_cb)Connection::warningCallbackAfter);
    assert(status == 0);
}

void Connection::warningCallbackWork(uv_work_t *req)
/**********************************************/
{
    //warningCallbackBaton *baton = static_cast<warningCallbackBaton*>(req->data);
    //scoped_lock lock(baton->obj->conn_mutex);
}

void Connection::warningCallbackAfter(uv_work_t *req)
/*********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope	scope(isolate);
    warningCallbackBaton *baton = static_cast<warningCallbackBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
             baton->callback, undef, baton->callback_required);
    delete req;
}

Connection::Connection(const FunctionCallbackInfo<Value> &args)
/***************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    uv_mutex_init(&conn_mutex);
    conn = NULL;
    autoCommit = true;
    warningBaton = NULL;
    is_connected = false;

    if (args.Length() >= 1) {
        if (args[0]->IsString()) {
            Local<String> str = args[0]->ToString();
            int string_len = str->Utf8Length();
            char *buf = new char[string_len + 1];
            str->WriteUtf8(buf);
            _arg.Reset(isolate, String::NewFromUtf8(isolate, buf));
            delete[] buf;
        } else if (args[0]->IsObject()) {
            hashToString(args[0]->ToObject(), _arg);
        }  else if (!args[0]->IsUndefined() && !args[0]->IsNull()) {
            throwErrorIP(0, "createConnection[createClient]([conn_params])",
                         "string|object", getJSTypeName(getJSType(args[0])).c_str());
            return;
        } else {
            _arg.Reset(isolate, String::NewFromUtf8(isolate, ""));
        }
    } else {
        _arg.Reset(isolate, String::NewFromUtf8(isolate, ""));
    }
}

Connection::~Connection()
/***********************/
{
    scoped_lock lock(conn_mutex);

    _arg.Reset();

    if (warningBaton != NULL) {
        delete warningBaton;
    }

    if (conn != NULL) {
        api.dbcapi_disconnect(conn);
        api.dbcapi_free_connection(conn);
        conn = NULL;
        openConnections--;
    }
};

Persistent<Function> Connection::constructor;

void Connection::Init(Isolate *isolate)
/***************************************/
{
    HandleScope scope(isolate);
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
    tpl->SetClassName(String::NewFromUtf8(isolate, "Connection"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Prototype
    NODE_SET_PROTOTYPE_METHOD(tpl, "exec", exec);
    NODE_SET_PROTOTYPE_METHOD(tpl, "execute", exec);
    NODE_SET_PROTOTYPE_METHOD(tpl, "prepare", prepare);
    NODE_SET_PROTOTYPE_METHOD(tpl, "connect", connect);
    NODE_SET_PROTOTYPE_METHOD(tpl, "disconnect", disconnect);
    NODE_SET_PROTOTYPE_METHOD(tpl, "close", disconnect);
    NODE_SET_PROTOTYPE_METHOD(tpl, "commit", commit);
    NODE_SET_PROTOTYPE_METHOD(tpl, "rollback", rollback);
    NODE_SET_PROTOTYPE_METHOD(tpl, "setAutoCommit", setAutoCommit);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getClientInfo", getClientInfo);
    NODE_SET_PROTOTYPE_METHOD(tpl, "setClientInfo", setClientInfo);
    NODE_SET_PROTOTYPE_METHOD(tpl, "setWarningCallback", setWarningCallback);

    constructor.Reset(isolate, tpl->GetFunction());
}

void Connection::New(const FunctionCallbackInfo<Value> &args)
/*************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    if (args.IsConstructCall()) {
        Connection *obj = new Connection(args);
        obj->Wrap(args.This());
        args.GetReturnValue().Set(args.This());
    }
    else {
        const int argc = 1;
        Local<Value> argv[argc] = {args[0]};
        Local<Function> cons = Local<Function>::New(isolate, constructor);
        args.GetReturnValue().Set(cons->NewInstance(argc, argv));
    }
}

void Connection::NewInstance(const FunctionCallbackInfo<Value> &args)
/*********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    const unsigned argc = 1;
    Handle<Value> argv[argc] = {args[0]};
    Local<Function> cons = Local<Function>::New(isolate, constructor);
    Local<Object> instance = cons->NewInstance(argc, argv);
    args.GetReturnValue().Set(instance);
}

NODE_API_FUNC( Connection::exec )
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    int  num_args = args.Length();
    int  cbfunc_arg = -1;
    int  invalidArg = -1;
    bool bind_required = false;
    unsigned int expectedTypes[] = { JS_STRING, JS_ARRAY | JS_FUNCTION, JS_FUNCTION };

    args.GetReturnValue().SetUndefined();

    if ( num_args == 0 || !args[0]->IsString() ) {
        invalidArg = 0;
    } else if (num_args == 2) {
        if ( args[1]->IsArray() ) {
            bind_required = true;
        } else if ( args[1]->IsFunction() ) {
            cbfunc_arg = 1;
        } else if ( !args[1]->IsUndefined() && !args[1]->IsNull() ) {
            invalidArg = 1;
        }
    } else if (num_args >= 3) {
        if ( args[1]->IsArray() || args[1]->IsNull() || args[1]->IsUndefined() ) {
            bind_required = args[1]->IsArray();
            if ( args[2]->IsFunction() || args[2]->IsUndefined() || args[2]->IsNull()) {
                cbfunc_arg = ( args[2]->IsFunction() ) ? 2 : -1;
            } else {
                invalidArg = 2;
            }
        } else {
            invalidArg = 1;
        }
    }

    if ( invalidArg >= 0 ) {
        throwErrorIP(invalidArg, "exec[ute](sql[, params][, callback])",
                     getJSTypeName(expectedTypes[invalidArg]).c_str(),
                     getJSTypeName(getJSType(args[invalidArg])).c_str());
        return;
    }

    bool callback_required = (cbfunc_arg >= 0);
    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );

    if( obj == NULL || obj->conn == NULL ) {
        int error_code;
	std::string error_msg;
        std::string sql_state;
        getErrorMsg( JS_ERR_INVALID_OBJECT, error_code, error_msg, sql_state );
	callBack( error_code, &error_msg, &sql_state, args[cbfunc_arg], undef, callback_required );
	args.GetReturnValue().SetUndefined();
	return;
    }

    String::Utf8Value 		param0( args[0]->ToString() );

    executeBaton *baton = new executeBaton();
    baton->dbcapi_stmt_ptr = NULL;
    baton->obj = obj;
    baton->callback_required = callback_required;
    baton->stmt = std::string(*param0);
    baton->del_stmt_ptr = true;

    if( bind_required ) {
        if (!getInputParameters(args[1], baton->provided_params, baton->error_code, baton->error_msg, baton->sql_state)) {
            callBack( baton->error_code, &baton->error_msg, &baton->sql_state, args[cbfunc_arg], undef, callback_required );
            args.GetReturnValue().SetUndefined();
            delete baton;
            return;
	}
    }

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	int status;
	status = uv_queue_work( uv_default_loop(), req, executeWork,
				(uv_after_work_cb)executeAfter );
	assert(status == 0);

	args.GetReturnValue().SetUndefined();
	return;
    }

    Persistent<Value> ResultSet;

    executeWork( req );
    bool success = fillResult( baton, ResultSet );

    if( baton->dbcapi_stmt_ptr != NULL ) {
	api.dbcapi_free_stmt( baton->dbcapi_stmt_ptr );
    }

    delete baton;
    delete req;

    if( !success ) {
	args.GetReturnValue().SetUndefined();
	return;
    }
    Local<Value> local_result = Local<Value>::New( isolate, ResultSet );
    args.GetReturnValue().Set( local_result );
    ResultSet.Reset();
}

struct prepareBaton {
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string 		error_msg;
    std::string                 sql_state;
    bool 			callback_required;

    Statement 			*obj;
    std::string 		stmt;
    Persistent<Value> 		stmtObj;

    prepareBaton() {
	err = false;
	callback_required = false;
	obj = NULL;
    }

    ~prepareBaton() {
	obj = NULL;
	callback.Reset();
	stmtObj.Reset();
    }
};

void Connection::prepareWork( uv_work_t *req )
/*********************************************/
{
    prepareBaton *baton = static_cast<prepareBaton*>(req->data);
    if( baton->obj == NULL || baton->obj->connection == NULL ||
	baton->obj->connection->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_code , baton->error_msg, baton->sql_state );
	return;
    }

    scoped_lock lock( baton->obj->connection->conn_mutex );

    baton->obj->dbcapi_stmt_ptr = api.dbcapi_prepare( baton->obj->connection->conn,
						  baton->stmt.c_str() );
    baton->obj->num_params = api.dbcapi_num_params( baton->obj->dbcapi_stmt_ptr );

    for (int i = 0; i < baton->obj->num_params; i++) {
        dbcapi_bind_param_info info;
        api.dbcapi_get_bind_param_info(baton->obj->dbcapi_stmt_ptr, i, &info);
        baton->obj->param_infos.push_back(info);
    }

    if( baton->obj->dbcapi_stmt_ptr == NULL ) {
	baton->err = true;
	getErrorMsg( baton->obj->connection->conn, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }
}

void Connection::prepareAfter( uv_work_t *req )
/**********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    prepareBaton *baton = static_cast<prepareBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    if( baton->err ) {
	callBack( baton->error_code, &( baton->error_msg ), &( baton->sql_state),
                  baton->callback, undef, baton->callback_required );
	delete baton;
	delete req;
	return;
    }

    if( baton->callback_required ) {
	Local<Value> stmtObj = Local<Value>::New( isolate, baton->stmtObj );
	callBack( 0, NULL, NULL, baton->callback, stmtObj,  baton->callback_required );
	baton->stmtObj.Reset();
    }

    delete baton;
    delete req;
}

NODE_API_FUNC( Connection::prepare )
/**********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int cbfunc_arg = -1;
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_STRING, JS_FUNCTION };
    bool isOptional[] = { false, true };
    if (!checkParameters(args, "prepare(sql, [callback])", 2, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg == 1);

    Connection *db = ObjectWrap::Unwrap<Connection>( args.This() );

    if( db == NULL || db->conn == NULL ) {
        int error_code;
	std::string error_msg;
        std::string sql_state;
	getErrorMsg( JS_ERR_NOT_CONNECTED, error_code, error_msg, sql_state );
	callBack( error_code, &error_msg, &sql_state, args[cbfunc_arg], undef, callback_required );
	args.GetReturnValue().SetUndefined();
	return;
    }

    Persistent<Object> p_stmt;
    Statement::CreateNewInstance( args, p_stmt );
    Local<Object> l_stmt = Local<Object>::New( isolate, p_stmt );
    Statement *obj = ObjectWrap::Unwrap<Statement>( l_stmt );
    obj->connection = db;
    obj->conn_mutex = &db->conn_mutex;

    if( obj == NULL ) {
        int error_code;
        std::string error_msg;
        std::string sql_state;
	getErrorMsg( JS_ERR_GENERAL_ERROR, error_code, error_msg, sql_state );
	callBack( error_code, &error_msg, &sql_state, args[cbfunc_arg], undef, callback_required );
	p_stmt.Reset();
	return;
    }

    String::Utf8Value param0( args[0]->ToString() );

    prepareBaton *baton = new prepareBaton();
    baton->obj = obj;
    baton->obj->sql = std::string(*param0);
    baton->callback_required = callback_required;
    baton->stmt =  std::string(*param0);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );
	baton->stmtObj.Reset( isolate, p_stmt );
	int status;
	status = uv_queue_work( uv_default_loop(), req, prepareWork,
				(uv_after_work_cb)prepareAfter );
	assert(status == 0);
	p_stmt.Reset();
	return;
    }

    prepareWork( req );
    bool err = baton->err;
    prepareAfter( req );

    if( err ) {
	return;
    }

    args.GetReturnValue().Set( p_stmt );
    p_stmt.Reset();
}

// Connect and disconnect
// Connect Function
struct connectBaton {
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string 		error_msg;
    std::string                 sql_state;
    bool 			callback_required;

    Connection 			*obj;
    bool 			external_connection;
    std::string 		conn_string;
    void 			*external_conn_ptr;

    connectBaton() {
	obj = NULL;
	external_conn_ptr = NULL;
	external_connection = false;
	err = false;
	callback_required = false;
    }

    ~connectBaton() {
	obj = NULL;
	external_conn_ptr = NULL;
	callback.Reset();
    }
};

void Connection::connectWork( uv_work_t *req )
/*********************************************/
{
    connectBaton *baton = static_cast<connectBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

    //if( api.initialized == false) {
    //    scoped_lock api_lock(api_mutex);
    //    if (api.initialized == false) {
    //        char * env = getenv("DBCAPI_API_DLL");
    //        if (!dbcapi_initialize_interface(&api, env)) {
    //            baton->err = true;
    //            getErrorMsg(JS_ERR_INITIALIZING_DBCAPI, baton->error_code, baton->error_msg, baton->sql_state);
    //            return;
    //        }
    //        if (!api.dbcapi_init("Node.js", _DBCAPI_VERSION,
    //            &(baton->obj->max_api_ver))) {
    //            baton->err = true;
    //            getErrorMsg(JS_ERR_INITIALIZING_DBCAPI, baton->error_code, baton->error_msg, baton->sql_state);
    //            return;
    //        }
    //    }
    //}

    if( !baton->external_connection ) {
        if (baton->obj->conn == NULL) {
            baton->obj->conn = api.dbcapi_new_connection();
        }
        api.dbcapi_set_autocommit( baton->obj->conn, baton->obj->autoCommit );

	if( !api.dbcapi_connect( baton->obj->conn, baton->conn_string.c_str() ) ) {
	    getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	    baton->err = true;
	    api.dbcapi_free_connection( baton->obj->conn );
	    baton->obj->conn = NULL;
	    return;
	}
    } else {
	baton->obj->conn = api.dbcapi_make_connection( baton->external_conn_ptr );
        api.dbcapi_set_autocommit( baton->obj->conn, baton->obj->autoCommit );
	if( baton->obj->conn == NULL ) {
	    getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	    return;
	}
    }

    api.dbcapi_register_warning_callback(baton->obj->conn, warningCallback, baton->obj);

    baton->obj->is_connected = true;
    baton->obj->external_connection = baton->external_connection;

    openConnections++;
}

void Connection::connectAfter( uv_work_t *req )
/**********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    connectBaton *baton = static_cast<connectBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    if( baton->err ) {
	callBack( baton->error_code, &( baton->error_msg ), &( baton->sql_state ),
                  baton->callback, undef, baton->callback_required );
	delete baton;
	delete req;
	return;
    }

    callBack( 0, NULL, NULL, baton->callback, undef, baton->callback_required, false );

    delete baton;
    delete req;
}

NODE_API_FUNC( Connection::connect )
/**********************************/
{
    Isolate      *isolate = args.GetIsolate();
    HandleScope  scope( isolate );
    int		 num_args = args.Length();
    int		 cbfunc_arg = -1;
    bool	 arg_is_string = false;
    bool	 arg_is_object = false;
    bool	 external_connection = false;
    int          invalidArg = -1;
    unsigned int expectedTypes[] = { JS_STRING | JS_OBJECT, JS_FUNCTION };

    args.GetReturnValue().SetUndefined();

    if( num_args == 1 ) {
        if( args[0]->IsFunction() ) {
	    cbfunc_arg = 0;
        } else if( args[0]->IsNumber() ){
	    external_connection = true;
        } else if( args[0]->IsString() ) {
            arg_is_string = true;
        } else if( args[0]->IsObject() ) {
	    arg_is_object = true;
        } else if( !args[0]->IsUndefined() && !args[0]->IsNull() ){
            invalidArg = 0;
        }
    } else if( num_args >= 2 ) {
        if( args[0]->IsString() || args[0]->IsObject() || args[0]->IsNumber() || args[0]->IsUndefined() || args[0]->IsNull() ) {
            if( args[1]->IsFunction() || args[1]->IsUndefined() || args[1]->IsNull()) {
                cbfunc_arg = (args[1]->IsFunction()) ? 1 : -1;
                if( args[0]->IsNumber() ) {
                    external_connection = true;
                } else if( args[0]->IsString() ) {
                    arg_is_string = true;
                }  else if( args[0]->IsObject() ) {
                    arg_is_object = true;
                }
            } else {
                invalidArg = 1;
            }
        } else {
            invalidArg = 0;
        }
    }

    if( invalidArg >= 0 ) {
        throwErrorIP(invalidArg, "connect(conn_params[, callback])",
                     getJSTypeName(expectedTypes[invalidArg]).c_str(),
                     getJSTypeName(getJSType(args[invalidArg])).c_str());
	return;
    }

    bool callback_required = (cbfunc_arg >= 0);
    Connection *obj = ObjectWrap::Unwrap<Connection>(args.This());
    connectBaton *baton = new connectBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    baton->external_connection = external_connection;

    if( external_connection ) {
	baton->external_conn_ptr = (void *)(long)args[0]->NumberValue();
    } else {
	Local<String> localArg = Local<String>::New( isolate, obj->_arg );
	if( localArg->Length() > 0 ) {
	    String::Utf8Value param0( localArg );
	    baton->conn_string = std::string(*param0);
	} else {
	    baton->conn_string = std::string();
	}
	if( arg_is_string ) {
	    String::Utf8Value param0( args[0]->ToString() );
            if ( baton->conn_string.length() > 0 )
	        baton->conn_string.append( ";" );
	    baton->conn_string.append(*param0);
	} else if( arg_is_object ) {
	    Persistent<String> arg_string;
	    hashToString( args[0]->ToObject(), arg_string );
	    Local<String> local_arg_string =
		Local<String>::New( isolate, arg_string );
	    String::Utf8Value param0( local_arg_string );
            if(baton->conn_string.length() > 0)
                baton->conn_string.append( ";" );
	    baton->conn_string.append(*param0);
	    arg_string.Reset();
	}
	baton->conn_string.append( ";CHARSET=UTF-8;SCROLLABLERESULT=0" );
    }

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );

	int status;
	status = uv_queue_work( uv_default_loop(), req, connectWork,
				(uv_after_work_cb)connectAfter );
	assert(status == 0);
	args.GetReturnValue().SetUndefined();
	return;
    }

    connectWork( req );
    connectAfter( req );
    args.GetReturnValue().SetUndefined();
    return;
}

// Disconnect Function
void Connection::disconnectWork( uv_work_t *req )
/************************************************/
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

    if( baton->obj->conn == NULL ) {
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    api.dbcapi_register_warning_callback(baton->obj->conn, NULL, baton->obj);

    if( !baton->obj->external_connection ) {
	api.dbcapi_disconnect( baton->obj->conn );
    }
    // Must free the connection object or there will be a memory leak
    api.dbcapi_free_connection( baton->obj->conn );

    baton->obj->is_connected = false;
    baton->obj->conn = NULL;

    openConnections--;
    if( openConnections <= 0 ) {
	openConnections = 0;
    }

    return;
}

NODE_API_FUNC( Connection::disconnect )
/*************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "disconnect([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );
    noParamBaton *baton = new noParamBaton();
    baton->callback_required = callback_required;
    baton->obj = obj;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );

	int status;
	status = uv_queue_work( uv_default_loop(), req, disconnectWork,
				(uv_after_work_cb)noParamAfter );
	assert(status == 0);

	args.GetReturnValue().SetUndefined();
	return;
    }

    disconnectWork( req );
    noParamAfter( req );
}

void Connection::commitWork( uv_work_t *req )
/********************************************/
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

    if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    if( !api.dbcapi_commit( baton->obj->conn ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }
}

NODE_API_FUNC( Connection::commit )
/*********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "commit([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );

    noParamBaton *baton = new noParamBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );

	int status;
	status = uv_queue_work( uv_default_loop(), req, commitWork,
				(uv_after_work_cb)noParamAfter );
	assert(status == 0);

	args.GetReturnValue().SetUndefined();
	return;
    }

    commitWork( req );
    noParamAfter( req );
}

void Connection::rollbackWork( uv_work_t *req )
/**********************************************/
{
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    scoped_lock lock( baton->obj->conn_mutex );

   if( baton->obj->conn == NULL ) {
	baton->err = true;
	getErrorMsg( JS_ERR_NOT_CONNECTED, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    if( !api.dbcapi_rollback( baton->obj->conn ) ) {
	baton->err = true;
	getErrorMsg( baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }
}

NODE_API_FUNC( Connection::rollback )
/***********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "rollback([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    Connection *obj = ObjectWrap::Unwrap<Connection>( args.This() );

    noParamBaton *baton = new noParamBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );

	int status;
	status = uv_queue_work( uv_default_loop(), req, rollbackWork,
				(uv_after_work_cb)noParamAfter );
	assert(status == 0);

	args.GetReturnValue().SetUndefined();
	return;
    }

    rollbackWork( req );
    noParamAfter( req );
}

NODE_API_FUNC(Connection::setAutoCommit)
/***********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_BOOLEAN };
    if (!checkParameters(args, "setAutoCommit(flag)", 1, expectedTypes)) {
        return;
    }

    Connection *obj = ObjectWrap::Unwrap<Connection>(args.This());
    convertToBool(args[0], obj->autoCommit);

    if (obj->conn != NULL) {
        if (!api.dbcapi_set_autocommit(obj->conn, obj->autoCommit)) {
            throwError(obj->conn);
        }
    }
}

NODE_API_FUNC(Connection::getClientInfo)
/***********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    args.GetReturnValue().Set(Null(isolate));

    // check parameters
    unsigned int expectedTypes[] = { JS_STRING };
    if (!checkParameters(args, "getClientInfo(key)", 1, expectedTypes)) {
        return;
    }

    String::Utf8Value prop(args[0]->ToString());
    Connection *obj = ObjectWrap::Unwrap<Connection>(args.This());
    const char* val = api.dbcapi_get_clientinfo(obj->conn, *prop);
    if (val != NULL) {
        args.GetReturnValue().Set(String::NewFromUtf8(isolate, val, NewStringType::kNormal, (int)strlen(val)).ToLocalChecked());
    }
}

NODE_API_FUNC(Connection::setClientInfo)
/***********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_STRING, JS_STRING };
    if (!checkParameters(args, "setClientInfo(key, value)", 2, expectedTypes)) {
        return;
    }

    String::Utf8Value prop(args[0]->ToString());
    String::Utf8Value val(args[1]->ToString());
    Connection *obj = ObjectWrap::Unwrap<Connection>(args.This());
    scoped_lock lock(obj->conn_mutex);
    //api.dbcapi_set_clientinfo(obj->conn, std::string(prop).c_str(), std::string(prop).c_str());
    if (obj->conn == NULL) {
        obj->conn = api.dbcapi_new_connection();
    }
    api.dbcapi_set_clientinfo(obj->conn, *prop, *val);
}

// Generic Baton and Callback (After) Function
// Use this if the function does not have any return values and
// Does not take any parameters.
// Create custom Baton and Callback (After) functions otherwise

void Connection::noParamAfter(uv_work_t *req)
/*********************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope	scope(isolate);
    noParamBaton *baton = static_cast<noParamBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required, false);
        return;
    }

    callBack(0, NULL, NULL, baton->callback, undef, baton->callback_required, false);

    delete baton;
    delete req;
}

NODE_API_FUNC(Connection::setWarningCallback)
/***********************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "setWarningCallback(callback)", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }

    Connection *obj = ObjectWrap::Unwrap<Connection>(args.This());

    if (obj->warningBaton != NULL) {
        delete obj->warningBaton;
        obj->warningBaton = NULL;
    }

    if (args[0]->IsUndefined() || args[0]->IsNull()) {
        return;
    }

    warningCallbackBaton *baton = new warningCallbackBaton();
    obj->warningBaton = baton;
    baton->obj = obj;
    Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
    baton->callback.Reset(isolate, callback);
}
