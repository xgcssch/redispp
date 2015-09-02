#include "stdafx.h"

#include "redisClient\Response.h"
#include "redisClient\Request.h"
#include "redisClient\SimpleConnectionManager.h"
#include "redisClient\Connection.h"
#include "redisClient\Error.h"
#include "redisClient\Commands.h"

#ifndef _DEBUGd

namespace redis
{
    Request prepareRequest_ping()
    {
        return Request("PING");
    }

    auto processResult_ping(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() != Response::Type::SimpleString || Data.string() != "PONG")
            ec = ::redis::make_error_code(ErrorCodes::protocol_error);
    }

    template <class Connection>
    auto ping(Connection& con, boost::system::error_code& ec)
    {
        auto theResponse = con.command(prepareRequest_ping(), ec);
        if (!ec)
            processResult_ping(theResponse->top(), ec);
    }

    template <class Connection, class CompletionToken>
    auto async_ping(Connection& con, CompletionToken&& token)
    {
        using handler_type = typename boost::asio::handler_type<CompletionToken,
            void(boost::system::error_code)>::type;
        handler_type handler(std::forward<decltype(token)>(token));
        boost::asio::async_result<decltype(handler)> result(handler);

        auto spRequest = std::make_shared<Request>(prepareRequest_ping());

        con.async_command(*spRequest, [&con, spRequest, handler](auto ec, const auto& Data)
        {
            if (ec)
                handler(ec);
            else
            {
                processResult_ping(Data,ec);
                handler(ec);
            }
        });

        return result.get();
    }

    template <class T1_, class T2_>
    Request prepareRequest_set(T1_ Key, T2_ Value, size_t ExpireTimeInMilliseconds, bool SetIfNotExist, bool SetIfExist)
    {
        Request r("SET");
        r << Key << Value;
        if (ExpireTimeInMilliseconds)
            r << "PX" << ExpireTimeInMilliseconds;
        if (SetIfNotExist)
            r << "NX";
        if (SetIfExist)
            r << "XX";
        return r;
    }

    auto processResult_set(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() == Response::Type::SimpleString && Data.string() == "OK")
            return true;
        else
            if (Data.type() == Response::Type::Null )
                return false;
            else
            {
                ec = ::redis::make_error_code(ErrorCodes::protocol_error);
                return false;
            }
    }

    template <class Connection, class T1_, class T2_>
    auto set(Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Value, size_t ExpireTimeInMilliseconds = 0, bool SetIfNotExist = false, bool SetIfExist = false)
    {
        auto theResponse = con.command(prepareRequest_set(Key, Value, ExpireTimeInMilliseconds, SetIfNotExist, SetIfExist), ec);
        if (ec)
            return false;

        return processResult_set(theResponse->top(), ec);
    }

    template <class Connection, class CompletionToken, class T1_, class T2_, class R_=bool>
    auto async_set(Connection& con, CompletionToken&& token, T1_ Key, T2_ Value, size_t ExpireTimeInMilliseconds = 0, bool SetIfNotExist = false, bool SetIfExist = false)
    {
        using handler_type = typename boost::asio::handler_type<CompletionToken,
            void(boost::system::error_code, R_)>::type;
        handler_type handler(std::forward<decltype(token)>(token));
        boost::asio::async_result<decltype(handler)> result(handler);

        auto spRequest = std::make_shared<Request>(prepareRequest_set(Key, Value, ExpireTimeInMilliseconds, SetIfNotExist, SetIfExist));

        con.async_command(*spRequest, [&con, spRequest, handler](auto ec, const auto& Data)
        {
            if (ec)
                handler(ec,R_());
            else
            {
                auto Result = processResult_set(Data, ec);
                handler(ec, Result);
            }
        });

        return result.get();
    }


}

int main(int argc, char**argv)
{
    try
    {
        boost::asio::io_service io_service;

        //redis::SimpleConnectionManager scm("hgf-vb-vg-857.int.alte-leipziger.de", 26379);
        redis::SimpleConnectionManager scm("148.251.71.44", 6379);
        
        redis::Connection<redis::SimpleConnectionManager> con(io_service, scm);

        /// Syncronous Version
        //boost::system::error_code ec;
        //redis::ping(con, ec);
        //bool setok = redis::set(con, ec, "ein", "test");

        // Callback Version
        //con.async_command(r, [&con](auto ec, auto Data)
        //{
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    {
        //        std::cerr << Data.dump() << std::endl;
        //    }
        //});

        redis::async_set( con, [](auto ec, bool setok)
        {
            if (ec)
                std::cerr << ec.message() << std::endl;
            else
            {
                std::cerr << "set " << setok << std::endl;
            }
        }, "ein", "test");
        std::thread thread([&io_service]() { io_service.run(); });
        thread.join();

        //redis::async_ping( con, [](auto ec)
        //{
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    {
        //        std::cerr << "Ping OK" << std::endl;
        //    }
        //});
        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();

        // Futures Version
        //boost::asio::io_service::work work(io_service);
        //std::thread thread([&io_service]() { io_service.run(); });

        //boost::system::error_code ec;
        //auto f = con.async_command("PING", boost::asio::use_future);
        //f.wait();
        //std::cerr << f.get() << std::endl;

        //io_service.stop();
        //thread.join();

        //// Coroutine
        //boost::asio::spawn(io_service,
        //                   [&](boost::asio::yield_context yield)
        //{
        //    boost::system::error_code ec;
        //    auto Data = con.async_command(r, yield[ec]);
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    	std::cerr << Data.dump() << std::endl;
        //});
        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    return 0;
    }

#else
bool testit(const std::string& Teststring, size_t Buffersize=1024)
{
    redis::ResponseHandler res(Buffersize);

    boost::asio::const_buffer InputBuffer = boost::asio::buffer(Teststring);
    size_t InputBufferSize = boost::asio::buffer_size(InputBuffer);
    size_t RemainingBytes = InputBufferSize;
    size_t ConsumedBytes = 0;

    //std::cerr << "Test: '" << Teststring << "'" << std::endl;

    bool ParseCompleted = false;
    while (InputBufferSize > ConsumedBytes)
    {
        boost::asio::mutable_buffer ResponseBuffer = res.buffer();

        size_t BytesToCopy = std::min(RemainingBytes, boost::asio::buffer_size(ResponseBuffer));
        boost::asio::buffer_copy(ResponseBuffer, InputBuffer + ConsumedBytes);
        ConsumedBytes += BytesToCopy;
        RemainingBytes -= BytesToCopy;

        ParseCompleted = res.dataReceived(BytesToCopy);
    }

    //if (ParseCompleted)
    //    std::cerr << res.top().dump() << std::endl;

    return ParseCompleted;
}

bool testit_complete(const std::string& Teststring)
{
    bool Result = true;
    for (size_t Buffersize = 1; Buffersize <= Teststring.size(); ++Buffersize)
    {
        Result = testit(Teststring, Buffersize);
        if (!Result)
        {
            std::cerr << "Failure at test " << Teststring << " Buffersize: " << Buffersize << std::endl;
            return false;
        }
    }
    Result = testit(Teststring, redis::ResponseHandler::DefaultBuffersize);
    if (!Result)
    {
        std::cerr << "Failure at test " << Teststring << " Buffersize: " << redis::ResponseHandler::DefaultBuffersize << std::endl;
        return false;
    }
    return Result;
}

#define BOOST_TEST_MODULE Redis Client
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(Redis_Response_Parse_With_Different_Buffersizes)
{
    BOOST_TEST(testit_complete("+PONG\r\n"));
    BOOST_TEST(testit_complete("-Error message\r\n"));
    BOOST_TEST(testit_complete(":1000\r\n"));
    BOOST_TEST(testit_complete("$6\r\nfoobar\r\n"));
    BOOST_TEST(testit_complete("$-1\r\n"));
    BOOST_TEST(testit_complete("*-1\r\n"));
    BOOST_TEST(testit_complete("*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"));
    BOOST_TEST(testit_complete("*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n*2\r\n+Foo\r\n-Bar\r\n"));
}

std::string bufferSequenceToString(const redis::Request::BufferSequence_t& BufferSequence)
{
    std::string Result;
    for (const auto& SingleBuffer : BufferSequence)
        Result += std::string(boost::asio::buffer_cast<const char*>(SingleBuffer), boost::asio::buffer_size(SingleBuffer));
    return Result;
}

BOOST_AUTO_TEST_CASE(Redis_Request_Construction)
{

    redis::Request r("bingo");
    std::string a("a");
    std::string b("b");
    std::string test("test");
    redis::Request d(std::vector<boost::asio::const_buffer>{ boost::asio::buffer(a), boost::asio::buffer(b), boost::asio::buffer(test) });

    BOOST_TEST(bufferSequenceToString(d.bufferSequence()) == "*3\r\n$1\r\na\r\n$1\r\nb\r\n$4\r\ntest\r\n");

    redis::Request e("e", "f", std::string("jj"));
    auto ee = e.bufferSequence();

    BOOST_TEST(bufferSequenceToString(e.bufferSequence()) == "*3\r\n$1\r\ne\r\n$1\r\nf\r\n$2\r\njj\r\n");
}

#endif
