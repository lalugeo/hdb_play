'use strict';

var db = null;
var conn = null;
var path = require('path');
var fs = require('fs');

const debug = require('debug')('@sap/hana-client:build');
const name = 'build.js';

debug('Starting %s', name);

var exec = require('child_process').exec;

// on "install", Clean up before trying to load module
fs.unlink(path.join(__dirname, 'build/Release/hana-client.node'), function (err) {
    if (err && err.code !== 'ENOENT') {
        console.error(err);
    }
});

try {
    debug('Checking requirements...');
    db = require(path.join(__dirname, 'lib', 'index'));
    db.createConnection(); // If this step succeeds binaries can be found
    debug('Install Complete.');
} catch (err) {
    var checkstr = 'libdbcapiHDB.so` is missing.';
    if (err.message.indexOf(checkstr) > -1) {
        console.error(err.message);
        process.exit(-1);
    }

    try {
        debug("Trying to build binaries");

        debug('Attempting to run node-gyp configure');
        var config = require('child_process').exec;
        config("node-gyp configure", function (error, out) {
            if (error) {
                debug('Error when executing node-gyp configure');
                debug('Make sure node-gyp is installed and in the PATH');
                console.error(error, out);
                process.exit(-1);
            } else {
                debug('Done running node-gyp configure');
            }
            debug('Attempting to run node-gyp build');
            var build = require('child_process').exec;
            build("node-gyp build", function (error, out) {
                if (error) {
                    debug('Error when executing node-gyp build');
                    debug('Make sure compiler is installed and in the PATH');
                    console.error(error, out);
                    process.exit(-1);
                } else {
                    debug('Done running node-gyp build');
                }
                debug("Built Binaries.");

                // Clear cache to force reload
                delete require.cache[require.resolve(path.join(__dirname, 'lib', 'index'))];
                db = require(path.join(__dirname, 'lib', 'index'));
                conn = db.createConnection();
                debug('Install Complete.');

            });
        });
    } catch (err) {
        console.error('FATAL ERROR: A fatal error occurred during install.');
        console.error(err.message);
        process.exit(-1);
    }

}
