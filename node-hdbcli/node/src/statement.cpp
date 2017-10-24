// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"
#include "hana_utils.h"

using namespace v8;
using namespace node;

// Stmt Object Functions
Statement::Statement()
/**********************/
{
    connection = NULL;
    dbcapi_stmt_ptr = NULL;
    num_params = 0;
    execBaton = NULL;
    conn_mutex = NULL;
    is_dropped = false;
}

Statement::~Statement()
/***********************/
{
    if (execBaton != NULL) {
        delete execBaton;
        execBaton = NULL;
    }
    if( dbcapi_stmt_ptr != NULL ) {
        scoped_lock lock(*conn_mutex);
        api.dbcapi_free_stmt( dbcapi_stmt_ptr );
        dbcapi_stmt_ptr = NULL;
    }
    clearParameters( params );
    param_infos.clear();
}

Persistent<Function> Statement::constructor;

void Statement::Init(Isolate *isolate)
/***************************************/
{
    HandleScope	scope(isolate);
    // Prepare constructor template
    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
    tpl->SetClassName(String::NewFromUtf8(isolate, "Statement"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    // Prototype
    NODE_SET_PROTOTYPE_METHOD(tpl, "isValid", isValid);
    NODE_SET_PROTOTYPE_METHOD(tpl, "exec", exec);
    NODE_SET_PROTOTYPE_METHOD(tpl, "execute", exec);
    NODE_SET_PROTOTYPE_METHOD(tpl, "execQuery", execQuery);
    NODE_SET_PROTOTYPE_METHOD(tpl, "executeQuery", execQuery);
    NODE_SET_PROTOTYPE_METHOD(tpl, "execBatch", execBatch);
    NODE_SET_PROTOTYPE_METHOD(tpl, "executeBatch", execBatch);
    NODE_SET_PROTOTYPE_METHOD(tpl, "drop", drop);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getParameterInfo", getParameterInfo);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getParameterValue", getParameterValue);
    NODE_SET_PROTOTYPE_METHOD(tpl, "sendParameterData", sendParameterData);
    NODE_SET_PROTOTYPE_METHOD(tpl, "functionCode", functionCode);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getColumnInfo", getColumnInfo);

    constructor.Reset(isolate, tpl->GetFunction());
}

void Statement::New(const FunctionCallbackInfo<Value> &args)
/*************************************************************/
{
    Statement* obj = new Statement();

    obj->Wrap(args.This());
    args.GetReturnValue().Set(args.This());
}

void Statement::NewInstance(const FunctionCallbackInfo<Value> &args)
/*********************************************************************/
{
    Persistent<Object> obj;
    CreateNewInstance(args, obj);
    args.GetReturnValue().Set(obj);
}

void Statement::CreateNewInstance(const FunctionCallbackInfo<Value> &	args,
                                  Persistent<Object> &		obj)
    /***************************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope	scope(isolate);
    const unsigned argc = 1;
    Handle<Value> argv[argc] = { args[0] };
    Local<Function>cons = Local<Function>::New(isolate, constructor);
    obj.Reset(isolate, cons->NewInstance(argc, argv));
}

bool Statement::checkExecParameters(const FunctionCallbackInfo<Value> &args,
                                    const char *function,
                                    bool &bind_required,
                                    int  &cbfunc_arg)
/*******************************/
{
    int  num_args = args.Length();
    int  invalidArg = -1;
    unsigned int expectedTypes[] = { JS_ARRAY | JS_FUNCTION, JS_FUNCTION };

    cbfunc_arg = -1;
    bind_required = false;

    if (num_args == 1) {
        if (args[0]->IsArray()) {
            bind_required = true;
        } else if (args[0]->IsFunction()) {
            cbfunc_arg = 0;
        } else if (!args[0]->IsUndefined() && !args[0]->IsNull()) {
            invalidArg = 0;
        }
    } else if (num_args >= 2) {
        if (args[0]->IsArray() || args[0]->IsNull() || args[0]->IsUndefined()) {
            bind_required = args[0]->IsArray();
            if (args[1]->IsFunction() || args[1]->IsUndefined() || args[1]->IsNull()) {
                cbfunc_arg = (args[1]->IsFunction()) ? 1 : -1;
            } else {
                invalidArg = 1;
            }
        } else {
            invalidArg = 0;
        }
    }

    if (invalidArg >= 0) {
        throwErrorIP(invalidArg, function,
                     getJSTypeName(expectedTypes[invalidArg]).c_str(),
                     getJSTypeName(getJSType(args[invalidArg])).c_str());
        return false;
    }

    return true;
}

NODE_API_FUNC(Statement::isValid)
/*****************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());
    args.GetReturnValue().Set(Boolean::New(isolate, !obj->is_dropped));
}

NODE_API_FUNC(Statement::exec)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int  cbfunc_arg = -1;
    bool bind_required = false;

    args.GetReturnValue().SetUndefined();

    if (!checkExecParameters(args, "exec[ute]([params][, callback])", bind_required, cbfunc_arg)) {
        return;
    }

    bool callback_required = (cbfunc_arg >= 0);
    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    if (!Statement::checkStatement(obj, args, cbfunc_arg, callback_required)) {
        return;
    }

    executeBaton *baton = new executeBaton();
    baton->obj = obj->connection;
    baton->obj_stmt = obj;
    baton->dbcapi_stmt_ptr = obj->dbcapi_stmt_ptr;
    baton->callback_required = callback_required;

    if (bind_required) {
        if (!getInputParameters(args[0], baton->provided_params, baton->error_code, baton->error_msg, baton->sql_state)) {
            Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));
            callBack(baton->error_code, &baton->error_msg, &baton->sql_state, args[cbfunc_arg], undef, callback_required);
            return;
        }
    }

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status;
        status = uv_queue_work(uv_default_loop(), req, executeWork,
                               (uv_after_work_cb)executeAfter);
        assert(status == 0);

        return;
    }

    Persistent<Value> ResultSet;

    executeWork(req);
    bool success = fillResult(baton, ResultSet);
    delete req;

    if (!success) {
        return;
    }
    args.GetReturnValue().Set(ResultSet);
    ResultSet.Reset();
}

struct executeBatchBaton
{
    Persistent<Function>		callback;
    bool 				err;
    int                                 error_code;
    std::string 			error_msg;
    std::string                         sql_state;
    bool 				callback_required;

    Connection 				*obj;
    Statement                           *obj_stmt;
    dbcapi_stmt 			*dbcapi_stmt_ptr;

    std::string				stmt;
    std::vector<dbcapi_bind_data*> 	params;
    std::vector<size_t> 	        buffer_size;

    int 				rows_affected;
    int                                 batch_size;
    int                                 row_param_count;

    executeBatchBaton()
    {
        err = false;
        callback_required = false;
        obj = NULL;
        obj_stmt = NULL;
        dbcapi_stmt_ptr = NULL;
        batch_size = -1;
        rows_affected = -1;
        row_param_count = -1;
    }

    ~executeBatchBaton()
    {
        obj = NULL;
        // the Statement will free dbcapi_stmt_ptr
        dbcapi_stmt_ptr = NULL;
        callback.Reset();
        clearParameters(params);
    }
};

NODE_API_FUNC(Statement::execBatch)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;
    bool bind_required = false;
    char *fun = "exec[ute]Batch([params][, callback])";

    args.GetReturnValue().SetUndefined();

    if (!checkExecParameters(args, fun, bind_required, cbfunc_arg)) {
        return;
    }

    int row_param_count = -1;
    bool invalid_arguments = false;
    Handle<Array> bind_params = Handle<Array>::Cast(args[0]);
    int batch_size = bind_params->Length();

    if (batch_size < 1) {
        invalid_arguments = true;
    } else {
        for (int i = 0; i < batch_size; i++) {
            if (!bind_params->Get(i)->IsArray()) {
                invalid_arguments = true;
                break;
            } else {
                Handle<Array> values = Handle<Array>::Cast(bind_params->Get(i));
                int length = values->Length();
                if (length < 1 || (row_param_count != -1 && length != row_param_count)) {
                    invalid_arguments = true;
                    break;
                }  else {
                    row_param_count = length;
                }
            }
        }
    }

    if (invalid_arguments) {
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "Invalid parameter 1 for function '%s': expected an array of arrays with same length.", fun);
        std::string errText = buffer;
        std::string sqlState = "HY000";
        throwError(JS_ERR_INVALID_ARGUMENTS, errText, sqlState);
        return;
    }

    bool callback_required = (cbfunc_arg >= 0);
    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());
    if (!Statement::checkStatement(obj, args, cbfunc_arg, callback_required)) {
        return;
    }

    executeBatchBaton *baton = new executeBatchBaton();
    baton->obj = obj->connection;
    baton->obj_stmt = obj;
    baton->dbcapi_stmt_ptr = obj->dbcapi_stmt_ptr;
    baton->callback_required = callback_required;
    baton->batch_size = batch_size;
    baton->row_param_count = row_param_count;

    if (!getBindParameters(args[0], row_param_count, baton->params, baton->buffer_size)) {
        int error_code;
        std::string error_msg;
        std::string sql_state;
        Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));
        getErrorMsg(JS_ERR_BINDING_PARAMETERS, error_code, error_msg, sql_state);
        callBack(error_code, &error_msg, &sql_state, args[cbfunc_arg], undef, callback_required);
        delete baton;
        return;
    }

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status;
        status = uv_queue_work(uv_default_loop(), req, executeBatchWork,
                               (uv_after_work_cb)executeBatchAfter);
        assert(status == 0);
        return;
    }

    executeBatchWork(req);

    int rows_affected = baton->rows_affected;
    bool err = baton->err;

    delete baton;
    delete req;

    if (err) {
        return;
    }

    args.GetReturnValue().Set(Integer::New(isolate, rows_affected));
}

void Statement::executeBatchAfter(uv_work_t *req)
/*********************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    executeBatchBaton *baton = static_cast<executeBatchBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required);
        delete baton;
        delete req;
        return;
    }

    if (baton->callback_required) {
        Persistent<Value> rows_affected;
        rows_affected.Reset(isolate, Integer::New(isolate, baton->rows_affected));
        callBack(0, NULL, NULL, baton->callback, rows_affected, baton->callback_required);
        rows_affected.Reset();
    }

    delete baton;
    delete req;
}

void Statement::executeBatchWork(uv_work_t *req)
/********************************/
{
    executeBatchBaton *baton = static_cast<executeBatchBaton*>(req->data);
    scoped_lock lock(*baton->obj_stmt->conn_mutex);

    if (baton->obj->conn == NULL) {
        baton->err = true;
        getErrorMsg(JS_ERR_NOT_CONNECTED, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    if (baton->dbcapi_stmt_ptr == NULL && baton->stmt.length() > 0) {
        baton->dbcapi_stmt_ptr = api.dbcapi_prepare(baton->obj->conn,
                                                baton->stmt.c_str());
        if (baton->dbcapi_stmt_ptr == NULL) {
            baton->err = true;
            getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
            return;
        }
    }
    else if (baton->dbcapi_stmt_ptr == NULL) {
        baton->err = true;
        getErrorMsg(JS_ERR_INVALID_OBJECT, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    if (!api.dbcapi_reset(baton->dbcapi_stmt_ptr)) {
        baton->err = true;
        getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    dbcapi_bind_data param;
    std::vector<dbcapi_bind_data*> params;
    void* bufferSrc;

    for (int i = 0; i < baton->row_param_count; i++) {
        memset(&param, 0, sizeof(dbcapi_bind_data));

        if (!api.dbcapi_describe_bind_param(baton->dbcapi_stmt_ptr, i, &param)) {
            baton->err = true;
            getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
            return;
        }

        dbcapi_bind_data* paramNew = new dbcapi_bind_data();
        memset(paramNew, 0, sizeof(dbcapi_bind_data));
        params.push_back(paramNew);

        params[i]->value.buffer_size = baton->buffer_size[i];
        params[i]->value.type = baton->params[i]->value.type;
        params[i]->direction = param.direction;

        params[i]->value.buffer = new char[baton->batch_size * baton->buffer_size[i]];
        for (int j = 0; j < baton->batch_size; j++) {
            bufferSrc = baton->params[baton->row_param_count * j + i]->value.buffer;
            if (bufferSrc != NULL) {
                memcpy(params[i]->value.buffer + baton->buffer_size[i] * j, bufferSrc, baton->buffer_size[i]);
            }
        }

        params[i]->value.length = new size_t[baton->batch_size * sizeof(size_t)];
        for (int j = 0; j < baton->batch_size; j++) {
            params[i]->value.length[j] = *baton->params[baton->row_param_count * j + i]->value.length;
        }

        params[i]->value.is_null = new dbcapi_bool[baton->batch_size * sizeof(dbcapi_bool)];
        for (int j = 0; j < baton->batch_size; j++) {
            params[i]->value.is_null[j] = *baton->params[baton->row_param_count * j + i]->value.is_null;
        }

        if (!api.dbcapi_bind_param(baton->dbcapi_stmt_ptr, i, params[i])) {
            baton->err = true;
            getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
            clearParameters(params);
            return;
        }
    }

    dbcapi_bool success_execute = api.dbcapi_set_batch_size(baton->dbcapi_stmt_ptr, baton->batch_size);
    if (!success_execute) {
        baton->err = true;
        getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
        clearParameters(params);
        return;
    }

    success_execute = api.dbcapi_execute(baton->dbcapi_stmt_ptr);
    if (!success_execute) {
        baton->err = true;
        getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
        clearParameters(params);
        return;
    }

    baton->rows_affected = api.dbcapi_affected_rows(baton->dbcapi_stmt_ptr);
    clearParameters(params);
}

bool Statement::checkStatement(Statement *obj,
                               const FunctionCallbackInfo<Value> &args,
                               int cbfunc_arg,
                               bool callback_required)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (obj == NULL || obj->connection == NULL || obj->connection->conn == NULL ||
        obj->dbcapi_stmt_ptr == NULL) {
        int error_code;
        std::string error_msg;
        std::string sql_state;
        getErrorMsg(JS_ERR_INVALID_OBJECT, error_code, error_msg, sql_state);
        callBack(error_code, &error_msg, &sql_state, args[cbfunc_arg], undef, callback_required);
        args.GetReturnValue().SetUndefined();
        return false;
    }
    return true;
}

struct executeQueryBaton {
    Persistent<Function>		callback;
    bool 				err;
    int                                 error_code;
    std::string 			error_msg;
    std::string                         sql_state;
    bool 				callback_required;

    Connection 				*obj;
    Statement                           *obj_stmt;
    dbcapi_stmt 			*dbcapi_stmt_ptr;

    std::vector<dbcapi_bind_data*> 	params;
    std::vector<dbcapi_bind_data*> 	provided_params;

    Persistent<Value> 		        resultSetObj;

    executeQueryBaton()
    {
        err = false;
        callback_required = false;
        obj = NULL;
        obj_stmt = NULL;
        dbcapi_stmt_ptr = NULL;
    }

    ~executeQueryBaton()
    {
        //dbcapi_stmt_ptr will be freed by ResultSet
        callback.Reset();
        resultSetObj.Reset();
        clearParameters(params);
        clearParameters(provided_params);
    }
};

void Statement::executeQueryAfter(uv_work_t *req)
/******************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    executeQueryBaton *baton = static_cast<executeQueryBaton*>(req->data);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        // Error Message is already set in the executeQueryWork() function
        callBack(0, NULL, NULL, baton->callback, undef, baton->callback_required);
        delete baton;
        delete req;
        return;
    }

    if (baton->callback_required) {
        Local<Value> resultSetObj = Local<Value>::New(isolate,
                                                      baton->resultSetObj);
        baton->resultSetObj.Reset();
        ResultSet *resultset = node::ObjectWrap::Unwrap<ResultSet>(resultSetObj->ToObject());
        resultset->connection = baton->obj_stmt->connection;
        resultset->conn_mutex = baton->obj_stmt->conn_mutex;
        resultset->dbcapi_stmt_ptr = baton->dbcapi_stmt_ptr;
        resultset->getColumnInfos();

        callBack(0, NULL, NULL, baton->callback, resultSetObj, baton->callback_required);
    }

    delete baton;
    delete req;
}

void Statement::executeQueryWork(uv_work_t *req)
/*****************************************************/
{
    executeQueryBaton *baton = static_cast<executeQueryBaton*>(req->data);
    scoped_lock lock(*baton->obj_stmt->conn_mutex);

    baton->dbcapi_stmt_ptr = api.dbcapi_prepare(baton->obj_stmt->connection->conn,
                                                baton->obj_stmt->sql.c_str());
    if (baton->dbcapi_stmt_ptr == NULL) {
        baton->err = true;
        getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
        return;
    }

    bool sendParamData = false;
    if (!bindParameters(baton->obj->conn, baton->dbcapi_stmt_ptr, baton->params,
        baton->error_code, baton->error_msg, baton->sql_state, sendParamData)) {
        baton->err = true;
        return;
    }

    dbcapi_bool success_execute = api.dbcapi_execute(baton->dbcapi_stmt_ptr);

    if (success_execute) {
        copyParameters(baton->obj_stmt->params, baton->params);
    } else {
        baton->err = true;
        getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
    }
}

NODE_API_FUNC(Statement::execQuery)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int  cbfunc_arg = -1;
    bool bind_required = false;

    args.GetReturnValue().SetUndefined();

    if (!checkExecParameters(args, "exec[ute]Query([params][, callback])", bind_required, cbfunc_arg)) {
        return;
    }

    bool callback_required = (cbfunc_arg >= 0);
    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    if (!Statement::checkStatement(obj, args, cbfunc_arg, callback_required)) {
        return;
    }

    Persistent<Object> resultSetObj;
    ResultSet::CreateNewInstance(args, resultSetObj);

    executeQueryBaton *baton = new executeQueryBaton();
    baton->obj = obj->connection;
    baton->obj_stmt = obj;
    baton->dbcapi_stmt_ptr = obj->dbcapi_stmt_ptr;
    baton->callback_required = callback_required;

    if (bind_required) {
        if (!getInputParameters(args[0], baton->provided_params, baton->error_code, baton->error_msg, baton->sql_state) ||
            !checkParameterCount(baton->error_code, baton->error_msg, baton->sql_state, baton->provided_params, baton->dbcapi_stmt_ptr)) {
            Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));
            callBack(baton->error_code, &baton->error_msg, &baton->sql_state, args[cbfunc_arg], undef, callback_required);
            delete baton;
            return;
        }
    }

    getBindParameters(baton->provided_params, baton->params, baton->dbcapi_stmt_ptr);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        baton->resultSetObj.Reset(isolate, resultSetObj);
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status = uv_queue_work(uv_default_loop(), req, executeQueryWork,
                                   (uv_after_work_cb)executeQueryAfter);
        assert(status == 0);
        _unused(status);
        return;
    }

    Local<Object>lres = Local<Object>::New(isolate, resultSetObj);
    ResultSet *resultset = node::ObjectWrap::Unwrap<ResultSet>(lres);
    resultset->connection = obj->connection;
    resultset->conn_mutex = obj->conn_mutex;

    executeQueryWork(req);
    resultset->dbcapi_stmt_ptr = baton->dbcapi_stmt_ptr;
    bool err = baton->err;
    executeQueryAfter(req);

    if (err) {
        return;
    }

    resultset->getColumnInfos();
    args.GetReturnValue().Set(resultSetObj);
}

struct dropBaton
{
    Persistent<Function> 	callback;
    bool 			err;
    int                         error_code;
    std::string                 error_msg;
    std::string                 sql_state;
    bool 			callback_required;

    Statement 			*obj;

    dropBaton()
    {
        err = false;
        callback_required = false;
        obj = NULL;
    }

    ~dropBaton()
    {
        obj = NULL;
        callback.Reset();
    }
};

void Statement::dropAfter(uv_work_t *req)
/*******************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    dropBaton *baton = static_cast<dropBaton*>(req->data);
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

void Statement::dropWork(uv_work_t *req)
/******************************************/
{
    dropBaton *baton = static_cast<dropBaton*>(req->data);
    scoped_lock lock(*baton->obj->conn_mutex);

    if (baton->obj->dbcapi_stmt_ptr != NULL) {
        api.dbcapi_free_stmt(baton->obj->dbcapi_stmt_ptr);
    }

    baton->obj->dbcapi_stmt_ptr = NULL;
    //baton->obj->connection = NULL;
    baton->obj->is_dropped = true;
}

NODE_API_FUNC(Statement::drop)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_FUNCTION };
    bool isOptional[] = { true };
    if (!checkParameters(args, "drop([callback])", 1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    if (obj->is_dropped) {
        return;
    }

    dropBaton *baton = new dropBaton();
    baton->obj = obj;
    baton->callback_required = callback_required;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status;
        status = uv_queue_work(uv_default_loop(), req, dropWork,
                               (uv_after_work_cb)dropAfter);
        assert(status == 0);
        return;
    }

    dropWork(req);
    dropAfter(req);

    return;
}

NODE_API_FUNC(Statement::getParameterInfo)
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    int num_params = api.dbcapi_num_params(obj->dbcapi_stmt_ptr);
    Local<Array> paramInfos = Array::New(isolate);

    dbcapi_bind_data data;
    dbcapi_bind_param_info info;

    args.GetReturnValue().SetUndefined();

    for (int i = 0; i < num_params; i++) {
        if (api.dbcapi_get_bind_param_info(obj->dbcapi_stmt_ptr, i, &info) &&
            api.dbcapi_describe_bind_param(obj->dbcapi_stmt_ptr, i, &data)) {
            Local<Object> paramInfo = Object::New(isolate);
            paramInfo->Set(String::NewFromUtf8(isolate, "name"),
                           String::NewFromUtf8(isolate, info.name));
            paramInfo->Set(String::NewFromUtf8(isolate, "direction"),
                           Integer::New(isolate, info.direction));
            paramInfo->Set(String::NewFromUtf8(isolate, "nativeType"),
                           Integer::New(isolate, info.native_type));
            paramInfo->Set(String::NewFromUtf8(isolate, "nativeTypeName"),
                           String::NewFromUtf8(isolate, getNativeTypeName(info.native_type)));
            paramInfo->Set(String::NewFromUtf8(isolate, "precision"),
                           Integer::NewFromUnsigned(isolate, info.precision));
            paramInfo->Set(String::NewFromUtf8(isolate, "scale"),
                           Integer::NewFromUnsigned(isolate, info.scale));
            paramInfo->Set(String::NewFromUtf8(isolate, "maxSize"),
                           Integer::New(isolate, (int)info.max_size));
            paramInfo->Set(String::NewFromUtf8(isolate, "type"),
                           Integer::New(isolate, data.value.type));
            paramInfo->Set(String::NewFromUtf8(isolate, "typeName"),
                           String::NewFromUtf8(isolate, getTypeName(data.value.type)));
            paramInfos->Set(i, paramInfo);
        } else {
            throwError(obj->connection->conn);
        }
    }

    args.GetReturnValue().Set(paramInfos);
}

bool Statement::checkParameterIndex(Statement *obj,
                                    const FunctionCallbackInfo<Value> &args,
                                    int &paramIndex)
/*******************************/
{
    if (args[0]->IsInt32()) {
        paramIndex = args[0]->Int32Value();
        if (paramIndex >= 0 && paramIndex < (int)(obj->params.size())) {
            return true;
        }
    }

    std::string sqlState = "HY000";
    std::string errText = "Invalid parameter index.";
    throwError(JS_ERR_INVALID_INDEX, errText, sqlState);

    return false;
}

NODE_API_FUNC(Statement::getParameterValue)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_INTEGER };
    if (!checkParameters(args, "getParameterValue(paramIndex)", 1, expectedTypes)) {
        return;
    }

    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    int paramIndex;
    if (checkParameterIndex(obj, args, paramIndex)) {
        dbcapi_data_value & value = obj->params[paramIndex].value;
        setReturnValue(args, value, obj->param_infos[paramIndex].native_type);
    }
}

struct sendParameterDataBaton
{
    Persistent<Function>		callback;
    bool 				callback_required;

    bool 				err;
    int                                 error_code;
    std::string 			error_msg;
    std::string                         sql_state;

    Connection 				*obj;
    Statement                           *obj_stmt;
    dbcapi_stmt 			*dbcapi_stmt_ptr;

    int                                 column_index;
    size_t                              data_length;
    char*                               data;

    dbcapi_bool                         succeeded;

    sendParameterDataBaton()
    {
        err = false;
        callback_required = false;
        obj = NULL;
        obj_stmt = NULL;
        dbcapi_stmt_ptr = NULL;
        succeeded = false;
    }

    ~sendParameterDataBaton()
    {
        obj = NULL;
        dbcapi_stmt_ptr = NULL;
        callback.Reset();
    }
};

void Statement::sendParameterDataAfter(uv_work_t *req)
/*******************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    sendParameterDataBaton *baton = static_cast<sendParameterDataBaton*>(req->data);
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

void Statement::sendParameterDataWork(uv_work_t *req)
/******************************************/
{
    sendParameterDataBaton *baton = static_cast<sendParameterDataBaton*>(req->data);
    scoped_lock lock(*baton->obj_stmt->conn_mutex);

    baton->succeeded = api.dbcapi_send_param_data(baton->dbcapi_stmt_ptr,
        baton->column_index, baton->data, baton->data_length);

    if (!baton->succeeded) {
        baton->err = true;
        getErrorMsg(baton->obj->conn, baton->error_code, baton->error_msg, baton->sql_state);
    }
}

NODE_API_FUNC(Statement::sendParameterData)
/*******************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    int cbfunc_arg = -1;

    args.GetReturnValue().SetUndefined();

    // check parameters
    unsigned int expectedTypes[] = { JS_INTEGER, JS_BUFFER, JS_FUNCTION };
    bool isOptional[] = { false, false, true };
    if (!checkParameters(args, "sendParameterData(columnIndex, buffer[, callback])",
        1, expectedTypes, &cbfunc_arg, isOptional)) {
        return;
    }
    bool callback_required = (cbfunc_arg >= 0);

    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    int index;
    if (!checkParameterIndex(obj, args, index)) {
        return;
    }

    if (!Statement::checkStatement(obj, args, cbfunc_arg, callback_required)) {
        return;
    }

    sendParameterDataBaton *baton = new sendParameterDataBaton();
    baton->obj = obj->connection;
    baton->obj_stmt = obj;
    baton->dbcapi_stmt_ptr = obj->dbcapi_stmt_ptr;
    baton->callback_required = callback_required;
    baton->column_index = index;
    baton->data_length = Buffer::Length(args[1]);
    baton->data = Buffer::Data(args[1]);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    if (callback_required) {
        Local<Function> callback = Local<Function>::Cast(args[cbfunc_arg]);
        baton->callback.Reset(isolate, callback);

        int status;
        status = uv_queue_work(uv_default_loop(), req, sendParameterDataWork,
            (uv_after_work_cb)sendParameterDataAfter);
        assert(status == 0);

        return;
    }

    sendParameterDataWork(req);

    bool succeeded = (baton->succeeded == 1) ? true : false;
    bool err = baton->err;

    delete baton;
    delete req;

    if (err) {
        return;
    }

    args.GetReturnValue().Set(Boolean::New(isolate, succeeded));
}

NODE_API_FUNC(Statement::functionCode)
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());
    args.GetReturnValue().Set(Integer::New(isolate, api.dbcapi_get_function_code(obj->dbcapi_stmt_ptr)));
}

NODE_API_FUNC(Statement::getColumnInfo)
/*******************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Statement *obj = ObjectWrap::Unwrap<Statement>(args.This());

    std::vector<dbcapi_column_info*> column_infos;
    int num_cols = fetchColumnInfos(obj->dbcapi_stmt_ptr, column_infos);
    Local<Array> columnInfos = Array::New(isolate);

    for (int i = 0; i < num_cols; i++) {
        Local<Object> columnInfo = Object::New(isolate);
        setColumnInfo(isolate, columnInfo, column_infos[i]);
        columnInfos->Set(i, columnInfo);
    }

    args.GetReturnValue().Set(columnInfos);
    clearVector(column_infos);
}
