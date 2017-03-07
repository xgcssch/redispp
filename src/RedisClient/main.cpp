#include "stdafx.h"

#include "redispp/Response.h"
#include "redispp/Request.h"
#include "redispp/Connection.h"
#include "redispp/SingleHostConnectionManager.h"
#include "redispp/MultipleHostsConnectionManager.h"
#include "redispp/SentinelConnectionManager.h"
#include "redispp/Error.h"

#include "redispp/Commands.h"
#include "redispp/HashCommands.h"

#ifndef _DEBUGf

#include <time.h>
#include <string.h>

#include <thread>
#include <chrono>

namespace po = boost::program_options;

int main(int argc, char**argv)
{
    std::string             Hostname;
    int                     Port;

    try
    {
        po::options_description CommandlineOptionsDescription("Usage: redisclient [OPTIONS]");
        CommandlineOptionsDescription.add_options()
            ("help",                                                                                "Output this text and exit")
            ("hostname,h",      po::value<std::string>(&Hostname)->default_value("127.0.0.1"),      "Server hostname (default: 127.0.0.1).")
            ("port,p",          po::value<int>(&Port)->default_value(6379),                         "Server port (default: 6379).")
            ;

        po::variables_map CommandlineOptions;
        po::store(po::command_line_parser(argc, argv)
                  .options(CommandlineOptionsDescription)
                  .run(),
                  CommandlineOptions);

        po::notify(CommandlineOptions);

        if (CommandlineOptions.count("help"))
        {
            std::cerr << CommandlineOptionsDescription << std::endl;
            return 8;
        }

        boost::asio::io_service io_service;

        using namespace redis;

        //redis::SingleHostConnectionManager scm(Hostname, Port);
        redis::MultipleHostsConnectionManager mcm(io_service, 
            { 
                MultipleHostsConnectionManager::Host{ "hgf-vb-vg-116.int.alte-leipziger.de", 26379 },
                MultipleHostsConnectionManager::Host{ "hgf-vb-vg-254.int.alte-leipziger.de", 26379 },
                MultipleHostsConnectionManager::Host{ "hgf-vb-vg-857.int.alte-leipziger.de", 26379 }
            }
        );
        redis::SentinelConnectionManager secm( io_service,
            {
                MultipleHostsConnectionManager::Host{ "hgf-vb-vg-116.int.alte-leipziger.de", 26379 }/*,
                MultipleHostsConnectionManager::Host{ "hgf-vb-vg-254.int.alte-leipziger.de", 26379 },
                MultipleHostsConnectionManager::Host{ "hgf-vb-vg-857.int.alte-leipziger.de", 26379 }*/
            }, "almaster"
        );

        //redis::Connection<redis::SingleHostConnectionManager> con(io_service, scm);
        //redis::Connection<redis::MultipleHostsConnectionManager> con( io_service, mcm );
        redis::Connection<redis::SentinelConnectionManager> con(io_service, secm, 1);

        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        std::string Key( "2F.STRL.8be94ffe69c8454ca11a06b84e758f2c.murkelpurz" );

        using namespace std::literals;
        boost::system::error_code ec;
        for( ;;)
        {
            std::this_thread::sleep_for( 1s );

            auto R0 = con.transmit( redis::getCommand( Key ), ec );
            if( !ec )
            {
            }

            if( R0->top().type() == redis::Response::Type::SimpleString ||
                R0->top().type() == redis::Response::Type::BulkString
                )
            {
                if( R0->top().asint() > 10 )
                {
                    std::cerr << "limit reached: " << R0->top().asint() << std::endl;
                    continue;
                }
            }

            redis::Pipeline pip;
            pip << redis::multiCommand()
                << redis::setCommand( Key, "0", 60s, redis::SetOptions::SetIfNotExist )
                << redis::incrCommand( Key )
                << redis::execCommand()
                ;

            auto RAll = con.transmit( pip, ec );

            //if( !redis::multi( con, ec ) )
            //{
            //    std::cerr << "MULTI: " << ec.message() << " - " << con.lastServerError() << std::endl;
            //    continue;
            //}

            //size_t index = 0;
            //if( R0->top().type() == redis::Response::Type::Null )
            //{
            //    con.transmit( redis::setCommand( Key, "0", 60s ), ec );
            //    ++index;
            //}
            //auto R1 = con.transmit( redis::incrCommand( Key ), ec );
            //auto Result = con.transmit( redis::execCommand(), ec );

            //auto val = redis::incrResult( Result->top()[index], ec );

            auto rh = con.remote_endpoint();
            //std::cerr << "Index now " << val << " - " << std::get<0>(rh) << ":" << std::get<1>( rh ) << std::endl;

            //auto R1 = con.transmit( redis::incrCommand( "testit" ), ec );
            //auto R2 = con.transmit( redis::expireCommand( "testit", 10s ), ec );
            //auto Result = con.transmit( redis::execCommand(), ec );
            //if( ec )
            //    continue;

            auto val = redis::incrResult( RAll[3][1], ec );
            //auto expireok = redis::expireResult( Result->top()[1], ec );

            //auto rh = con.remote_endpoint();
            std::cerr << "OK " << val << " - " << std::get<0>(rh) << ":" << std::get<1>( rh ) << std::endl;
        }

        //std::error_code ec;
        //auto xx = redis::sentinel_getMasterAddrByName(con, ec, "almaster");
        //if( ec )
        //    std::cerr << ec.message() << std::endl;
        //else
        //    std::cerr << xx.first << ":" << xx.second << std::endl;
        //auto yy = redis::sentinel_sentinels(con, ec, "almaster");
        //if (ec)
        //    std::cerr << ec.message() << std::endl;
        //else
        //{
        //    for( const auto& Sentinel : yy )
        //    {
        //        std::cerr << Sentinel.at("name") << std::endl;
        //    }
        //}

        /// Synchronous Version
#ifdef sdfasdf
        std::string Keyname("simple_loop:count");
        std::error_code ec;
        redis::set(con, ec, Keyname, "0");

        std::chrono::time_point<std::chrono::system_clock> start, end;
        start = std::chrono::system_clock::now();
        end = start + std::chrono::seconds(5);

        size_t Count = 0;
        while (std::chrono::system_clock::now() < end)
        {
            redis::incr(con, ec, Keyname);
            ++Count;
        }

        std::chrono::duration<double> elapsed_seconds = std::chrono::system_clock::now() - start;
        double actual_freq = (double)Count / elapsed_seconds.count();

        std::time_t end_time = std::chrono::system_clock::to_time_t(end);

        std::cout << "finished computation elapsed time: " << elapsed_seconds.count() << "s\n"
            << actual_freq << "req/s\n";

        auto xx = redis::get(con, ec, Keyname);
        std::cout << "Final value of counter: " << std::string(boost::asio::buffer_cast<const char*>(xx.value()), boost::asio::buffer_size(xx.value())) << "\n";
        bool setok = redis::set(con, ec, "ein", "test");
#endif
#if sadfasdfsdf
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

        //redis::async_set(con, [](auto ec, bool setok)
        //{
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    {
        //        std::cerr << "set " << setok << std::endl;
        //    }
        //}, "ein", "test");
        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();

        redis::async_ping( con, [](auto ec)
        {
            if (ec)
                std::cerr << ec.message() << std::endl;
            else
            {
                std::cerr << "Ping OK" << std::endl;
            }
        });
        std::thread thread([&io_service]() { io_service.run(); });
        thread.join();

        // Futures Version
        //boost::asio::io_service::work work(io_service);
        //std::thread thread([&io_service]() { io_service.run(); });

        //std::error_code ec;
        //auto f = con.async_command("PING", boost::asio::use_future);
        //f.wait();
        //std::cerr << f.get() << std::endl;

        //io_service.stop();
        //thread.join();

        //// Coroutine
        //boost::asio::spawn(io_service,
        //                   [&](boost::asio::yield_context yield)
        //{
        //    std::error_code ec;
        //    auto Data = con.async_command(r, yield[ec]);
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    	std::cerr << Data.dump() << std::endl;
        //});
        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();
#endif
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    return 0;
}

#else
template<typename HandlerType>
bool testit(const std::string& Teststring, redis::ResponseHandler& res, HandlerType&& handler )
{
    boost::asio::const_buffer InputBuffer = boost::asio::buffer(Teststring);
    size_t InputBufferSize = boost::asio::buffer_size(InputBuffer);
    size_t RemainingBytes = InputBufferSize;
    size_t ConsumedBytes = 0;

    //std::cerr << "Test: '" << Teststring << "'" << std::endl;

    bool ParseCompleted = false;
    int ParseId = 0;
    while (InputBufferSize > ConsumedBytes)
    {
        boost::asio::mutable_buffer ResponseBuffer = res.buffer();

        size_t BytesToCopy = std::min(RemainingBytes, boost::asio::buffer_size(ResponseBuffer));
        boost::asio::buffer_copy(ResponseBuffer, InputBuffer + ConsumedBytes);
        ConsumedBytes += BytesToCopy;
        RemainingBytes -= BytesToCopy;

        ParseCompleted = res.dataReceived(BytesToCopy);

        if (ParseCompleted)
        {
            do
            {
                if (!handler(++ParseId, res.top()))
                    return false;
                //std::cerr << res.top().dump() << std::endl;
            } while (res.commit());
        }
    }

    //if (ParseCompleted)
    //    std::cerr << res.top().dump() << std::endl;

    return ParseCompleted;
}

template<typename HandlerType>
bool testit_complete(const std::string& Teststring, HandlerType&& handler)
{
    bool Result = true;
    for (size_t Buffersize = 1; Buffersize <= Teststring.size(); ++Buffersize)
    {
        Result = testit(Teststring, redis::ResponseHandler(Buffersize), handler);
        if (!Result)
        {
            std::cerr << "Failure at test " << Teststring << " Buffersize: " << Buffersize << std::endl;
            return false;
        }
    }
    Result = testit(Teststring, redis::ResponseHandler(), handler);
    if (!Result)
    {
        std::cerr << "Failure at test " << Teststring << " Buffersize: " << redis::ResponseHandler::DefaultBuffersize << std::endl;
        return false;
    }
    return Result;
}

#define BOOST_TEST_MODULE Redis Client
#include <boost/test/included/unit_test.hpp>

auto static good = [](auto ParseId,const auto& myresult) { return true;};

BOOST_AUTO_TEST_CASE(Redis_Response_Parse_Multiple_Responses_Default_Buffersize)
{
    std::string test1("*3\r\n$9\r\nsubscribe\r\n$5\r\nfirst\r\n:1\r\n*3\r\n$9\r\nsubscribe\r\n$6\r\nsecond\r\n:2\r\n");

    redis::ResponseHandler rh;

    testit(test1, rh, 
           [](auto ParseId, const auto& myresult) 
    {
        switch (ParseId)
        {
            case 1:
                BOOST_TEST(myresult.type() == redis::Response::Type::Array);
                BOOST_TEST(myresult.elements().size() == 3);
                BOOST_TEST(myresult[0].type() == redis::Response::Type::BulkString);
                BOOST_TEST(myresult[0].string() == "subscribe");
                BOOST_TEST(myresult[1].type() == redis::Response::Type::BulkString);
                BOOST_TEST(myresult[1].string() == "first");
                BOOST_TEST(myresult[2].type() == redis::Response::Type::Integer);
                BOOST_TEST(myresult[2].string() == "1");
                break;
            case 2:
                BOOST_TEST(myresult.type() == redis::Response::Type::Array);
                BOOST_TEST(myresult.elements().size() == 3);
                BOOST_TEST(myresult[0].type() == redis::Response::Type::BulkString);
                BOOST_TEST(myresult[0].string() == "subscribe");
                BOOST_TEST(myresult[1].type() == redis::Response::Type::BulkString);
                BOOST_TEST(myresult[1].string() == "second");
                BOOST_TEST(myresult[2].type() == redis::Response::Type::Integer);
                BOOST_TEST(myresult[2].string() == "2");
                break;
            default:
                BOOST_TEST(false);
        }
        return true;
    }
    );
}

BOOST_AUTO_TEST_CASE(Redis_Response_Parse_Multiple_Responses_Different_Buffersize)
{
    std::string test1("*3\r\n$9\r\nsubscribe\r\n$5\r\nfirst\r\n:1\r\n*3\r\n$9\r\nsubscribe\r\n$6\r\nsecond\r\n:2\r\n");

    for (size_t Buffersize = 1; Buffersize < test1.size();++Buffersize)
    {
        redis::ResponseHandler rh(Buffersize);

        testit(test1, rh,
               [](auto ParseId, const auto& myresult)
        {
            switch (ParseId)
            {
                case 1:
                    BOOST_TEST(myresult.type() == redis::Response::Type::Array);
                    BOOST_TEST(myresult.elements().size() == 3);
                    BOOST_TEST(myresult[0].type() == redis::Response::Type::BulkString);
                    BOOST_TEST(myresult[0].string() == "subscribe");
                    BOOST_TEST(myresult[1].type() == redis::Response::Type::BulkString);
                    BOOST_TEST(myresult[1].string() == "first");
                    BOOST_TEST(myresult[2].type() == redis::Response::Type::Integer);
                    BOOST_TEST(myresult[2].string() == "1");
                    break;
                case 2:
                    BOOST_TEST(myresult.type() == redis::Response::Type::Array);
                    BOOST_TEST(myresult.elements().size() == 3);
                    BOOST_TEST(myresult[0].type() == redis::Response::Type::BulkString);
                    BOOST_TEST(myresult[0].string() == "subscribe");
                    BOOST_TEST(myresult[1].type() == redis::Response::Type::BulkString);
                    BOOST_TEST(myresult[1].string() == "second");
                    BOOST_TEST(myresult[2].type() == redis::Response::Type::Integer);
                    BOOST_TEST(myresult[2].string() == "2");
                    break;
                default:
                    BOOST_TEST(false);
            }
            return true;
        }
        );
    }
}

BOOST_AUTO_TEST_CASE(Redis_Response_Parse_With_Different_Buffersizes)
{
    BOOST_TEST(testit_complete("+PONG\r\n", good));
    BOOST_TEST(testit_complete("-Error message\r\n", good));
    BOOST_TEST(testit_complete(":1000\r\n", good));
    BOOST_TEST(testit_complete("$6\r\nfoobar\r\n", good));
    BOOST_TEST(testit_complete("$-1\r\n", good));
    BOOST_TEST(testit_complete("*-1\r\n", good));
    BOOST_TEST(testit_complete("*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", good));
    BOOST_TEST(testit_complete("*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n*2\r\n+Foo\r\n-Bar\r\n", good));
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
