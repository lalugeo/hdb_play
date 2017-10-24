// ***************************************************************************
// Copyright (c) 2016 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
using namespace v8;

#include "nodever_cover.h"

struct executeBaton;

/** Represents prepared statement
 * @class Statement
 *
 * The Statement object is for SQL statements that will be executed multiple
 * times.
 * @see Connection::prepare
 */
class Statement : public node::ObjectWrap
{
  public:
    /// @internal
    static void Init( Isolate * );

    /// @internal
    static NODE_API_FUNC( NewInstance );

    /// @internal
    static void CreateNewInstance( const FunctionCallbackInfo<Value> &args,
				   Persistent<Object> &obj );

    /// @internal
    Statement();
    /// @internal
    ~Statement();

  private:
    /// @internal
    static Persistent<Function> constructor;
    /// @internal
    static NODE_API_FUNC( New );

    /** This method checks if the statement is valid.
    *
    * @fn Boolean Statement::isValid()
    *
    * @return Returns true if valid, false otherwise(already dropped). ( type: Boolean )
    */
    static NODE_API_FUNC(isValid);

    /** Executes the prepared SQL statement.
     *
     * This method optionally takes in an array of bind
     * parameters to execute.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err, result )
     * {
     *
     * };
     * </pre></p>
     *
     * For queries producing result sets, the result set object is returned
     * as the second parameter of the callback.
     * For insert, update and delete statements, the number of rows affected
     * is returned as the second parameter of the callback.
     * For other statements, result is undefined.
     *
     * The following synchronous example shows how to use the exec method
     * on a prepared statement.
     *
     * <p><pre>
     * var hana = require( '@sap/hana-client' );
     * var client = hana.createConnection();
     * client.connect( "serverNode=myserver;uid=system;pwd=manager" )
     * stmt = client.prepare( "SELECT * FROM Customers WHERE ID >= ? AND ID < ?" );
     * result = stmt.exec( [200, 300] );
     * stmt.drop();
     * console.log( result );
     * client.disconnect();
     * </pre></p>
     *
     * @fn result Statement::exec( Array params, Function callback )
     *
     * @param params The optional array of bind parameters.
     * @param callback The optional callback function.
     *
     * @return If no callback is specified, the result is returned.
     *
     */
    static NODE_API_FUNC(exec);

    /** Executes the prepared SQL statement and returns result set object.
    *
    * This method optionally takes in an array of bind
    * parameters to execute.
    *
    * This method can be either synchronous or asynchronous depending on
    * whether or not a callback function is specified.
    * The callback function is of the form:
    *
    * <p><pre>
    * function( err, result )
    * {
    *
    * };
    * </pre></p>
    *
    * The result set object is returned as the second parameter of the callback.
    *
    * The following synchronous example shows how to use the exec method
    * on a prepared statement.
    *
    * <p><pre>
    * var hana = require( '@sap/hana-client' );
    * var client = hana.createConnection();
    * client.connect( "serverNode=myserver;uid=system;pwd=manager" )
    * stmt = client.prepare( "SELECT * FROM Customers WHERE ID >= ? AND ID < ?" );
    * result = stmt.execQuery( [200, 300] );
    * stmt.drop();
    * console.log( result );
    * client.disconnect();
    * </pre></p>
    *
    * @fn result Statement::execQuery( Array params, Function callback )
    *
    * @param params The optional array of bind parameters.
    * @param callback The optional callback function.
    *
    * @return If no callback is specified, the result set is returned. ( type: ResultSet )
    *
    */
    static NODE_API_FUNC(execQuery);

    /** Submits the command for batch execution and if the command
    * executes successfully, returns the number of rows affected.
    *
    * This method takes in an array of bind parameters to execute.
    *
    * This method can be either synchronous or asynchronous depending on
    * whether or not a callback function is specified.
    * The callback function is of the form:
    *
    * <p><pre>
    * function( err, result )
    * {
    *
    * };
    * </pre></p>
    *
    * The result is number of rows affected.
    *
    * The following synchronous example shows how to use the exec method
    * on a prepared statement.
    *
    * <p><pre>
    * var hana = require( '@sap/hana-client' );
    * var client = hana.createConnection();
    * client.connect( "serverNode=myserver;uid=system;pwd=manager" )
    * stmt = client.prepare( "INSERT INTO Customers(ID, NAME) VALUES(?, ?)" );
    * result = stmt.execBatch( [[1, 'Company 1'], [2, 'Company 2']] );
    * stmt.drop();
    * console.log( result );
    * client.disconnect();
    * </pre></p>
    *
    * @fn result Statement::execBatch( Array params, Function callback )
    *
    * @param params The array of bind parameters.
    * @param callback The optional callback function.
    *
    * @return If no callback is specified, the number of rows affected is returned. ( type: Integer )
    *
    */
    static NODE_API_FUNC(execBatch);

    /** Drops the statement.
     *
     * This method drops the prepared statement and frees up resources.
     *
     * This method can be either synchronous or asynchronous depending on
     * whether or not a callback function is specified.
     * The callback function is of the form:
     *
     * <p><pre>
     * function( err )
     * {
     *
     * };
     * </pre></p>
     *
     * The following synchronous example shows how to use the drop method
     * on a prepared statement.
     *
     * <p><pre>
     * var hana = require( '@sap/hana-client' );
     * var client = hana.createConnection();
     * client.connect( "serverNode=myserver;uid=system;pwd=manager" )
     * stmt = client.prepare( "SELECT * FROM Customers WHERE ID >= ? AND ID < ?" );
     * result = stmt.exec( [200, 300] );
     * stmt.drop();
     * console.log( result );
     * client.disconnect();
     * </pre></p>
     *
     * @fn Statement::drop( Function callback )
     *
     * @param callback The optional callback function.
     *
     */
    static NODE_API_FUNC( drop );

    /** Gets the the parameter information.
    *
    * @fn Array Statement::getParameterInfo()
    *
    * @return Returns an array of Objects. Each Object contains properties of each parameter. ( type: Array )
    *
    */
    static NODE_API_FUNC(getParameterInfo);

    /** Gets the value of the specified parameter.
    *
    * This method can be used to retrieve output parameter values.
    *
    * @fn Object Statement::getParameterValue( Integer paramIndex )
    *
    * @param paramIndex The zero-based index of the parameter. ( type: Integer )
    *
    * @return Returns the value of the parameter. ( type: Object )
    *
    */
    static NODE_API_FUNC(getParameterValue);

    /** Sends data as part of a parameter.
    *
    * This method can be used to send a large amount of data for a parameter in chunks.
    *
    * @fn Object Statement::sendParameterData( Integer paramIndex, Buffer buffer, Function callback )
    *
    * @param paramIndex The zero-based index of the parameter. ( type: Integer )
    *
    * @param buffer The data to be sent. ( type: Buffer )
    *
    * @param callback The optional callback function.
    *
    */
    static NODE_API_FUNC(sendParameterData);

    /** Returns the function code of the statement.
    *
    * @fn Integer ResultSet::functionCode()
    *
    * @return Returns the function code of the statement. ( type: Integer )
    *
    */
    static NODE_API_FUNC(functionCode);

    /** Gets the the column information.
    *
    * @fn Array Statement::getColumnInfo()
    *
    * @return Returns an Array of Objects. Each Object contains properties of each column. ( type: Array )
    *
    */
    static NODE_API_FUNC(getColumnInfo);

    /// @internal
    static void executeQueryWork(uv_work_t *req);
    /// @internal
    static void executeQueryAfter(uv_work_t *req);

    /// @internal
    static void executeBatchWork(uv_work_t *req);
    /// @internal
    static void executeBatchAfter(uv_work_t *req);

    /// @internal
    static void dropAfter(uv_work_t *req);
    /// @internal
    static void dropWork(uv_work_t *req);

    /// @internal
    static void sendParameterDataAfter(uv_work_t *req);
    /// @internal
    static void sendParameterDataWork(uv_work_t *req);

    /// @internal
    static bool checkStatement(Statement *obj,
                               const FunctionCallbackInfo<Value> &args,
                               int cbfunc_arg,
                               bool callback_required);

    /// @internal
    static bool checkParameterIndex(Statement *obj,
                                    const FunctionCallbackInfo<Value> &args,
                                    int &paramIndex);

    /// @internal
    static bool checkExecParameters(const FunctionCallbackInfo<Value> &args,
                             const char *function,
                             bool &bind_required,
                             int &cbfunc_arg);

  public:
    /// @internal
    Connection		*connection;
    /// @internal
    dbcapi_stmt         *dbcapi_stmt_ptr;
    /// @internal
    int	num_params;
    /// @internal
    std::vector<dbcapi_bind_data> params;
    /// @internal
    executeBaton        *execBaton;
    /// @internal
    uv_mutex_t          *conn_mutex;
    /// @internal
    std::string         sql;
    /// @internal
    std::vector<dbcapi_bind_param_info> param_infos;
    /// @internal
    bool                is_dropped;
};
