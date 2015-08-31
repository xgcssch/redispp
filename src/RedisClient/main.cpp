#include "stdafx.h"

#include "redisClient\Response.h"
#include "redisClient\SimpleConnectionManager.h"
#include "redisClient\Connection.h"

#ifndef _DEBUG
int main(int argc, char**argv)
{
    try
    {
        boost::asio::io_service io_service;

        redis::SimpleConnectionManager scm("bingo.de", 26379);

        redis::Connection<redis::SimpleConnectionManager> con(io_service, scm);

        //boost::system::error_code ec;
        //auto Result = con.command("PING", ec);

        //// Callback Version
        //con.async_command("PING", [&con](auto ec, auto Data)
        //{
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //        std::cerr << Data << std::endl;
        //});
        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();

        // Futures Version
        boost::asio::io_service::work work(io_service);
        std::thread thread([&io_service]() { io_service.run(); });

        boost::system::error_code ec;
        auto f = con.async_command("PING", boost::asio::use_future);
        f.wait();
        std::cerr << f.get() << std::endl;

        io_service.stop();
        thread.join();

        //// Coroutine
        //boost::asio::spawn(io_service,
        //                   [&](boost::asio::yield_context yield)
        //{
        //    boost::system::error_code ec;
        //    auto Data = con.async_command("PING", yield[ec]);
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    	std::cerr << Data << std::endl;
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
    redis::Response res(Buffersize);

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
#endif
