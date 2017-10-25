#include <stdio.h>
#include <stdlib.h>
#include "/hana/shared/LT1/hdbclient/sdk/odbc/incl/sql.h"
#include "/hana/shared/LT1/hdbclient/sdk/odbc/incl/sqlext.h"
#include "/hana/shared/LT1/hdbclient/sdk/odbc/incl/sqltypes.h"
#include <time.h>
#include <pthread.h>
/*
int main() {
 
    SQLHENV env;
    SQLHDBC dbc;
    long    res;
 
    // Environment
                // Allocation
        SQLAllocHandle( SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
 
               // ODBC: Version: Set
        SQLSetEnvAttr( env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
 
    // DBC: Allocate
        SQLAllocHandle( SQL_HANDLE_DBC, env, &dbc);
 
    // DBC: Connect
        res = SQLConnect( dbc, (SQLCHAR*) "HANADB1", SQL_NTS,
                               (SQLCHAR*) "AFL_ACCESS", SQL_NTS,
                               (SQLCHAR*) "Initial123", SQL_NTS);

	 
        printf("RES: %i \n", res);
 
    //
        SQLDisconnect( dbc );
        SQLFreeHandle( SQL_HANDLE_DBC, dbc );
        SQLFreeHandle( SQL_HANDLE_ENV, env );
 
    printf("\n");
    return 0;
}

*/


SQLHENV			V_OD_Env;			// Handle ODBC environment
long			V_OD_erg;			// result of functions
SQLHDBC			V_OD_hdbc;			// Handle connection

char			V_OD_stat[10];		// Status SQL
SQLINTEGER		V_OD_err;//,V_OD_rowanz,V_OD_id;
SQLLEN			V_OD_err1,V_OD_rowanz,V_OD_id;
SQLSMALLINT		V_OD_mlen,V_OD_colanz;
char             	V_OD_msg[200],V_OD_buffer[200];
SQLDOUBLE		retInt;
SQLHSTMT		V_OD_hstmt;

int main(int argc,char *argv[])
{
	// 1. allocate Environment handle and register version 
	V_OD_erg=SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&V_OD_Env);
	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
	{
		printf("Error AllocHandle\n");
		exit(0);
	}
	V_OD_erg=SQLSetEnvAttr(V_OD_Env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0); 
	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
	{
		printf("Error SetEnv\n");
		SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
		exit(0);
	}
	// 2. allocate connection handle, set timeout
	V_OD_erg = SQLAllocHandle(SQL_HANDLE_DBC, V_OD_Env, &V_OD_hdbc); 
	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
	{
		printf("Error AllocHDB %d\n",V_OD_erg);
		SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
		exit(0);
	}
	SQLSetConnectAttr(V_OD_hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)5, 0);
	// 3. Connect to the datasource "web" 
	V_OD_erg = SQLConnect(V_OD_hdbc, (SQLCHAR*) "HANADB1", SQL_NTS,
                                     (SQLCHAR*) "AFL_ACCESS", SQL_NTS,
                                     (SQLCHAR*) "Initial123", SQL_NTS);
	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
	{
		printf("Error SQLConnect %d\n",V_OD_erg);
		SQLGetDiagRec(SQL_HANDLE_DBC, V_OD_hdbc,1, 
		              V_OD_stat, &V_OD_err,V_OD_msg,100,&V_OD_mlen);
		printf("%s (%d)\n",V_OD_msg,V_OD_err);
		SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
		exit(0);
	}
	printf("Connected !\n");
	V_OD_erg=SQLAllocHandle(SQL_HANDLE_STMT, V_OD_hdbc, &V_OD_hstmt);
	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
	{
		printf("Fehler im AllocStatement %d\n",V_OD_erg);
		SQLGetDiagRec(SQL_HANDLE_DBC, V_OD_hdbc,1, V_OD_stat,&V_OD_err,V_OD_msg,100,&V_OD_mlen);
		printf("%s (%d)\n",V_OD_msg,V_OD_err);
	        SQLDisconnect(V_OD_hdbc);
        	SQLFreeHandle(SQL_HANDLE_DBC,V_OD_hdbc);
		SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
		exit(0);
	}
	SQLBindCol(V_OD_hstmt,1,SQL_C_DOUBLE, &retInt,150,&V_OD_err1);
    	//SQLBindCol(V_OD_hstmt,2,SQL_C_ULONG,&V_OD_id,150,&V_OD_err1);
	
    	V_OD_erg=SQLExecDirect(V_OD_hstmt,"SELECT \"IRR\",'a' \"b\" from IRT.\"irt.cfkernel.tables::CF.IRR\"",SQL_NTS);   
    	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
    	{
       		printf("Error in Select %d\n",V_OD_erg);
       		SQLGetDiagRec(SQL_HANDLE_DBC, V_OD_hdbc,1, V_OD_stat,&V_OD_err,V_OD_msg,100,&V_OD_mlen);
       		printf("%s (%d)\n",V_OD_msg,V_OD_err);
      		SQLFreeHandle(SQL_HANDLE_STMT,V_OD_hstmt);
       		SQLDisconnect(V_OD_hdbc);
       		SQLFreeHandle(SQL_HANDLE_DBC,V_OD_hdbc);
       		SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
       		exit(0);
    	}
    	V_OD_erg=SQLNumResultCols(V_OD_hstmt,&V_OD_colanz);
    	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
    	{
        	SQLFreeHandle(SQL_HANDLE_STMT,V_OD_hstmt);
        	SQLDisconnect(V_OD_hdbc);
        	SQLFreeHandle(SQL_HANDLE_DBC,V_OD_hdbc);
        	SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
        	exit(0);
   	}
    	printf("Number of Columns %d\n",V_OD_colanz);
    	V_OD_erg=SQLRowCount(V_OD_hstmt,&V_OD_rowanz);
	//SQLBindCol( V_OD_hstmt,0, SQL_C_CHAR,
        //   buf[ i ], sizeof( buf[ i ] ), &indicator[ i ] );

   	if ((V_OD_erg != SQL_SUCCESS) && (V_OD_erg != SQL_SUCCESS_WITH_INFO))
    	{
      		printf("Number ofRowCount %d\n",V_OD_erg);
      		SQLFreeHandle(SQL_HANDLE_STMT,V_OD_hstmt);
      		SQLDisconnect(V_OD_hdbc);
      		SQLFreeHandle(SQL_HANDLE_DBC,V_OD_hdbc);
      		SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
      		exit(0);
    	}
	SQLLEN d_len[2];
	SQLDOUBLE output;
	SQLCHAR outputa[2];
	SQLBindCol(V_OD_hstmt,1,SQL_DOUBLE,&output,sizeof(output),&d_len[0]);
	SQLBindCol(V_OD_hstmt,2,SQL_C_CHAR,outputa,sizeof(outputa),&d_len[1]);

    	V_OD_erg=SQLFetch(V_OD_hstmt);  
	int i=0;
	clock_t start, end;
	double cpu_time_used;
	start = clock();
	//SQLBindCol(V_OD_hstmt,0,SQL_DOUBLE,&output,0,&d_len);
    	while(V_OD_erg != SQL_NO_DATA)
    	{
     		//printf("Result: %d %s\n",V_OD_id,V_OD_buffer);
		i++;
     		V_OD_erg=SQLFetch(V_OD_hstmt); 
     		//printf("Result: %f  --> %s\n",output,&outputa);
		 
    	};
	end = clock();
	cpu_time_used=((double) (end - start)) / CLOCKS_PER_SEC;
	printf("num rows=%d\nfetched in %f secs\n",i,cpu_time_used);
    	SQLFreeHandle(SQL_HANDLE_STMT,V_OD_hstmt);
    	SQLDisconnect(V_OD_hdbc);
    	SQLFreeHandle(SQL_HANDLE_DBC,V_OD_hdbc);
    	SQLFreeHandle(SQL_HANDLE_ENV, V_OD_Env);
    	return(0);
}
