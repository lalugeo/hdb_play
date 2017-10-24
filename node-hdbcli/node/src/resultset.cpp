// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "hana_utils.h"

using namespace v8;
using namespace node;

// ResultSet Object Functions

ResultSet::ResultSet()
/********************/
{
    connection = NULL;
    conn_mutex = NULL;
    dbcapi_stmt_ptr = NULL;
    is_closed = false;
    fetched_first = false;
}

ResultSet::~ResultSet()
/*********************/
{
    scoped_lock lock(*conn_mutex);
    freeStmt(this);
    deleteColumnInfos();
}

void ResultSet::freeStmt(ResultSet *resultset)
{
    if (resultset->dbcapi_stmt_ptr != NULL) {
        api.dbcapi_free_stmt(resultset->dbcapi_stmt_ptr);
        resultset->dbcapi_stmt_ptr = NULL;
    }
    resultset->is_closed = true;
}

void ResultSet::deleteColumnInfos()
/*********************/
{
    clearVector(column_infos);
    num_cols = 0;
}

Persistent<Function> ResultSet::constructor;

void ResultSet::Init( Isolate *isolate )
/**************************************/
{
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New( isolate, New );
    tpl->SetClassName( String::NewFromUtf8( isolate, "ResultSet" ) );
    tpl->InstanceTemplate()->SetInternalFieldCount( 1 );

    NODE_SET_PROTOTYPE_METHOD( tpl, "close",		close );
    NODE_SET_PROTOTYPE_METHOD( tpl, "isClosed",	        isClosed );

    // Accessor Functions
    NODE_SET_PROTOTYPE_METHOD( tpl, "getColumnCount",	getColumnCount );
    NODE_SET_PROTOTYPE_METHOD( tpl, "getColumnName",	getColumnName );
    NODE_SET_PROTOTYPE_METHOD( tpl, "getColumnInfo",	getColumnInfo );
    NODE_SET_PROTOTYPE_METHOD( tpl, "getValue",	        getValue );
    NODE_SET_PROTOTYPE_METHOD( tpl, "getValues",        getValues );
    NODE_SET_PROTOTYPE_METHOD( tpl, "getData",          getData );
    //NODE_SET_PROTOTYPE_METHOD( tpl, "getDouble",	getDouble );
    //NODE_SET_PROTOTYPE_METHOD( tpl, "getString",	getString );
    //NODE_SET_PROTOTYPE_METHOD( tpl, "getInteger",	getInteger );
    //NODE_SET_PROTOTYPE_METHOD( tpl, "getTimestamp",	getTimestamp );

    NODE_SET_PROTOTYPE_METHOD( tpl, "next",		next );
    NODE_SET_PROTOTYPE_METHOD( tpl, "nextResult",	nextResult );

    constructor.Reset( isolate, tpl->GetFunction() );
}

// Utility Functions and Macros

bool ResultSet::getSQLValue( ResultSet *			obj,
			     dbcapi_data_value &		value,
			     const FunctionCallbackInfo<Value> &args )
/********************************************************************/
{
    int colIndex;
    if ( !validate(obj, args, colIndex) ) {
        return false;
    }

    memset(&value, 0, sizeof(dbcapi_data_value));
    scoped_lock lock(*obj->conn_mutex);
    if( !api.dbcapi_get_column( obj->dbcapi_stmt_ptr, colIndex, &value ) ) {
	throwError( obj->connection->conn );
	return false;
    }

    return true;
}

bool ResultSet::validate( ResultSet *			    obj,
			  const FunctionCallbackInfo<Value> &args,
                          int &colIndex )
/********************************************************************/
{
    if( isInvalid( obj ) ) {
	throwError( JS_ERR_INVALID_OBJECT );
	return false;
    }

    //if( !obj->fetched_first ) {
    //    throwError( JS_ERR_NO_FETCH_FIRST );
    //    return false;
    //}

    return checkColumnIndex(obj, args, colIndex);
}

bool ResultSet::checkColumnIndex( ResultSet *obj,
			          const FunctionCallbackInfo<Value> &args,
                                  int &colIndex )
/********************************************************************/
{
    if (args[0]->IsInt32()) {
        colIndex = args[0]->Int32Value();
        if (colIndex >= 0 && colIndex < obj->num_cols) {
            return true;
        }
    }

    std::string sqlState = "HY000";
    std::string errText = "Invalid column index.";
    throwError(JS_ERR_INVALID_INDEX, errText, sqlState);

    return false;
}

bool ResultSet::isInvalid( ResultSet *obj )
/*****************************************/
{
    return obj == NULL
        || obj->is_closed
        || obj->dbcapi_stmt_ptr == NULL
        || obj->connection == NULL
        || !obj->connection->is_connected;
}

void ResultSet::getColumnInfos()
/*****************************************/
{
    num_cols = fetchColumnInfos(dbcapi_stmt_ptr, column_infos);
}

struct nextBaton {
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string 		error_msg;
    std::string                 sql_state;
    bool 			callback_required;

    ResultSet 			*obj;
    bool 			retVal;

    nextBaton() {
	err = false;
	callback_required = false;
	obj = NULL;
    }

    ~nextBaton() {
	obj = NULL;
	callback.Reset();
    }
};

void ResultSet::nextAfter( uv_work_t *req )
/*****************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    nextBaton *baton = static_cast<nextBaton*>(req->data);
    Local<Value> undef = Local<Value>::New( isolate, Undefined( isolate ) );

    if( baton->err ) {
	callBack( baton->error_code, &( baton->error_msg ), &( baton->sql_state ),
                 baton->callback, undef, baton->callback_required );
	delete baton;
	delete req;
	return;
    }

    Local<Value> ret = Local<Value>::New( isolate,
					  Boolean::New( isolate, baton->retVal ) );
    callBack( 0, NULL, NULL, baton->callback, ret, baton->callback_required );
    delete baton;
    delete req;
}

void ResultSet::nextWork( uv_work_t *req )
/****************************************/
{
    nextBaton *baton = static_cast<nextBaton*>(req->data);
    if( isInvalid( baton->obj ) ) {
	baton->err = true;
	getErrorMsg( JS_ERR_INVALID_OBJECT, baton->error_code, baton->error_msg, baton->sql_state );
	return;
    }

    scoped_lock lock(*baton->obj->conn_mutex);
    baton->retVal = ( api.dbcapi_fetch_next( baton->obj->dbcapi_stmt_ptr ) != 0 );
    baton->obj->fetched_first = true;
}

void ResultSet::next( const FunctionCallbackInfo<Value> &args )
/*************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "next([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());
    nextBaton *baton = new nextBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if( callback_required ) {
	Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
	baton->callback.Reset( isolate, callback );

	int status = uv_queue_work( uv_default_loop(), req, nextWork,
				    (uv_after_work_cb)nextAfter );
	assert(status == 0);
	_unused( status );

	args.GetReturnValue().SetUndefined();
	return;
    }

    nextWork( req );
    bool err = baton->err;
    bool retVal = baton->retVal;
    nextAfter( req );

    if( err ) {
	args.GetReturnValue().SetUndefined();
	return;
    }
    args.GetReturnValue().Set( Boolean::New( isolate, retVal ) );
}

void ResultSet::getColumnCount(const FunctionCallbackInfo<Value> &args)
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());

    if (isInvalid(obj)) {
        args.GetReturnValue().SetUndefined();
    } else {
        args.GetReturnValue().Set(Integer::New(isolate, obj->num_cols));
    }
}

void ResultSet::getColumnName(const FunctionCallbackInfo<Value> &args)
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_INTEGER };
    if (!checkParameters(args, "getColumnName(colIndex)", 1, expectedTypes)) {
        return;
    }

    int colIndex;
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());
    if (checkColumnIndex(obj, args, colIndex)) {
        args.GetReturnValue().Set(String::NewFromUtf8(isolate, obj->column_infos[colIndex]->name));
    }
}

void ResultSet::getColumnInfo(const FunctionCallbackInfo<Value> &args)
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());

    if (isInvalid(obj)) {
        args.GetReturnValue().SetUndefined();
    } else {
        Local<Array> columnInfos = Array::New(isolate);
        for (int i = 0; i < obj->num_cols; i++) {
            Local<Object> columnInfo = Object::New(isolate);
            setColumnInfo(isolate, columnInfo, obj->column_infos[i]);
            columnInfos->Set(i, columnInfo);
        }

        args.GetReturnValue().Set(columnInfos);
    }
}

void ResultSet::getValue(const FunctionCallbackInfo<Value> &args)
/*********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_INTEGER };
    if (!checkParameters(args, "getValue(colIndex)", 1, expectedTypes)) {
        return;
    }

    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());
    dbcapi_data_value value;
    if (getSQLValue(obj, value, args)) {
        int colIndex = args[0]->Int32Value();
        setReturnValue(args, value, obj->column_infos[colIndex]->native_type);
    }
}

void ResultSet::getValues(const FunctionCallbackInfo<Value> &args)
/*********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());
    Local<Object> row = Object::New(isolate);
    int int_number;

    args.GetReturnValue().SetUndefined();

    scoped_lock lock(*obj->conn_mutex);

    if (isInvalid(obj)) {
        throwError(JS_ERR_INVALID_OBJECT);
        return;
    }

    for (int i = 0; i < obj->num_cols; i++) {
        dbcapi_data_value value;
        memset(&value, 0, sizeof(dbcapi_data_value));

        if (!api.dbcapi_get_column(obj->dbcapi_stmt_ptr, i, &value)) {
            throwError(obj->connection->conn);
            return;
        }

        Local<String> col_name = String::NewFromUtf8(isolate, obj->column_infos[i]->name);

        if (value.is_null != NULL && *(value.is_null)) {
            row->Set(col_name, Null(isolate));
            continue;
        }

        switch (value.type) {
            case A_INVALID_TYPE:
                args.GetReturnValue().Set(Null(isolate));
                break;
            case A_VAL32:
            case A_VAL16:
            case A_UVAL16:
            case A_VAL8:
            case A_UVAL8:
                convertToInt(value, int_number, true);
                if (obj->column_infos[i]->native_type == DT_BOOLEAN) {
                    row->Set(col_name, Boolean::New(isolate, int_number > 0 ? true : false));
                }  else {
                    row->Set(col_name, Integer::New(isolate, int_number));
                }
                break;
            case A_UVAL32:
            case A_UVAL64:
            case A_VAL64:
                row->Set(col_name, Number::New(isolate, *(long long*)(value.buffer)));
                break;
            case A_DOUBLE:
                row->Set(col_name, Number::New(isolate, *(double*)(value.buffer)));
                break;
            case A_BINARY: {
                    MaybeLocal<Object> mbuf = node::Buffer::Copy(
                        isolate, (char *)value.buffer,
                        (int)*(value.length));
                    row->Set(col_name, mbuf.ToLocalChecked());
                }
                break;
            case A_STRING:
                row->Set(col_name, String::NewFromUtf8(isolate,
                         (char *)value.buffer,
                         NewStringType::kNormal,
                         (int)*(value.length)).ToLocalChecked());
                break;
        }
    }

    args.GetReturnValue().Set(row);
}

void ResultSet::getTimestamp( const FunctionCallbackInfo<Value> &args )
/*********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>( args.This() );
    dbcapi_data_value value;

    getSQLValue( obj, value, args );

    if( *(value.is_null) ) {
	args.GetReturnValue().Set( Null( isolate ) );
	return;
    }

    std::string timestamp( value.buffer, *(value.length) );
    scoped_lock lock( *obj->conn_mutex );
    Local<Value> DateVal = Date::New( isolate, 0 );
    if( !StringtoDate( timestamp.c_str(), DateVal, obj->connection->conn ) ) {
	args.GetReturnValue().SetUndefined();
	return;
    }

    args.GetReturnValue().Set( DateVal );
}

void ResultSet::getInteger( const FunctionCallbackInfo<Value> &args )
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>( args.This() );
    dbcapi_data_value value;
    int retVal;

    getSQLValue( obj, value, args );

    if( *(value.is_null) ) {
	args.GetReturnValue().Set( Null( isolate ) );
	return;
    }

    if( !convertToInt( value, retVal ) ) {
	args.GetReturnValue().SetUndefined();
	return;
    }

    args.GetReturnValue().Set( Int32::New( isolate, retVal) );
}

void ResultSet::getBinary( const FunctionCallbackInfo<Value> &args )
/******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>( args.This() );
    dbcapi_data_value value;

    getSQLValue( obj, value, args );

    if( *(value.is_null) ) {
	args.GetReturnValue().Set( Null( isolate ) );
	return;
    }

    //Local<Object> buf = node::Buffer::New( isolate, value.buffer, *value.length );
    MaybeLocal<Object> mbuf = node::Buffer::Copy(isolate, (char *)value.buffer,
                                                 static_cast<size_t>(*value.length));
    Local<Object> buf = mbuf.ToLocalChecked();
    args.GetReturnValue().Set( buf );
}

void ResultSet::getString( const FunctionCallbackInfo<Value> &args )
/******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>( args.This() );
    dbcapi_data_value value;

    getSQLValue( obj, value, args );

    if( *(value.is_null) ) {
	args.GetReturnValue().Set( Null( isolate ) );
	return;
    }

    if( value.type == A_BINARY || value.type == A_STRING ) {
	// Early Exit
	args.GetReturnValue().Set( String::NewFromUtf8( isolate,
							value.buffer,
							NewStringType::kNormal,
							(int)*(value.length) ).ToLocalChecked());
	return;
    }

    std::ostringstream out;

    if( !convertToString( value, out ) ) {
	args.GetReturnValue().SetUndefined();
	return;
    }

    args.GetReturnValue().Set( String::NewFromUtf8( isolate, out.str().c_str() ) );
}

void ResultSet::getDouble( const FunctionCallbackInfo<Value> &args )
/******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>( args.This() );
    dbcapi_data_value value;
    double retVal;

    getSQLValue( obj, value, args );

    if( *(value.is_null) ) {
	args.GetReturnValue().Set( Null( isolate ) );
	return;
    }

    if( !convertToDouble( value, retVal ) ) {
	args.GetReturnValue().SetUndefined();
	return;
    }

    args.GetReturnValue().Set( Number::New( isolate, retVal ) );
}

struct getDataBaton {
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string 		error_msg;
    std::string                 sql_state;
    bool 			callback_required;

    ResultSet 			*obj;
    int 			retVal;

    int                         column_index;
    int                         data_offset;
    int                         length;
    void                        *buffer;

    getDataBaton() {
        err = false;
        callback_required = false;
        obj = NULL;
    }

    ~getDataBaton() {
        obj = NULL;
        callback.Reset();
    }
};

void ResultSet::getDataAfter(uv_work_t *req)
/****************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    getDataBaton *baton = static_cast<getDataBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required);
    }
    else {
        Local<Value> retObj = Local<Value>::New(isolate, Integer::New(isolate, baton->retVal));
        callBack(0, NULL, NULL, baton->callback, retObj, baton->callback_required);
    }

    delete baton;
    delete req;
}

void ResultSet::getDataWork(uv_work_t *req)
/***************************************************/
{
    getDataBaton *baton = static_cast<getDataBaton*>(req->data);
    scoped_lock lock(*baton->obj->conn_mutex);

    if (isInvalid(baton->obj)) {
        baton->err = true;
        getErrorMsg(JS_ERR_INVALID_OBJECT, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    //dbcapi_data_info	dinfo;
    //bool ret = api.dbcapi_get_data_info(baton->obj->dbcapi_stmt_ptr, 0, &dinfo);

    baton->retVal = api.dbcapi_get_data(baton->obj->dbcapi_stmt_ptr, baton->column_index, baton->data_offset, baton->buffer, baton->length);

    if (baton->retVal == -1) {
        baton->err = true;
        getErrorMsg(baton->obj->connection->conn, baton->error_code, baton->error_msg, baton->sql_state);
    }
}

NODE_API_FUNC(ResultSet::getData)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // JavaScript Parameters
    // int columnIndex  - zero-based column ordinal.
    // int dataOffset   - index within the column from which to begin the read operation.
    // byte[] buffer    - buffer into which to copy the data.
    // int bufferOffset - index with the buffer to which the data will be copied.
    // int length       - maximum number of bytes/characters to read.
    // function cb      - callback function

    unsigned int expectedTypes[] = { JS_INTEGER, JS_INTEGER, JS_BUFFER, JS_INTEGER, JS_INTEGER, JS_FUNCTION };
    bool isOptional[] = { false, false, false, false, false, true };
    if (!checkParameters(args, "getData(colIndex, dataOffset, buffer, bufferOffset, length[, callback])",
        6, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg == 5);

    int column_index;
    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());

    // Check column index
    if (!validate(obj, args, column_index)) {
        return;
    }

    int data_offset = args[1]->Int32Value();
    int buffer_offset = args[3]->Int32Value();
    int length = args[4]->Int32Value();

    // Check data_offset, buffer_offset, length
    std::string errText;
    if (data_offset < 0) {
        errText = "Invalid data_offset.";
    } else if (buffer_offset < 0) {
        errText = "Invalid buffer_offset.";
    } else if (length < 0) {
        errText = "Invalid length.";
    }
    if (errText.length() > 0) {
        std::string sqlState = "HY000";
        throwError(JS_ERR_INVALID_ARGUMENTS, errText, sqlState);
        return;
    }

    getDataBaton *baton = new getDataBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;
    baton->column_index = column_index;
    baton->data_offset = data_offset;
    baton->length = length;
    baton->buffer = Buffer::Data(args[2]) + buffer_offset;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status = uv_queue_work(uv_default_loop(), req, getDataWork,
                                   (uv_after_work_cb)getDataAfter);
        assert(status == 0);
        _unused(status);

        return;
    }

    getDataWork(req);
    int retVal = baton->retVal;
    getDataAfter(req);

    args.GetReturnValue().Set(Integer::New(isolate, retVal));
}

void ResultSet::isClosed( const FunctionCallbackInfo<Value> &args )
/*****************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );

    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>( args.This() );
    args.GetReturnValue().Set( Boolean::New( isolate, obj->is_closed ) );
}

struct closeBaton {
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string 		error_msg;
    std::string                 sql_state;
    bool 			callback_required;
    ResultSet 			*obj;

    closeBaton() {
        err = false;
        callback_required = false;
        obj = NULL;
    }

    ~closeBaton() {
        obj = NULL;
        callback.Reset();
    }
};

void ResultSet::closeAfter(uv_work_t *req)
/****************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    closeBaton *baton = static_cast<closeBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required);
        delete baton;
        delete req;
        return;
    }

    callBack(0, NULL, NULL, baton->callback, undef, baton->callback_required);

    delete baton;
    delete req;
}

void ResultSet::closeWork(uv_work_t *req)
/***************************************************/
{
    closeBaton *baton = static_cast<closeBaton*>(req->data);
    scoped_lock lock(*baton->obj->conn_mutex);

    if (isInvalid(baton->obj)) {
        baton->err = true;
        getErrorMsg(JS_ERR_INVALID_OBJECT, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    baton->obj->freeStmt(baton->obj);
}

void ResultSet::close(const FunctionCallbackInfo<Value> &args)
/************************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "close([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());

    if (obj->is_closed) {
        return;
    }

    closeBaton *baton = new closeBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status = uv_queue_work(uv_default_loop(), req, closeWork,
            (uv_after_work_cb)closeAfter);
        assert(status == 0);
        _unused(status);
        return;
    }

    closeWork(req);
    closeAfter(req);
}

struct nextResultBaton {
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string 		error_msg;
    std::string                 sql_state;
    bool 			callback_required;

    ResultSet 			*obj;
    bool 			retVal;

    nextResultBaton() {
        err = false;
        callback_required = false;
        obj = NULL;
    }

    ~nextResultBaton() {
        obj = NULL;
        callback.Reset();
    }
};

void ResultSet::nextResultAfter(uv_work_t *req)
/****************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    nextResultBaton *baton = static_cast<nextResultBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required);
        delete baton;
        delete req;
        return;
    }

    Local<Value> retObj = Local<Value>::New(isolate, Boolean::New(isolate, baton->retVal));
    callBack(0, NULL, NULL, baton->callback, retObj, baton->callback_required);

    delete baton;
    delete req;
}

void ResultSet::nextResultWork(uv_work_t *req)
/***************************************************/
{
    nextResultBaton *baton = static_cast<nextResultBaton*>(req->data);
    scoped_lock lock(*baton->obj->conn_mutex);
    if (isInvalid(baton->obj)) {
        baton->err = true;
        getErrorMsg(JS_ERR_INVALID_OBJECT, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    baton->retVal = (api.dbcapi_get_next_result(baton->obj->dbcapi_stmt_ptr) != 0);
    if (baton->retVal) {
        baton->obj->getColumnInfos();
    }
}

void ResultSet::nextResult(const FunctionCallbackInfo<Value> &args)
/************************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "nextResult([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    ResultSet *obj = ObjectWrap::Unwrap<ResultSet>(args.This());
    obj->fetched_first = false;

    nextResultBaton *baton = new nextResultBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status = uv_queue_work(uv_default_loop(), req, nextResultWork,
                                   (uv_after_work_cb)nextResultAfter);
        assert(status == 0);
        _unused(status);

        args.GetReturnValue().SetUndefined();
        return;
    }

    nextResultWork(req);
    bool retVal = baton->retVal;
    nextResultAfter(req);

    args.GetReturnValue().Set(Boolean::New(isolate, retVal));
}

void ResultSet::New( const FunctionCallbackInfo<Value> &args )
/************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );
    ResultSet* obj = new ResultSet();
    obj->Wrap( args.This() );
    args.GetReturnValue().Set( args.This() );
}

void ResultSet::NewInstance( const FunctionCallbackInfo<Value> &args )
/********************************************************************/
{
    Persistent<Object> obj;
    CreateNewInstance( args, obj );
    args.GetReturnValue().Set( obj );
}

void ResultSet::CreateNewInstance(
    const FunctionCallbackInfo<Value> &	args,
    Persistent<Object> &		obj )
/*******************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope( isolate );

    const unsigned argc = 1;
    Handle<Value> argv[argc] = { args[0] };
    Local<Function> local_func = Local<Function>::New( isolate, constructor );
    Local<Object> instance = local_func->NewInstance(argc, argv);
    obj.Reset(isolate, instance);
}
