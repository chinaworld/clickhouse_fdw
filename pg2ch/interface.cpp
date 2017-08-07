#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_set>
#include <algorithm>
#include <experimental/optional>
#include <boost/program_options.hpp>

#include <Poco/File.h>
#include <Poco/Util/Application.h>

#include <Common/ClickHouseRevision.h>
#include <Common/Stopwatch.h>
#include <Common/Exception.h>
#include <Common/ShellCommand.h>
#include <Common/ExternalTable.h>
#include <Common/UnicodeBar.h>
#include <Common/formatReadable.h>
#include <Common/NetException.h>
#include <common/readline_use.h>
#include <Common/typeid_cast.h>
#include <Core/Types.h>
#include <Core/QueryProcessingStage.h>
#include <IO/ReadBufferFromFileDescriptor.h>
#include <IO/WriteBufferFromFileDescriptor.h>
#include <IO/WriteBufferFromFile.h>
#include <IO/ReadBufferFromMemory.h>
#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>
#include <DataStreams/AsynchronousBlockInputStream.h>
#include <Parsers/ParserQuery.h>
#include <Parsers/ASTSetQuery.h>
#include <Parsers/ASTUseQuery.h>
#include <Parsers/ASTInsertQuery.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTQueryWithOutput.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/formatAST.h>
#include <Parsers/parseQuery.h>
#include <Interpreters/Context.h>
#include <Client/Connection.h>
#include "InterruptListener.h"
#include <Functions/registerFunctions.h>
#include <AggregateFunctions/registerAggregateFunctions.h>


using String = std::string;
extern "C" void TestConnection(){}

class IAST;
using ASTPtr = std::shared_ptr<DB::IAST>;

static void doInsert(DB::ASTInsertQuery * query)
{

}

extern "C" void ExecuteCHQuery(char *cstrQuery)
{
    String query(cstrQuery);
    const char * begin = query.data();
    const char * end = begin + query.size();
    const char * pos = begin;

        DB::ParserQuery parser(end);
        ASTPtr res;

            String message;
            res = DB::tryParseQuery(parser, pos, end, message, true, "", false);

                DB::ASTInsertQuery * insert = typeid_cast<DB::ASTInsertQuery *>(&*res);
                doInsert(insert);


        auto connection = std::make_unique<Connection>("loclhost", DBMS_DEFAULT_PORT, "", "", "", "client", true,
            Poco::Timespan(config().getInt("connect_timeout", DBMS_DEFAULT_CONNECT_TIMEOUT_SEC), 0),
            Poco::Timespan(config().getInt("receive_timeout", DBMS_DEFAULT_RECEIVE_TIMEOUT_SEC), 0),
            Poco::Timespan(config().getInt("send_timeout", DBMS_DEFAULT_SEND_TIMEOUT_SEC), 0));
}

