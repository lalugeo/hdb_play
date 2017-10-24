// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "nodever_cover.h"
#include "hana_utils.h"

using namespace v8;
using namespace node;

void getErrorMsg( int           code,
                  int&          errCode,
                  std::string&  errText,
                  std::string&  sqlState )
/********************************************/
{
    errCode = code;
    sqlState = std::string( "HY000" );

    switch( code ) {
	case JS_ERR_INVALID_OBJECT:
	    errText = std::string( "Invalid Object" );
	    break;
	case JS_ERR_INVALID_ARGUMENTS:
            errText = std::string( "Invalid Arguments" );
	    break;
	case JS_ERR_CONNECTION_ALREADY_EXISTS:
            errText = std::string( "Already Connected" );
	    break;
	case JS_ERR_INITIALIZING_DBCAPI:
            errText = std::string( "Can't initialize DBCAPI" );
	    break;
	case JS_ERR_NOT_CONNECTED:
            errText = std::string( "No Connection Available" );
	    break;
	case JS_ERR_BINDING_PARAMETERS:
            errText = std::string( "Can not bind parameter(s)" );
	    break;
	case JS_ERR_GENERAL_ERROR:
            errText = std::string( "An error occurred" );
	    break;
	case JS_ERR_RESULTSET:
            errText = std::string( "Error making result set Object" );
	    break;
        case JS_ERR_TOO_MANY_PARAMETERS:
            errText = std::string("Too many parameters for the SQL statement");
            break;
        case JS_ERR_NOT_ENOUGH_PARAMETERS:
            errText = std::string("Not enough parameters for the SQL statement");
            break;
        case JS_ERR_NO_FETCH_FIRST:
            errText = std::string("ResetSet not fetched");
            break;
        default:
            errText = std::string( "Unknown Error" );
    }
}

void getErrorMsgBindingParam( int&          errCode,
                              std::string&  errText,
                              std::string&  sqlState,
                              int           invalidParam )
/********************************************/
{
    std::ostringstream msg;
    msg << "Can not bind parameter(" << invalidParam << ").";

    errCode = JS_ERR_BINDING_PARAMETERS;
    sqlState = std::string("HY000");
    errText = msg.str();
}

void getErrorMsg( dbcapi_connection*    conn,
                  int&                  errCode,
                  std::string&          errText,
                  std::string&          sqlState )
/*************************************************************/
{
    char bufferErrText[DBCAPI_ERROR_SIZE];
    char bufferSqlState[6];
    api.dbcapi_sqlstate( conn, bufferSqlState, sizeof( bufferSqlState ) );
    errCode = api.dbcapi_error( conn, bufferErrText, sizeof( bufferErrText ) );
    errText = std::string( bufferErrText );
    sqlState = std::string( bufferSqlState );
}

void setErrorMsg( Local<Object>&   error,
                  int              errCode,
                  std::string&     errText,
                  std::string&     sqlState )
/******************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    error->Set( String::NewFromUtf8( isolate, "code" ), Integer::New( isolate, errCode ) );
    error->Set( String::NewFromUtf8( isolate, "message" ), String::NewFromUtf8( isolate, errText.c_str() ) );
    error->Set( String::NewFromUtf8( isolate, "sqlState" ), String::NewFromUtf8( isolate, sqlState.c_str() ) );
}

void throwError( int              errCode,
                 std::string&     errText,
                 std::string&     sqlState )
/******************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    Local<Object> error = Object::New( isolate );
    setErrorMsg( error, errCode, errText, sqlState );

    // Add stack trace
    char buf[2048];
    Local<StackTrace> stackTrace = StackTrace::CurrentStackTrace( isolate, 10, StackTrace::StackTraceOptions::kDetailed );

    std::ostringstream lines;
    lines << "Error: " << errText << std::endl;

    for ( int i = 0; i < stackTrace->GetFrameCount(); i++ ) {
        Local<StackFrame> stackFrame = stackTrace->GetFrame( i );

        int line = stackFrame->GetLineNumber();
        int column = stackFrame->GetColumn();
        int scriptId = stackFrame->GetScriptId();

        Local<String> src = stackFrame->GetScriptNameOrSourceURL();
        src->WriteUtf8(buf);
        std::string strSrc(buf);

        Local<String> script = stackFrame->GetScriptName();
        script->WriteUtf8(buf);
        std::string strScript(buf);

        Local<String> fun = stackFrame->GetFunctionName();
        fun->WriteUtf8(buf);
        std::string strFun(buf);

        lines << "    at " << strScript;
        if (strFun.size() > 0) {
            lines << "." << strFun;
        }
        lines << " (" << strSrc << ":" << std::to_string(line) << ":" << std::to_string(column) << ")" << std::endl;
    }

    error->Set( String::NewFromUtf8(isolate, "stack" ),
               String::NewFromUtf8( isolate, lines.str().c_str() ) );

    isolate->ThrowException( error );
}

void throwError( dbcapi_connection *conn )
/******************************************/
{
    int errCode;
    std::string errText;
    std::string sqlState;

    getErrorMsg( conn, errCode, errText, sqlState );
    throwError( errCode, errText, sqlState );
}

void throwError( int code )
/*************************/
{
    int errCode;
    std::string errText;
    std::string sqlState;

    getErrorMsg( code, errCode, errText, sqlState );
    throwError( errCode, errText, sqlState );
}

void throwErrorIP( int paramIndex,
                   const char *function,
                   const char *expectedType,
                   const char *receivedType )
/*************************/
{
    int errCode = JS_ERR_INVALID_ARGUMENTS;
    std::string sqlState = "HY000";
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    sprintf(buffer, "Invalid parameter %d for function '%s': expected %s, received %s.",
            paramIndex + 1, function, expectedType, receivedType);

    std::string errText = buffer;
    throwError(errCode, errText, sqlState);
}

unsigned int getJSType( Local<Value> value )
{
    if (value->IsUndefined()) {
        return JS_UNDEFINED;
    } else if (value->IsNull()) {
        return JS_NULL;
    } else if (value->IsString()) {
        return JS_STRING;
    } else if (value->IsBoolean()) {
        return JS_BOOLEAN;
    } else if (value->IsInt32()) {
        return JS_INTEGER;
    } else if (value->IsNumber()) {
        return JS_NUMBER;
    } else if (value->IsFunction()) {
        return JS_FUNCTION;
    } else if (value->IsArray()) {
        return JS_ARRAY;
    } else if (Buffer::HasInstance(value)) {
        return JS_BUFFER;
    } else if (value->IsObject()) {
        return JS_OBJECT;
    } else if (value->IsSymbol()) {
        return JS_SYMBOL;
    } else {
        return JS_UNKNOWN_TYPE;
    }
}

std::string getJSTypeName( unsigned int type )
{
    std::string typeName;

    if ((type & JS_UNDEFINED) == JS_UNDEFINED) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "undefined";
    }
    if ((type & JS_NULL) == JS_NULL) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "null";
    }
    if ((type & JS_STRING) == JS_STRING) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "string";
    }
    if ((type & JS_BOOLEAN) == JS_BOOLEAN) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "boolean";
    }
    if ((type & JS_INTEGER) == JS_INTEGER) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "integer";
    }
    if ((type & JS_NUMBER) == JS_NUMBER ) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "number";
    }
    if ((type & JS_FUNCTION) == JS_FUNCTION) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "function";
    }
    if ((type & JS_ARRAY) == JS_ARRAY) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "array";
    }
    if ((type & JS_OBJECT) == JS_OBJECT) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "object";
    }
    if ((type & JS_BUFFER) == JS_BUFFER) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "buffer";
    }
    if ((type & JS_SYMBOL) == JS_SYMBOL) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "symbol";
    }
    if ((type & JS_UNKNOWN_TYPE) == JS_UNKNOWN_TYPE) {
        typeName += (typeName.length() > 0) ? "|" : "";
        typeName += "unknown type";
    }

    return typeName;
}

bool checkParameters( const FunctionCallbackInfo<Value> &args,
                      const char *function,
                      int argCount,
                      unsigned int *expectedType,
                      int *cbFunArg,
                      bool *isOptional,
                      bool *foundOptionalArg )

{
    int argIndex = -1;
    bool boolVal;
    std::string receivedTypeName;

    if (cbFunArg != NULL) {
        *cbFunArg = -1;
    }

    for (int i = 0; i < argCount; i++) {
        argIndex = i;

        if (foundOptionalArg != NULL) {
            foundOptionalArg[i] = false;
        }

        if (i == args.Length()) {
            if (isOptional == NULL || !isOptional[i]) {
                receivedTypeName = getJSTypeName(JS_UNDEFINED);
                break;
            }
        } else {
            unsigned int receivedType = getJSType(args[i]);

            if ((expectedType[i] & receivedType) == receivedType) {
                if (foundOptionalArg != NULL) {
                    foundOptionalArg[i] = true;
                }
            }  else {
                if ((expectedType[i] == JS_BOOLEAN) && convertToBool(args[i], boolVal)) {
                    if (foundOptionalArg != NULL) {
                        foundOptionalArg[i] = true;
                    }
                } else if (!((receivedType == JS_UNDEFINED || receivedType == JS_NULL) &&
                            (isOptional != NULL && isOptional[i]))) {
                    receivedTypeName = getJSTypeName(receivedType);
                    break;
                }
            }

            if ((cbFunArg != NULL) && (receivedType == JS_FUNCTION)) {
                *cbFunArg = i;
            }
        }
    }

    if (receivedTypeName.length() > 0) {
        throwErrorIP(argIndex, function,
                     getJSTypeName(expectedType[argIndex]).c_str(),
                     receivedTypeName.c_str());
        return false;
    }

    return true;
}

void callBack( int                      errCode,
               std::string *		errText,
               std::string *            sqlState,
	       Persistent<Function> &	callback,
	       Local<Value> &		Result,
	       bool			callback_required,
               bool			has_result )
/*********************************************************/
{
/*    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    Local<Function> local_callback = Local<Function>::New( isolate, callback );

    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !local_callback->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}

        Local<Value> Err;
        if (errText == NULL) {
            Err = Local<Value>::New(isolate, Undefined(isolate));
        }
        else {
            Err = Exception::Error(String::NewFromUtf8(isolate, errText->c_str()));
        }

	int argc = 2;
	Local<Value> argv[2] = { Err, Result };

	TryCatch try_catch;
	local_callback->Call( isolate->GetCurrentContext()->Global(), argc, argv );
	if( try_catch.HasCaught()) {
	    node::FatalException( isolate, try_catch );
	}
    } else {
	if( errText != NULL ) {
            throwError( errCode, *errText, *sqlState );
	}
    }
*/

    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    Local<Function> local_callback = Local<Function>::New(isolate, callback);

    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !local_callback->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}

        if (errText == NULL) {
            int argc = (has_result) ? 2 : 1;
            Local<Value> Err = Local<Value>::New(isolate, Undefined(isolate));
	    Local<Value> argv[2] = { Err, Result };

	    TryCatch try_catch;
	    //local_callback->Call( isolate->GetCurrentContext()->Global(), argc, argv );
            MakeCallback(isolate, isolate->GetCurrentContext()->Global(), local_callback, argc, argv);
            if( try_catch.HasCaught()) {
	        node::FatalException( isolate, try_catch );
	    }
        } else {
            int argc = (has_result) ? 2 : 1;
            Local<Object> Err = Object::New( isolate );
            setErrorMsg( Err, errCode, *errText, *sqlState );
            Local<Value> argv[2] = {Err, Object::New(isolate)};

	    TryCatch try_catch;
	    //local_callback->Call( isolate->GetCurrentContext()->Global(), argc, argv );
            MakeCallback(isolate, isolate->GetCurrentContext()->Global(), local_callback, argc, argv);
            if( try_catch.HasCaught()) {
	        node::FatalException( isolate, try_catch );
	    }
        }
    } else {
	if( errText != NULL ) {
            throwError( errCode, *errText, *sqlState );
	}
    }
}

void callBack( int                      errCode,
               std::string *		errText,
               std::string *            sqlState,
               Persistent<Function> &	callback,
	       Persistent<Value> &	Result,
	       bool			callback_required,
               bool			has_result )
/*********************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );
    Local<Value> local_result = Local<Value>::New( isolate, Result );

    callBack( errCode, errText, sqlState, callback, local_result, callback_required, has_result );
}

void callBack( int                      errCode,
               std::string *		errText,
               std::string *            sqlState,
               const Local<Value> &	arg,
	       Local<Value> &		Result,
	       bool			callback_required,
               bool			has_result )
/*********************************************************/
{
/*    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !arg->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}
	Local<Function> callback = Local<Function>::Cast(arg);

        Local<Value> Err;
        if (errText == NULL) {
            Err = Local<Value>::New(isolate, Undefined(isolate));
        }
        else {
            Err = Exception::Error(String::NewFromUtf8(isolate, errText->c_str()));
        }

	int argc = 2;
	Local<Value> argv[2] = { Err,  Result };

	TryCatch try_catch;
	callback->Call( isolate->GetCurrentContext()->Global(), argc, argv );
	if( try_catch.HasCaught()) {
	    node::FatalException( isolate, try_catch );
	}
    } else {
	if( errText != NULL ) {
            throwError( errCode, *errText, *sqlState );
	}
    }
*/

    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    // If string is NULL, then there is no error
    if( callback_required ) {
	if( !arg->IsFunction() ) {
	    throwError( JS_ERR_INVALID_ARGUMENTS );
	    return;
	}

	Local<Function> callback = Local<Function>::Cast(arg);

        if (errText == NULL) {
            int argc = (has_result) ? 2 : 1;
            Local<Value> Err = Local<Value>::New(isolate, Undefined(isolate));
	    Local<Value> argv[2] = { Err,  Result };

	    TryCatch try_catch;
	    //callback->Call(isolate->GetCurrentContext()->Global(), argc, argv);
            MakeCallback(isolate, isolate->GetCurrentContext()->Global(), callback, argc, argv);
	    if( try_catch.HasCaught()) {
	        node::FatalException( isolate, try_catch );
	    }
        } else {
            int argc = (has_result) ? 2 : 1;
            Local<Object> Err = Object::New(isolate);
            setErrorMsg(Err, errCode, *errText, *sqlState);
            Local<Value> argv[2] = {Err, Object::New(isolate)};

            TryCatch try_catch;
            //callback->Call(isolate->GetCurrentContext()->Global(), argc, argv);
            MakeCallback(isolate, isolate->GetCurrentContext()->Global(), callback, argc, argv);
            if (try_catch.HasCaught()) {
                node::FatalException(isolate, try_catch);
            }
        }
    } else {
	if( errText != NULL ) {
            throwError( errCode, *errText, *sqlState );
	}
    }
}

void setReturnValue(const FunctionCallbackInfo<Value> &args,
                    dbcapi_data_value & value,
                    dbcapi_native_type nativeType)
/**********************************************************************/
{
    Isolate *isolate = args.GetIsolate();
    args.GetReturnValue().Set(Undefined(isolate));

    if (value.is_null != NULL && *(value.is_null)) {
        args.GetReturnValue().Set(Null(isolate));
        return;
    }

    int int_number;

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
            if (nativeType == DT_BOOLEAN) {
                args.GetReturnValue().Set(Boolean::New(isolate, int_number > 0 ? true : false));
            } else {
                args.GetReturnValue().Set(Integer::New(isolate, int_number));
            }
            break;
        case A_UVAL32:
        case A_UVAL64:
        case A_VAL64:
            args.GetReturnValue().Set(Number::New(isolate, *(long long*)(value.buffer)));
            break;
        case A_FLOAT:
        case A_DOUBLE:
            args.GetReturnValue().Set(Number::New(isolate, *(double*)(value.buffer)));
            break;
        case A_BINARY: {
                MaybeLocal<Object> mbuf = node::Buffer::Copy(
                    isolate, (char *)value.buffer,
                    (int)*(value.length));
                args.GetReturnValue().Set(mbuf.ToLocalChecked());
            }
            break;
        case A_STRING:
            //args.GetReturnValue().Set(String::NewFromUtf8(isolate,
            //                      (char *)value.buffer,
            //                      NewStringType::kNormal,
            //                      (int)*(value.length)).ToLocalChecked());
            args.GetReturnValue().Set(String::NewFromUtf8(isolate, (char *)value.buffer));
            break;
    }
}

bool checkParameterCount( int &                             errCode,
                          std::string &                     errText,
                          std::string &                     sqlState,
                          std::vector<dbcapi_bind_data*> &  providedParams,
                          dbcapi_stmt *                     dbcapi_stmt_ptr )
/**********************************************************************/
{
    int inputParamCount = 0;
    int numParams = api.dbcapi_num_params(dbcapi_stmt_ptr);

    for (int i = 0; i < numParams; i++) {
        dbcapi_bind_data param;
        api.dbcapi_describe_bind_param(dbcapi_stmt_ptr, i, &param);
        if (param.direction == DD_INPUT || param.direction == DD_INPUT_OUTPUT) {
            inputParamCount++;
        }
    }

    if (inputParamCount < (int) providedParams.size()) {
        getErrorMsg(JS_ERR_TOO_MANY_PARAMETERS, errCode, errText, sqlState);
        return false;
    } else if (inputParamCount > (int) providedParams.size()) {
        getErrorMsg(JS_ERR_NOT_ENOUGH_PARAMETERS, errCode, errText, sqlState);
        return false;
    }

    return true;
}

bool getBindParameters( std::vector<char*> &			string_params,
			std::vector<double*> &			num_params,
			std::vector<int*> &			int_params,
			std::vector<size_t*> &			string_len,
			Handle<Value> 				arg,
			std::vector<dbcapi_bind_data> &	params )
/**********************************************************************/
{
    Handle<Array>	bind_params = Handle<Array>::Cast( arg );
    dbcapi_bind_data 	param;

    for( unsigned int i = 0; i < bind_params->Length(); i++ ) {
	param.value.is_null = NULL;
        param.value.buffer_size = 0;

        if( bind_params->Get(i)->IsInt32() ) {
	    int *param_int = new int;
	    *param_int = bind_params->Get(i)->Int32Value();
	    int_params.push_back( param_int );
            size_t *len = new size_t;
            *len = sizeof(int);
            param.value.length = len;
            param.value.buffer = (char *)( param_int );
	    param.value.type   = A_VAL32;
            param.value.buffer_size = sizeof(int);
	} else if( bind_params->Get(i)->IsNumber() ) {
	    double *param_double = new double;
	    *param_double = bind_params->Get(i)->NumberValue(); // Remove Round off Error
	    num_params.push_back( param_double );
            size_t *len = new size_t;
            *len = sizeof(double);
            param.value.length = len;
            param.value.buffer = (char *)( param_double );
	    param.value.type   = A_DOUBLE;
            param.value.buffer_size = sizeof(double);

	} else if( bind_params->Get(i)->IsString() ) {
	    String::Utf8Value paramValue( bind_params->Get(i)->ToString() );
	    const char* param_string = (*paramValue);
	    size_t *len = new size_t;
	    *len = (size_t)paramValue.length();

	    char *param_char = new char[*len + 1];

	    memcpy( param_char, param_string, ( *len ) + 1 );
	    string_params.push_back( param_char );
	    string_len.push_back( len );

	    param.value.type = A_STRING;
	    param.value.buffer = param_char;
	    param.value.length = len;
	    param.value.buffer_size = *len + 1;

	} else if( Buffer::HasInstance( bind_params->Get(i) ) ) {
	    size_t *len = new size_t;
	    *len = Buffer::Length( bind_params->Get(i) );
	    char *param_char = new char[*len];
	    memcpy( param_char, Buffer::Data( bind_params->Get(i) ), *len );
	    string_params.push_back( param_char );
	    string_len.push_back( len );

	    param.value.type = A_BINARY;
	    param.value.buffer = param_char;
	    param.value.length = len;
	    param.value.buffer_size = sizeof( param_char );

	} else if( bind_params->Get(i)->IsNull() ) {
	    param.value.type = A_VAL32;
	    param.value.is_null = new dbcapi_bool;
	    *param.value.is_null = true;
	} else if (bind_params->Get(i)->IsObject()) { // Length for LOB types
            Local<Object> obj = bind_params->Get(i)->ToObject();
            Local<Array> props = obj->GetOwnPropertyNames();
            std::string lengthKey = "length";
            bool hasLengthProp = false;

            for (unsigned int j = 0; j < props->Length(); j++) {
                Local<String> key = props->Get(j).As<String>();
                Local<String> val = obj->Get(key).As<String>();
                String::Utf8Value key_utf8(key);
                String::Utf8Value val_utf8(val);
                std::string strKey(*key_utf8);
                std::string strVal(*val_utf8);
                if (compareString(strKey, lengthKey, false)) {
                    size_t *len = new size_t;
                    *len = std::stoi(strVal);
                    param.value.length = len;
                    param.value.buffer = NULL;
                    param.value.type = A_INVALID_TYPE; // LOB types
                    param.value.buffer_size = 0;
                    hasLengthProp = true;
                    break;
                }
            }
            if (!hasLengthProp) {
                return false;
            }
        } else {
	    return false;
	}

	params.push_back( param );
    }

    return true;
}

bool getInputParameters( Handle<Value>                     arg,
                         std::vector<dbcapi_bind_data*> &  params,
                         int &                             errCode,
                         std::string &                     errText,
                         std::string &                     sqlState )
/**********************************************************************/
{
    Handle<Array> bind_params = Handle<Array>::Cast(arg);

    params.clear();

    for (int i = 0; i < (int) bind_params->Length(); i++) {
        dbcapi_bind_data* param = getBindParameter(bind_params->Get(i));
        if (param == NULL) {
            getErrorMsgBindingParam(errCode, errText, sqlState, i);
            return false;
        } else {
            params.push_back(param);
        }
    }

    return true;
}

bool getBindParameters(std::vector<dbcapi_bind_data*> &    inputParams,
                       std::vector<dbcapi_bind_data*> &    params,
                       dbcapi_stmt *                       stmt)
 /**********************************************************************/
{
    int numParams = api.dbcapi_num_params(stmt);

    params.clear();

    for (int i = 0; i < numParams; i++) {
        dbcapi_bind_data* param = new dbcapi_bind_data();
        api.dbcapi_describe_bind_param(stmt, i, param);
        param->value.length = new size_t;
        *param->value.length = 0;
        param->value.is_null = new dbcapi_bool;
        *param->value.is_null = false;

        if ( ( param->direction == DD_INPUT || param->direction == DD_INPUT_OUTPUT ) &&
             ( inputParams.size() > 0 ) ) {
            if ( inputParams[0]->value.type == A_STRING && param->value.type == A_VAL32 ) {
                int *pInt = new int;
                *pInt = std::stoi(inputParams[0]->value.buffer);
                param->value.buffer = (char *)(pInt);
                param->value.buffer_size = sizeof(int);
                *param->value.length = sizeof(int);
                params.push_back(param);
                clearParameter(inputParams[0], true);
                inputParams.erase(inputParams.begin());
            } else {
                params.push_back(inputParams[0]);
                inputParams.erase(inputParams.begin());
                clearParameter(param, true);
            }
        } else {
            params.push_back(param);
        }
    }

    return true;
}

// Used for execBatch
bool getBindParameters(Handle<Value>                    arg,
                       int                              row_param_count,
                       std::vector<dbcapi_bind_data*> &	params,
                       std::vector<size_t> &	        buffer_size)
/**********************************************************************/
{
    Handle<Array> bind_params = Handle<Array>::Cast(arg);

    params.clear();
    buffer_size.clear();

    for (int k = 0; k < row_param_count; k++) {
        buffer_size.push_back(0);
    }

    for (unsigned int i = 0; i < bind_params->Length(); i++) {
        Handle<Array> row = Handle<Array>::Cast(bind_params->Get(i));
        for (int j = 0; j < row_param_count; j++) {
            dbcapi_bind_data* 	param = getBindParameter(row->Get(j));

            if (param == NULL) {
                return false;
            }

            if (buffer_size[j] < param->value.buffer_size) {
                buffer_size[j] = param->value.buffer_size;
            }

            params.push_back(param);
        }
    }

    return true;
}

dbcapi_bind_data* getBindParameter( Local<Value> element )
/**********************************************************************/
{
    if (!(element->IsNull() || element->IsInt32() || element->IsNumber() || element->IsBoolean() ||
          element->IsString() || element->IsObject() || Buffer::HasInstance(element))) {
        return NULL;
    }

    dbcapi_bind_data* param = new dbcapi_bind_data();

    memset(param, 0, sizeof(dbcapi_bind_data));
    param->value.length = new size_t;
    *param->value.length = 0;
    param->value.is_null = new dbcapi_bool;
    *param->value.is_null = false;

    if (element->IsNull()) {
        param->value.type = A_VAL32;
        *param->value.is_null = true;
    }
    else if (element->IsBoolean()) {
        int *param_int = new int;
        *param_int = element->BooleanValue() ? 1 : 0;
        param->value.buffer = (char *)(param_int);
        param->value.type = A_VAL32;
        param->value.buffer_size = sizeof(int);
        *param->value.length = sizeof(int);
    }
    else if (element->IsInt32()) {
        int *param_int = new int;
        *param_int = element->Int32Value();
        param->value.buffer = (char *)(param_int);
        param->value.type = A_VAL32;
        param->value.buffer_size = sizeof(int);
        *param->value.length = sizeof(int);
    }
    else if (element->IsNumber()) {
        double *param_double = new double;
        *param_double = element->NumberValue(); // Remove Round off Error
        param->value.buffer = (char *)(param_double);
        param->value.type = A_DOUBLE;
        param->value.buffer_size = sizeof(double);
        *param->value.length = sizeof(double);
    }
    else if (element->IsString()) {
        String::Utf8Value paramValue(element->ToString());
        size_t len = (size_t)paramValue.length();
        char *param_char = new char[len + 1];
        memcpy(param_char, *paramValue, len + 1);
        param->value.type = A_STRING;
        param->value.buffer = param_char;
        *param->value.length = len;
        param->value.buffer_size = len + 1;
    }
    else if (Buffer::HasInstance(element)) {
        size_t len = Buffer::Length(element);
        char *param_char = new char[len];
        memcpy(param_char, Buffer::Data(element), len);
        param->value.type = A_BINARY;
        param->value.buffer = param_char;
        *param->value.length = len;
        param->value.buffer_size = len;
    }
    else if (element->IsObject()) { // Length for LOB types
        Local<Object> obj = element->ToObject();
        Local<Array> props = obj->GetOwnPropertyNames();
        std::string lengthKey = "length";
        bool hasLengthProp = false;

        for (unsigned int j = 0; j < props->Length(); j++) {
            Local<String> key = props->Get(j).As<String>();
            Local<String> val = obj->Get(key).As<String>();
            String::Utf8Value key_utf8(key);
            String::Utf8Value val_utf8(val);
            std::string strKey(*key_utf8);
            std::string strVal(*val_utf8);
            if (compareString(strKey, lengthKey, false)) {
                size_t *len = new size_t;
                *len = std::stoi(strVal);
                param->value.length = len;
                param->value.buffer = NULL;
                param->value.type = A_INVALID_TYPE; // LOB types
                param->value.buffer_size = 0;
                hasLengthProp = true;
                break;
            }
        }
        if (!hasLengthProp) {
            clearParameter(param, true);
            return NULL;
        }
    }

    return param;
}

bool bindParameters(dbcapi_connection *                 conn,
                    dbcapi_stmt *                       stmt,
                    std::vector<dbcapi_bind_data*> &    params,
                    int &                               errCode,
                    std::string &                       errText,
                    std::string &                       sqlState,
                    bool &                              sendParamData)
/*************************************************************************/
{
    sendParamData = false;

    for (int i = 0; i < (int)(params.size()); i++) {
        dbcapi_bind_data param;
        dbcapi_bind_param_info info;

        if (!api.dbcapi_get_bind_param_info(stmt, i, &info) ||
            !api.dbcapi_describe_bind_param(stmt, i, &param)) {
            getErrorMsg(conn, errCode, errText, sqlState);
            return false;
        }

        if (param.direction == DD_OUTPUT || param.direction == DD_INPUT_OUTPUT) {
            if (params[i]->value.buffer == NULL || info.max_size > params[i]->value.buffer_size) {
                size_t size = info.max_size + 1;
                char *bufferNew = new char[size];
                if (params[i]->value.buffer != NULL) {
                    memcpy(bufferNew, params[i]->value.buffer, params[i]->value.buffer_size);
                    delete params[i]->value.buffer;
                }
                params[i]->value.buffer = bufferNew;
                params[i]->value.buffer_size = size;
            }
        }

        if (params[i]->value.type == A_INVALID_TYPE) {
            sendParamData = true;
        } else {
            param.value.type = params[i]->value.type;
        }

        param.value.buffer = params[i]->value.buffer;
        param.value.length = params[i]->value.length;
        param.value.buffer_size = params[i]->value.buffer_size;
        param.value.is_null = params[i]->value.is_null;

        if (!api.dbcapi_bind_param(stmt, i, &param)) {
            getErrorMsg(conn, errCode, errText, sqlState);
            return false;
        }
    }

    return true;
}

void clearParameter(dbcapi_bind_data* param, bool free)
/*************************************************************************/
{
    if (param != NULL) {
        if (param->value.buffer != NULL) {
            delete param->value.buffer;
            param->value.buffer = NULL;
        }
        if (param->value.is_null != NULL) {
            delete param->value.is_null;
            param->value.is_null = NULL;
        }
        if (param->value.length != NULL) {
            delete param->value.length;
            param->value.length = NULL;
        }
        if (free) {
            delete param;
        }
    }
}

void clearParameters(std::vector<dbcapi_bind_data> & params)
/*************************************************************************/
{
    for (size_t i = 0; i < params.size(); i++) {
        clearParameter(&params[i], false);
    }
    params.clear();
}

void clearParameters(std::vector<dbcapi_bind_data*> & params)
/*************************************************************************/
{
    for (size_t i = 0; i < params.size(); i++) {
        clearParameter(params[i], true);
    }
    params.clear();
}

void copyParameters(std::vector<dbcapi_bind_data> & paramsDest,
                    std::vector<dbcapi_bind_data> & paramsSrc)
/*************************************************************************/
{
    clearParameters(paramsDest);

    for (size_t i = 0; i < paramsSrc.size(); i++) {
        paramsDest.push_back(paramsSrc[i]);
        if (paramsSrc[i].value.is_null != NULL) {
            paramsDest[i].value.is_null = new dbcapi_bool;
            *paramsDest[i].value.is_null = *paramsSrc[i].value.is_null;
        }
        if (paramsSrc[i].value.length != NULL) {
            paramsDest[i].value.length = new size_t;
            *paramsDest[i].value.length = *paramsSrc[i].value.length;
        }
        size_t size = paramsSrc[i].value.buffer_size;
        if (size > 0) {
            paramsDest[i].value.buffer = new char[size];
            memcpy(paramsDest[i].value.buffer, paramsSrc[i].value.buffer, size);
        }
    }
}

void copyParameters(std::vector<dbcapi_bind_data> & paramsDest,
                    std::vector<dbcapi_bind_data*> & paramsSrc)
    /*************************************************************************/
{
    clearParameters(paramsDest);

    for (size_t i = 0; i < paramsSrc.size(); i++) {
        paramsDest.push_back(*paramsSrc[i]);
        if (paramsSrc[i]->value.is_null != NULL) {
            paramsDest[i].value.is_null = new dbcapi_bool;
            *paramsDest[i].value.is_null = *paramsSrc[i]->value.is_null;
        }
        if (paramsSrc[i]->value.length != NULL) {
            paramsDest[i].value.length = new size_t;
            *paramsDest[i].value.length = *paramsSrc[i]->value.length;
        }
        size_t size = paramsSrc[i]->value.buffer_size;
        if (size > 0) {
            paramsDest[i].value.buffer = new char[size];
            memcpy(paramsDest[i].value.buffer, paramsSrc[i]->value.buffer, size);
        }
    }
}

bool fillResult(executeBaton *baton, Persistent<Value> &ResultSet)
/*************************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    Local<Value> undef = Local<Value>::New(isolate, Undefined(isolate));

    if (baton->err) {
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required);
        return false;
    }

    if (!getResultSet(ResultSet, baton->rows_affected, baton->col_names,
        baton->string_vals, baton->num_vals, baton->int_vals,
        baton->string_len, baton->col_types, baton->col_native_types)) {
        getErrorMsg(JS_ERR_RESULTSET, baton->error_code, baton->error_msg, baton->sql_state);
        callBack(baton->error_code, &(baton->error_msg), &(baton->sql_state),
                 baton->callback, undef, baton->callback_required);
        return false;
    }
    if (baton->callback_required) {
        // No result for DDL statements
        int hasResult = api.dbcapi_get_function_code(baton->dbcapi_stmt_ptr) != 1;
        callBack(baton->error_code, NULL, &(baton->sql_state),
            baton->callback, ResultSet, baton->callback_required, hasResult);
    }
    return true;
}

bool getResultSet(Persistent<Value> &			Result,
                  int &				        rows_affected,
                  std::vector<char *> &		        colNames,
                  std::vector<char*> &			string_vals,
                  std::vector<double*> &		num_vals,
                  std::vector<int*> &			int_vals,
                  std::vector<size_t*> &		string_len,
                  std::vector<dbcapi_data_type> &	col_types,
                  std::vector<dbcapi_native_type> &     col_native_types )
/*****************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope scope(isolate);
    int 	num_rows = 0;
    size_t	num_cols = colNames.size();

    if (rows_affected >= 0) {
        Result.Reset(isolate, Integer::New(isolate, rows_affected));
        return true;
    }

    if (num_cols > 0) {
        size_t count = 0;
        size_t count_int = 0, count_num = 0, count_string = 0;
        Local<Array> ResultSet = Array::New(isolate);

        std::vector<Local<String>> colNamesLocal;
        for (size_t i = 0; i < num_cols; i++) {
            colNamesLocal.push_back(String::NewFromUtf8(isolate, colNames[i]));
        }

        while (count_int < int_vals.size() ||
               count_num < num_vals.size() ||
               count_string < string_vals.size()) {
            Local<Object> curr_row = Object::New(isolate);
            num_rows++;
            for (size_t i = 0; i < num_cols; i++) {
                switch (col_types[count]) {
                    case A_INVALID_TYPE:
                        curr_row->Set(colNamesLocal[i],
                                      Null(isolate));
                        break;

                    case A_VAL32:
                    case A_VAL16:
                    case A_UVAL16:
                    case A_VAL8:
                    case A_UVAL8:
                        if (int_vals[count_int] == NULL) {
                            curr_row->Set(colNamesLocal[i],
                                          Null(isolate));
                        }
                        else {
                            if (col_native_types[i] == DT_BOOLEAN) {
                                curr_row->Set(colNamesLocal[i], Boolean::New(isolate, *int_vals[count_int] > 0 ? true : false));
                            } else {
                                curr_row->Set(colNamesLocal[i], Integer::New(isolate, *int_vals[count_int]));
                            }
                        }
                        delete int_vals[count_int];
                        int_vals[count_int] = NULL;
                        count_int++;
                        break;

                    case A_UVAL32:
                    case A_UVAL64:
                    case A_VAL64:
                    case A_DOUBLE:
                        if (num_vals[count_num] == NULL) {
                            curr_row->Set(colNamesLocal[i],
                                          Null(isolate));
                        }
                        else {
                            curr_row->Set(colNamesLocal[i],
                                          Number::New(isolate, *num_vals[count_num]));
                        }
                        delete num_vals[count_num];
                        num_vals[count_num] = NULL;
                        count_num++;
                        break;

                    case A_BINARY:
                        if (string_vals[count_string] == NULL) {
                            curr_row->Set(colNamesLocal[i],
                                          Null(isolate));
                        }
                        else {
                            MaybeLocal<Object> mbuf = node::Buffer::Copy(
                                isolate, string_vals[count_string],
                                *string_len[count_string]);
                            Local<Object> buf = mbuf.ToLocalChecked();
                            curr_row->Set(colNamesLocal[i],
                                          buf);
                        }
                        delete string_vals[count_string];
                        delete string_len[count_string];
                        string_vals[count_string] = NULL;
                        string_len[count_string] = NULL;
                        count_string++;
                        break;

                    case A_STRING:
                        if (string_vals[count_string] == NULL) {
                            curr_row->Set(colNamesLocal[i],
                                          Null(isolate));
                        }
                        else {
                            curr_row->Set(colNamesLocal[i],
                                          String::NewFromUtf8(isolate,
                                          string_vals[count_string],
                                          NewStringType::kNormal,
                                          *((int*)string_len[count_string])).ToLocalChecked()
                            );
                        }
                        delete string_vals[count_string];
                        delete string_len[count_string];
                        string_vals[count_string] = NULL;
                        string_len[count_string] = NULL;
                        count_string++;
                        break;

                    default:
                        return false;
                }
                count++;
            }
            ResultSet->Set(num_rows - 1, curr_row);
        }
        Result.Reset(isolate, ResultSet);
    } else {
        Result.Reset(isolate, Local<Value>::New(isolate,
                     Undefined(isolate)));
    }

    return true;
}

void clearValues(std::vector<dbcapi_data_value*> & values)
/*****************************************************************/
{
}

#define DEFAULT_ROWSET_SIZE 10

bool fetchResultSet( dbcapi_stmt *			dbcapi_stmt_ptr,
		     int &				rows_affected,
		     std::vector<char *> &		colNames,
		     std::vector<char*> &		string_vals,
		     std::vector<double*> &		num_vals,
		     std::vector<int*> &		int_vals,
		     std::vector<size_t*> &		string_len,
		     std::vector<dbcapi_data_type> &	col_types,
                     std::vector<dbcapi_native_type> &  col_native_types )
/*****************************************************************/
{
    dbcapi_data_value		value;
    int				num_cols = 0;

    rows_affected = api.dbcapi_affected_rows( dbcapi_stmt_ptr );
    num_cols = api.dbcapi_num_cols( dbcapi_stmt_ptr );

    if( rows_affected > 0 && num_cols < 1 ) {
        return true;
    }

    rows_affected = -1;
    if (num_cols > 0) {
        bool has_lob = false;
        int rowset_size = DEFAULT_ROWSET_SIZE;
        dataValueCollection bind_cols;

        for (int i = 0; i < num_cols; i++) {
            dbcapi_column_info info;
            api.dbcapi_get_column_info(dbcapi_stmt_ptr, i, &info);
            size_t size = strlen(info.name) + 1;
            char *name = new char[size];
            memcpy(name, info.name, size);
            colNames.push_back(name);
            col_native_types.push_back(info.native_type);

            if (info.native_type == DT_BLOB || info.native_type == DT_CLOB || info.native_type == DT_NCLOB) {
                has_lob = true;
            }

            if (!has_lob) {
                dbcapi_data_value* bind_col = new dbcapi_data_value();
                bind_col->buffer_size = info.max_size;
                bind_col->buffer = new char[info.max_size * rowset_size];
                bind_col->length = new size_t[rowset_size];
                bind_col->is_null = new dbcapi_bool[rowset_size];
                bind_col->type = info.type;
                bind_cols.push_back(bind_col);
            }
        }

        if (!has_lob) {

            if (!api.dbcapi_set_rowset_size(dbcapi_stmt_ptr, rowset_size)) {
                return false;
            }

            for (int i = 0; i < num_cols; i++) {
                if (!api.dbcapi_bind_column(dbcapi_stmt_ptr, i, bind_cols[i])) {
                    return false;
                }
            }
        }

        int count_string = 0, count_num = 0, count_int = 0;
        while (api.dbcapi_fetch_next(dbcapi_stmt_ptr)) {

            int fetched_rows = api.dbcapi_fetched_rows(dbcapi_stmt_ptr);

            for (int row = 0; row < fetched_rows; row++) {

                for (int i = 0; i < num_cols; i++) {
                    if (has_lob) {
                        if (!api.dbcapi_get_column(dbcapi_stmt_ptr, i, &value)) {
                            return false;
                        }
                    }
                    else {
                        value.buffer = bind_cols[i]->buffer + row * bind_cols[i]->buffer_size;
                        value.buffer_size = bind_cols[i]->buffer_size;
                        value.type = bind_cols[i]->type;
                        value.length = bind_cols[i]->length + row;
                        value.is_null = bind_cols[i]->is_null + row;
                    }

                    if (*(value.is_null)) {
                        col_types.push_back(A_INVALID_TYPE);
                        continue;
                    }

                    switch (value.type) {
                        case A_BINARY:
                        {
                            size_t *size = new size_t;
                            *size = *(value.length);
                            char *val = new char[*size];
                            memcpy(val, value.buffer, *size);
                            string_vals.push_back(val);
                            string_len.push_back(size);
                            count_string++;
                            break;
                        }

                        case A_STRING:
                        {
                            size_t *size = new size_t;
                            *size = (size_t)((int)*(value.length));
                            char *val = new char[*size];
                            memcpy(val, (char *)value.buffer, *size);
                            string_vals.push_back(val);
                            string_len.push_back(size);
                            count_string++;
                            break;
                        }

                        case A_VAL64:
                        {
                            double *val = new double;
                            *val = (double)*(long long *)value.buffer;
                            num_vals.push_back(val);
                            count_num++;
                            break;
                        }

                        case A_UVAL64:
                        {
                            double *val = new double;
                            *val = (double)*(unsigned long long *)value.buffer;
                            num_vals.push_back(val);
                            count_num++;
                            break;
                        }

                        case A_VAL32:
                        {
                            int *val = new int;
                            *val = *(int*)value.buffer;
                            int_vals.push_back(val);
                            count_int++;
                            break;
                        }

                        case A_UVAL32:
                        {
                            double *val = new double;
                            *val = (double)*(unsigned int*)value.buffer;
                            num_vals.push_back(val);
                            count_num++;
                            break;
                        }

                        case A_VAL16:
                        {
                            int *val = new int;
                            *val = (int)*(short*)value.buffer;
                            int_vals.push_back(val);
                            count_int++;
                            break;
                        }

                        case A_UVAL16:
                        {
                            int *val = new int;
                            *val = (int)*(unsigned short*)value.buffer;
                            int_vals.push_back(val);
                            count_int++;
                            break;
                        }

                        case A_VAL8:
                        {
                            int *val = new int;
                            *val = (int)*(char *)value.buffer;
                            int_vals.push_back(val);
                            count_int++;
                            break;
                        }

                        case A_UVAL8:
                        {
                            int *val = new int;
                            *val = (int)*(unsigned char *)value.buffer;
                            int_vals.push_back(val);
                            count_int++;
                            break;
                        }

                        case A_DOUBLE:
                        {
                            double *val = new double;
                            *val = (double)*(double *)value.buffer;
                            num_vals.push_back(val);
                            count_num++;
                            break;
                        }

                        default:
                            return false;
                    }
                    col_types.push_back(value.type);
                }
            }
        }
    }

    return true;
}

bool compareString( const std::string &str1, const std::string &str2, bool caseSensitive )
/*************************************************************/
{
    std::locale loc;

    if( str1.length() == str2.length() ) {
        for( size_t i = 0; i < str1.length(); i++ ) {
            if( caseSensitive && ( str1[i] != str2[i] ) ) {
                return false;
            } else if( !caseSensitive && ( std::tolower( str1[i], loc ) != std::tolower( str2[i], loc) ) ) {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool compareString(const std::string &str1, const char* str2, bool caseSensitive)
/*************************************************************/
{
    return compareString( str1, std::string( str2 ), caseSensitive );
}

// Connection Functions

void hashToString( Local<Object> obj, Persistent<String> &ret )
/*************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();
    HandleScope	scope(isolate);
    Local<Array> props = obj->GetOwnPropertyNames();
    int length = props->Length();
    std::string params = "";
    std::string hostKey = "host";
    std::string hostVal = "";
    std::string portKey = "port";
    std::string portVal = "";
    bool	first = true;

    for( int i = 0; i < length; i++ ) {
	Local<String> key = props->Get(i).As<String>();
	Local<String> val = obj->Get(key).As<String>();
	String::Utf8Value key_utf8( key );
	String::Utf8Value val_utf8( val );
        std::string strKey( *key_utf8 );
        std::string strVal( *val_utf8 );

        // Check host and port
        if( compareString( strKey, hostKey, false )) {
            hostVal = strVal;
            continue;
        } else if( compareString( strKey, portKey, false )) {
            portVal = strVal;
            continue;
        }

        // Check other keys
        if( compareString( strKey, "user", false ) || compareString( strKey, "username", false ) ) {
            strKey = std::string( "uid" );
        } else if ( compareString( strKey, "password", false ) ) {
            strKey = std::string( "pwd" );
        }

        if( !first ) {
	    params += ";";
	}
	first = false;
	params += strKey;
	params += "=";
	params += strVal;
    }

    if( hostVal.length() > 0 ) {
        if( params.length() > 0 ) {
            params += ";";
        }
        params += "ServerNode=";
        params += hostVal;
        if( portVal.length() > 0) {
            params += ":";
            params += portVal;
        }
    }

    ret.Reset( isolate, String::NewFromUtf8( isolate, params.c_str() ) );
}

#if 0
// Handy function for determining what type an object is.
static void CheckArgType( Local<Value> &obj )
/*******************************************/
{
    static const char *type = NULL;
    if( obj->IsArray() ) {
	type = "Array";
    } else if( obj->IsBoolean() ) {
	type = "Boolean";
    } else if( obj->IsBooleanObject() ) {
	type = "BooleanObject";
    } else if( obj->IsDate() ) {
	type = "Date";
    } else if( obj->IsExternal() ) {
	type = "External";
    } else if( obj->IsFunction() ) {
	type = "Function";
    } else if( obj->IsInt32() ) {
	type = "Int32";
    } else if( obj->IsNativeError() ) {
	type = "NativeError";
    } else if( obj->IsNull() ) {
	type = "Null";
    } else if( obj->IsNumber() ) {
	type = "Number";
    } else if( obj->IsNumberObject() ) {
	type = "NumberObject";
    } else if( obj->IsObject() ) {
	type = "Object";
    } else if( obj->IsRegExp() ) {
	type = "RegExp";
    } else if( obj->IsString() ) {
	type = "String";
    } else if( obj->IsStringObject() ) {
	type = "StringObject";
    } else if( obj->IsUint32() ) {
	type = "Uint32";
    } else if( obj->IsUndefined() ) {
	type = "Undefined";
    } else {
	type = "Unknown";
    }
}
#endif

bool StringtoDate(const char *str, Local<Value> &val, dbcapi_connection *conn)
/********************************************************************************/
{
    Isolate *isolate = Isolate::GetCurrent();

    dbcapi_data_value value;
    std::string reformat_stmt = "SELECT DATEFORMAT( '";
    reformat_stmt.append(str);
    reformat_stmt.append("', 'YYYY-MM-DD HH:NN:SS.SSS' ) ");

    dbcapi_stmt *stmt = api.dbcapi_execute_direct(conn, reformat_stmt.c_str());
    if (stmt == NULL) {
        throwError(conn);
        return false;
    }
    if (!api.dbcapi_fetch_next(stmt)) {
        api.dbcapi_free_stmt(stmt);
        throwError(conn);
        return false;
    }
    if (!api.dbcapi_get_column(stmt, 0, &value)) {
        api.dbcapi_free_stmt(stmt);
        throwError(conn);
        return false;
    }

    std::ostringstream out;
    if (!convertToString(value, out)) {
        return false;
    }

    api.dbcapi_free_stmt(stmt);
    std::string tempstr;

    std::stringstream ss;
    ss << out.str().substr(0, 4) << ' '
        << out.str().substr(5, 2) << ' '
        << out.str().substr(8, 2) << ' '
        << out.str().substr(11, 2) << ' '
        << out.str().substr(14, 2) << ' '
        << out.str().substr(17, 2) << ' '
        << out.str().substr(20, 2);

    unsigned int year, month, day, hour, min, sec, millisec;

    ss >> year >> month >> day >> hour >> min >> sec >> millisec;

    Local<Object> obj = Local<Object>::Cast(val);
    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setDate"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, day)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }

    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setMonth"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, month - 1)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }

    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setFullYear"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, year)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }

    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setMilliseconds"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, millisec)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }

    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setSeconds"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, sec)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }

    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setMinutes"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, min)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }

    {
        Local<Value> set_prop = Local<Object>::Cast(val)->Get(
            String::NewFromUtf8(isolate, "setHours"));
        Local<Value> argv[1] = { Local<Value>::New(isolate,
                                                   Number::New(isolate, hour)) };
        Local<Function> set_func = Local<Function>::Cast(set_prop);
        set_func->Call(obj, 1, argv);
    }
    return true;
}

bool convertToBool(Local<Value> val, bool &out)
{
    if (val->IsBoolean()) {
        out = val->BooleanValue();
        return true;
    } else if (val->IsString()) {
        std::string str = convertToString(val);
        if (compareString(str, "true", false) ||
            compareString(str, "on", false) ||
            compareString(str, "1", false)) {
            out = true;
            return true;
        } else if (compareString(str, "false", false) ||
                   compareString(str, "off", false) ||
                   compareString(str, "0", false)) {
            out = false;
            return true;
        }
    }
    return false;
}

bool convertToString(dbcapi_data_value	        value,
                     std::ostringstream &	out,
                     bool			do_throw_error)
/**************************************************************/
{
    switch (value.type) {
        case A_BINARY:
            out.write((char *)value.buffer, (int)*(value.length));
            break;
        case A_STRING:
            out.write((char *)value.buffer, (int)*(value.length));
            break;
        case A_VAL64:
            out << (double)*(long long *)value.buffer;
            break;
        case A_UVAL64:
            out << (double)*(unsigned long long *)value.buffer;
            break;
        case A_VAL32:
            out << *(int*)value.buffer;
            break;
        case A_UVAL32:
            out << *(unsigned int*)value.buffer;
            break;
        case A_VAL16:
            out << *(short*)value.buffer;
            break;
        case A_UVAL16:
            out << *(unsigned short*)value.buffer;
            break;
        case A_VAL8:
            out << *(char *)value.buffer;
            break;
        case A_UVAL8:
            out << *(unsigned char *)value.buffer;
            break;
        case A_DOUBLE:
            out << *(double *)value.buffer;
            break;
        default:
            if (do_throw_error) {
                throwError(JS_ERR_RETRIEVING_DATA);  // Can't retrieve data
            }
            return false;
            break;
    }
    return true;
}

bool convertToDouble(dbcapi_data_value	        value,
                     double &			number,
                     bool			do_throw_error)
/**************************************************************/
{
    switch (value.type) {
        case A_VAL64:
            number = (double)*(long long *)value.buffer;
            break;
        case A_UVAL64:
            number = (double)*(unsigned long long *)value.buffer;
            break;
        case A_VAL32:
            number = *(int*)value.buffer;
            break;
        case A_UVAL32:
            number = *(unsigned int*)value.buffer;
            break;
        case A_VAL16:
            number = *(short*)value.buffer;
            break;
        case A_UVAL16:
            number = *(unsigned short*)value.buffer;
            break;
        case A_VAL8:
            number = *(char *)value.buffer;
            break;
        case A_UVAL8:
            number = *(unsigned char *)value.buffer;
            break;
        case A_DOUBLE:
            number = *(double *)value.buffer;
            break;
        case A_STRING:
        {
            size_t	len = *value.length;
            char *strval = (char *)malloc(len + 1);
            if (strval != NULL) {
                memcpy(strval, value.buffer, len);
                strval[len] = '\0';
                number = atof(strval);
                free(strval);
                break;
            } // else fall through
        }
        default:
            if (do_throw_error) {
                throwError(JS_ERR_RETRIEVING_DATA);  // Can't retrieve data
            }
            return false;
            break;
    }

    return true;
}

bool convertToInt(dbcapi_data_value value, int &number, bool do_throw_error)
/******************************************************************************/
{
    switch (value.type) {
        case A_VAL32:
            number = *(int*)value.buffer;
            break;
        case A_VAL16:
            number = *(short*)value.buffer;
            break;
        case A_UVAL16:
            number = *(unsigned short*)value.buffer;
            break;
        case A_VAL8:
            number = *(char *)value.buffer;
            break;
        case A_UVAL8:
            number = *(unsigned char *)value.buffer;
            break;
        case A_STRING:
        {
            size_t	len = *value.length;
            char *strval = (char *)malloc(len + 1);
            if (strval != NULL) {
                memcpy(strval, value.buffer, len);
                strval[len] = '\0';
                number = atoi(strval);
                free(strval);
                break;
            } // else fall through
        }
        default:
            if (do_throw_error) {
                throwError(JS_ERR_RETRIEVING_DATA);  // Can't retrieve data
            }
            return false;
    }

    return true;
}

const char* getTypeName(dbcapi_data_type type)
/******************************************************************************/
{
    switch (type) {
        case A_INVALID_TYPE: return "INVALID_TYPE";
        case A_BINARY:       return "BINARY";
        case A_STRING:       return "STRING";
        case A_DOUBLE:       return "DOUBLE";
        case A_VAL64:        return "VAL64";
        case A_UVAL64:       return "UVAL64";
        case A_VAL32:        return "VAL32";
        case A_UVAL32:       return "UVAL32";
        case A_VAL16:        return "VAL16";
        case A_UVAL16:       return "UVAL16";
        case A_VAL8:         return "VAL8";
        case A_UVAL8:        return "UVAL8";
        case A_FLOAT:        return "FLOAT";
    }
    return "UNKNOWN";
}

const char* getNativeTypeName(dbcapi_native_type nativeType)
/******************************************************************************/
{
    switch (nativeType) {
        case DT_NULL:                return "NULL";
        case DT_TINYINT:             return "TINYINT";
        case DT_SMALLINT:            return "SMALLINT";
        case DT_INT:                 return "INT";
        case DT_BIGINT:              return "BIGINT";
        case DT_DECIMAL:             return "DECIMAL";
        case DT_REAL:                return "REAL";
        case DT_DOUBLE:              return "DOUBLE";
        case DT_CHAR:                return "CHAR";
        case DT_VARCHAR1:            return "VARCHAR1";
        case DT_NCHAR:               return "NCHAR";
        case DT_NVARCHAR:            return "NVARCHAR";
        case DT_BINARY:              return "BINARY";
        case DT_VARBINARY:           return "VARBINARY";
        case DT_DATE:                return "DATE";
        case DT_TIME:                return "TIME";
        case DT_TIMESTAMP:           return "TIMESTAMP";
        case DT_TIME_TZ:             return "TIME_TZ";
        case DT_TIME_LTZ:            return "TIME_LTZ";
        case DT_TIMESTAMP_TZ:        return "TIMESTAMP_TZ";
        case DT_TIMESTAMP_LTZ:       return "TIMESTAMP_LTZ";
        case DT_INTERVAL_YM:         return "INTERVAL_YM";
        case DT_INTERVAL_DS:         return "INTERVAL_DS";
        case DT_ROWID:               return "ROWID";
        case DT_UROWID:              return "UROWID";
        case DT_CLOB:                return "CLOB";
        case DT_NCLOB:               return "NCLOB";
        case DT_BLOB:                return "BLOB";
        case DT_BOOLEAN:             return "BOOLEAN";
        case DT_STRING:              return "STRING";
        case DT_NSTRING:             return "NSTRING";
        case DT_LOCATOR:             return "LOCATOR";
        case DT_NLOCATOR:            return "NLOCATOR";
        case DT_BSTRING:             return "BSTRING";
        case DT_DECIMAL_DIGIT_ARRAY: return "DECIMAL_DIGIT_ARRAY";
        case DT_VARCHAR2:            return "VARCHAR2";
        case DT_TABLE:               return "TABLE";
        case DT_ABAPSTREAM:          return "ABAPSTREAM";
        case DT_ABAPSTRUCT:          return "ABAPSTRUCT";
        case DT_ARRAY:               return "ARRAY";
        case DT_TEXT:                return "TEXT";
        case DT_SHORTTEXT:           return "SHORTTEXT";
        case DT_BINTEXT:             return "BINTEXT";
        case DT_ALPHANUM:            return "ALPHANUM";
        case DT_LONGDATE:            return "LONGDATE";
        case DT_SECONDDATE:          return "SECONDDATE";
        case DT_DAYDATE:             return "DAYDATE";
        case DT_SECONDTIME:          return "SECONDTIME";
        case DT_CLOCATOR:            return "CLOCATOR";
        case DT_BLOB_DISK_RESERVED:  return "BLOB_DISK_RESERVED";
        case DT_CLOB_DISK_RESERVED:  return "CLOB_DISK_RESERVED";
        case DT_NCLOB_DISK_RESERVE:  return "NCLOB_DISK_RESERVE";
        case DT_ST_GEOMETRY:         return "ST_GEOMETRY";
        case DT_ST_POINT:            return "ST_POINT";
        case DT_FIXED16:             return "FIXED16";
        case DT_ABAP_ITAB:           return "ABAP_ITAB";
        case DT_RECORD_ROW_STORE:    return "RECORD_ROW_STORE";
        case DT_RECORD_COLUMN_STORE: return "RECORD_COLUMN_STORE";
        case DT_NOTYPE:              return "NOTYPE";
    }
    return "UNKNOWN";
}

void setColumnInfo(Isolate*             isolate,
                   Local<Object>&       object,
                   dbcapi_column_info*  columnInfo)
/******************************************************************************/
{
    object->Set(String::NewFromUtf8(isolate, "columnName"),
                String::NewFromUtf8(isolate, columnInfo->name));
    object->Set(String::NewFromUtf8(isolate, "tableName"),
                String::NewFromUtf8(isolate, columnInfo->table_name));
    object->Set(String::NewFromUtf8(isolate, "ownerName"),
                String::NewFromUtf8(isolate, columnInfo->owner_name));
    object->Set(String::NewFromUtf8(isolate, "type"),
                Integer::New(isolate, columnInfo->type));
    object->Set(String::NewFromUtf8(isolate, "typeName"),
                String::NewFromUtf8(isolate, getTypeName(columnInfo->type)));
    object->Set(String::NewFromUtf8(isolate, "nativeType"),
                Integer::New(isolate, columnInfo->native_type));
    object->Set(String::NewFromUtf8(isolate, "nativeTypeName"),
                String::NewFromUtf8(isolate, getNativeTypeName(columnInfo->native_type)));
    object->Set(String::NewFromUtf8(isolate, "precision"),
                Integer::NewFromUnsigned(isolate, columnInfo->precision));
    object->Set(String::NewFromUtf8(isolate, "scale"),
                Integer::NewFromUnsigned(isolate, columnInfo->scale));
    object->Set(String::NewFromUtf8(isolate, "nullable"),
                Integer::NewFromUnsigned(isolate, columnInfo->nullable ? 1 : 0));
}

int fetchColumnInfos(dbcapi_stmt* dbcapi_stmt_ptr,
                     std::vector<dbcapi_column_info*>& column_infos)
/*****************************************/
{
    clearVector<dbcapi_column_info>(column_infos);

    int num_cols = api.dbcapi_num_cols(dbcapi_stmt_ptr);
    if (num_cols > 0) {
        for (int i = 0; i < num_cols; i++) {
            dbcapi_column_info* info = new dbcapi_column_info();
            api.dbcapi_get_column_info(dbcapi_stmt_ptr, i, info);
            column_infos.push_back(info);
        }
    }

    return num_cols;
}