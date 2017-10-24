{
  "targets": [
    {
      "target_name": "hana-client",
      "defines": [ '_DBCAPI_VERSION=2', 'DRIVER_NAME=hana' ],
      "sources": [ "src/hana.cpp",
		   "src/utils.cpp",
		   "src/connection.cpp",
		   "src/statement.cpp",
		   "src/resultset.cpp",
		   "src/DBCAPI_DLL.cpp", ],

      "include_dirs": [ "src/h", ],

      'configurations': {
	'Release': {
	  'msvs_settings': {
            'VCCLCompilerTool': {
              'ExceptionHandling': 1
            }
	  }
	}
      }
    }
  ]
}
