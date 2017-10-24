'use strict';

module.exports =
{
    // Create an readable stream which returns object
    createObjectStream: function (resultset) {
        return new HanaObjectStream(resultset);
    },

    // Create an readable stream which returns array
    createArrayStream: function (resultset) {
        return new HanaArrayStream(resultset);
    },

    // Create a LOB stream
    createLobStream: function (resultset, columnIndex, options) {
        return new HanaLobStream(resultset, columnIndex, options);
    }
};

var util = require('util');
var Readable = require('stream').Readable;

// Object stream
function HanaObjectStream(resultset) {
    checkResultset(resultset);
    Readable.call(this, { objectMode: true });
    this.resultset = resultset;
};

util.inherits(HanaObjectStream, Readable);

HanaObjectStream.prototype._read = function () {
    var stream = this;
    this.resultset.next(function (err, ret) {
        if (err === undefined) {
            if (ret) {
                stream.push(stream.resultset.getValues());
            } else {
                stream.push(null);
            }
        } else {
            stream.emit('error', err);
        }
    });
};

// Array stream
function HanaArrayStream(resultset) {
    checkResultset(resultset);
    Readable.call(this, { objectMode: true });
    this.resultset = resultset;
    this.columnInfo = this.resultset.getColumnInfo();
    this.colCount = this.columnInfo.length;
};

util.inherits(HanaArrayStream, Readable);

HanaArrayStream.prototype._read = function () {
    var stream = this;
    this.resultset.next(function (err, ret) {
        if (err === undefined) {
            if (ret) {
                var values = [];
                for (var i = 0; i < stream.colCount; i++) {
                    values.push(stream.resultset.getValue(i));
                }
                stream.push(values);
            } else {
                stream.push(null);
            }
        } else {
            stream.emit('error', err);
        }
    });
};

// Lob stream
function HanaLobStream(resultset, columnIndex, options) {
    checkResultset(resultset);
    Readable.call(this, { objectMode: true });
    this.resultset = resultset;
    this.columnInfo = this.resultset.getColumnInfo();
    this.columnIndex = columnIndex;
    this.options = options || {readSize: DEFAULT_READ_SIZE};
    this.readSize = (this.options.readSize > MAX_READ_SIZE) ? MAX_READ_SIZE : this.options.readSize;
    this.offset = 0;
    checkColumnIndex(this.columnInfo, columnIndex);
    checkLobType(this.columnInfo, columnIndex);
};

util.inherits(HanaLobStream, Readable);

HanaLobStream.prototype._read = function () {
    var stream = this;
    var buffer = new Buffer(this.readSize);
    this.resultset.getData(this.columnIndex, this.offset, buffer, 0, this.readSize, function (err, bytesRetrieved) {
        if (err === undefined) {
            stream.offset += bytesRetrieved;
            if (bytesRetrieved > 0) {
                if (bytesRetrieved < stream.readSize) {
                    var buffer2 = new Buffer(bytesRetrieved);
                    buffer.copy(buffer2, 0, 0, bytesRetrieved);
                    stream.push(buffer2);
                } else {
                    stream.push(buffer);
                }
            } else {
                stream.push(null);
            }
        } else {
            stream.emit('error', err);
        }
    });
};

//HanaLobStream.prototype.Read = function (size) {
//    try {
//        if (size === undefined && size === null) {
//            return this.resultset.getValue(this.columnIndex);
//        } else {
//            if (size <= 0) {
//                throw new Error("Invalid parameter 'size'.");
//            }
//            var buffer = new Buffer(size);
//            var bytesRetrieved = this.resultset.getData(this.columnIndex, this.offset, buffer, 0, size);
//            this.offset += bytesRetrieved;
//            if (bytesRetrieved > 0) {
//                if (bytesRetrieved < size) {
//                    var buffer2 = new Buffer(bytesRetrieved);
//                    buffer.copy(buffer2, 0, 0, bytesRetrieved);
//                    return buffer2;
//                } else {
//                    return buffer;
//                }
//            } else {
//                return null;
//            }
//        }
//    } catch (err) {
//        this.emit('error', err);
//    }
//};

function checkResultset(resultset) {
    if (resultset === undefined || resultset === null) {
        throw new Error("Invalid parameter 'resultset'.");
    }
}

function checkColumnIndex(columnInfo, columnIndex) {
    if (columnIndex === undefined || columnIndex === null ||
        columnIndex < 0 || columnIndex >= columnInfo.length) {
        throw new Error("Invalid parameter 'columnIndex'.");
    }
}

function checkLobType(columnInfo, columnIndex) {
    var type = columnInfo[columnIndex].nativeType;
    if (type !== 25 && // CLOB
        type !== 26 && // NCLOB
        type !== 27) { // BLOB
        throw new Error('Column is not LOB type.');
    }
};

var MAX_READ_SIZE = Math.pow(2, 18);
var DEFAULT_READ_SIZE = Math.pow(2, 11) * 100;
