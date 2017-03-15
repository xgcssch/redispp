#include "stdafx.h"
#include "CppUnitTest.h"

#include "redispp/Response.h"
#include "redispp/Request.h"
#include "redispp/Error.h"

#include <iostream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

std::stringstream Out;

template<typename HandlerType>
bool testit(const std::string& Teststring, redis::ResponseHandler& res, HandlerType&& handler, size_t TransmissionLimit=std::numeric_limits<size_t>::max())
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
        auto Buffersize = boost::asio::buffer_size( ResponseBuffer );

        size_t BytesToCopy = std::min( { RemainingBytes, Buffersize, TransmissionLimit } );
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

template<class ECT_>
std::vector<std::shared_ptr<redis::Response>> testitmultiple( const std::string& Teststring, redis::ResponseHandler& res, size_t ExpectedResponses, ECT_& ec )
{
    std::vector<std::shared_ptr<redis::Response>> Responses{ ExpectedResponses };

    boost::asio::const_buffer InputBuffer = boost::asio::buffer( Teststring );
    size_t InputBufferSize = boost::asio::buffer_size( InputBuffer );
    size_t RemainingBytes = InputBufferSize;
    size_t ConsumedBytes = 0;

    std::cerr << "Test: '" << Teststring << "'" << std::endl;

    bool ParseCompleted = false;
    int ParseId = 0;
    size_t CurrentResponse = 0;
    while( InputBufferSize > ConsumedBytes && CurrentResponse < ExpectedResponses )
    {
        boost::asio::mutable_buffer ResponseBuffer = res.buffer();

        size_t BytesToCopy = std::min( RemainingBytes, boost::asio::buffer_size( ResponseBuffer ) );
        boost::asio::buffer_copy( ResponseBuffer, InputBuffer + ConsumedBytes );
        ConsumedBytes += BytesToCopy;
        RemainingBytes -= BytesToCopy;

        ParseCompleted = res.dataReceived( BytesToCopy );

        if( ParseCompleted )
        {
            do
            {
                Responses[CurrentResponse++] = res.spTop();
            } while( res.commit( true ) );
        }
    }

    ec = ::redis::make_error_code( ParseCompleted ? redis::ErrorCodes::success : redis::ErrorCodes::incomplete_response );

    return Responses;
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

auto static good = [](auto ParseId, const auto& myresult) { return true;};

namespace UnitTest1
{		
    TEST_CLASS(UnitTest1)
    {
    public:

        TEST_METHOD(Redis_Response_Parse_Multiple_Responses_Default_Buffersize)
        {
            std::string test1("*3\r\n$9\r\nsubscribe\r\n$5\r\nfirst\r\n:1\r\n*3\r\n$9\r\nsubscribe\r\n$6\r\nsecond\r\n:2\r\n");

            redis::ResponseHandler rh;

            testit(test1, rh,
                   [](auto ParseId, const auto& myresult)
            {
                switch (ParseId)
                {
                    case 1:
                        Assert::IsTrue(myresult.type() == redis::Response::Type::Array);
                        Assert::IsTrue(myresult.elements().size() == 3);
                        Assert::IsTrue(myresult[0].type() == redis::Response::Type::BulkString);
                        Assert::IsTrue(myresult[0].string() == "subscribe");
                        Assert::IsTrue(myresult[1].type() == redis::Response::Type::BulkString);
                        Assert::IsTrue(myresult[1].string() == "first");
                        Assert::IsTrue(myresult[2].type() == redis::Response::Type::Integer);
                        Assert::IsTrue(myresult[2].string() == "1");
                        break;
                    case 2:
                        Assert::IsTrue(myresult.type() == redis::Response::Type::Array);
                        Assert::IsTrue(myresult.elements().size() == 3);
                        Assert::IsTrue(myresult[0].type() == redis::Response::Type::BulkString);
                        Assert::IsTrue(myresult[0].string() == "subscribe");
                        Assert::IsTrue(myresult[1].type() == redis::Response::Type::BulkString);
                        Assert::IsTrue(myresult[1].string() == "second");
                        Assert::IsTrue(myresult[2].type() == redis::Response::Type::Integer);
                        Assert::IsTrue(myresult[2].string() == "2");
                        break;
                    default:
                        Assert::Fail(L"Fail");
                }
                return true;
            }
            );
        }

        TEST_METHOD(Redis_Response_Parse_Multiple_Responses_Different_Buffersize)
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
                            Assert::IsTrue(myresult.type() == redis::Response::Type::Array);
                            Assert::IsTrue(myresult.elements().size() == 3);
                            Assert::IsTrue(myresult[0].type() == redis::Response::Type::BulkString);
                            Assert::IsTrue(myresult[0].string() == "subscribe");
                            Assert::IsTrue(myresult[1].type() == redis::Response::Type::BulkString);
                            Assert::IsTrue(myresult[1].string() == "first");
                            Assert::IsTrue(myresult[2].type() == redis::Response::Type::Integer);
                            Assert::IsTrue(myresult[2].string() == "1");
                            break;
                        case 2:
                            Assert::IsTrue(myresult.type() == redis::Response::Type::Array);
                            Assert::IsTrue(myresult.elements().size() == 3);
                            Assert::IsTrue(myresult[0].type() == redis::Response::Type::BulkString);
                            Assert::IsTrue(myresult[0].string() == "subscribe");
                            Assert::IsTrue(myresult[1].type() == redis::Response::Type::BulkString);
                            Assert::IsTrue(myresult[1].string() == "second");
                            Assert::IsTrue(myresult[2].type() == redis::Response::Type::Integer);
                            Assert::IsTrue(myresult[2].string() == "2");
                            break;
                        default:
                            Assert::Fail(L"Fail");
                    }
                    return true;
                }
                );
            }
        }

        TEST_METHOD( Redis_Response_Parse_Multiple_Responses_Different_Buffersize_CombinedResult )
        {
            std::string test1( "*3\r\n$9\r\nsubscribe\r\n$5\r\nfirst\r\n:1\r\n*3\r\n$9\r\nsubscribe\r\n$6\r\nsecond\r\n:2\r\n" );
            boost::system::error_code ec;

            for( size_t Buffersize = 1; Buffersize < test1.size();++Buffersize )
            {
                redis::ResponseHandler rh{ Buffersize };

                auto r = testitmultiple( test1, rh, 2, ec );
                Assert::IsTrue( !ec );
                Assert::IsTrue( r.size() == 2 );
                auto& r1 = *r[0];
                Assert::IsTrue( r1.type() == redis::Response::Type::Array );
                Assert::IsTrue( r1.elements().size() == 3 );
                Assert::IsTrue( r1[0].type() == redis::Response::Type::BulkString );
                Assert::IsTrue( r1[0].string() == "subscribe" );
                Assert::IsTrue( r1[1].type() == redis::Response::Type::BulkString );
                Assert::IsTrue( r1[1].string() == "first" );
                Assert::IsTrue( r1[2].type() == redis::Response::Type::Integer );
                Assert::IsTrue( r1[2].string() == "1" );
                auto& r2 = *r[1];
                Assert::IsTrue( r2.type() == redis::Response::Type::Array );
                Assert::IsTrue( r2.elements().size() == 3 );
                Assert::IsTrue( r2[0].type() == redis::Response::Type::BulkString );
                Assert::IsTrue( r2[0].string() == "subscribe" );
                Assert::IsTrue( r2[1].type() == redis::Response::Type::BulkString );
                Assert::IsTrue( r2[1].string() == "second" );
                Assert::IsTrue( r2[2].type() == redis::Response::Type::Integer );
                Assert::IsTrue( r2[2].string() == "2" );
            }
        }

        TEST_METHOD(Redis_Response_Parse_With_Different_Buffersizes)
        {
            Assert::IsTrue(testit_complete("+PONG\r\n", good));
            Assert::IsTrue(testit_complete("-Error message\r\n", good));
            Assert::IsTrue(testit_complete(":1000\r\n", good));
            Assert::IsTrue(testit_complete("$6\r\nfoobar\r\n", good));
            Assert::IsTrue(testit_complete("$-1\r\n", good));
            Assert::IsTrue(testit_complete("*-1\r\n", good));
            Assert::IsTrue(testit_complete("*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", good));
            Assert::IsTrue(testit_complete("*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n*2\r\n+Foo\r\n-Bar\r\n", good));
            //Assert::IsTrue(testit("*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", redis::ResponseHandler(2), good ));
            //Assert::IsTrue(testit("*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", redis::ResponseHandler(1), good ));
        }

        TEST_METHOD(Redis_Response_Parse_With_Toosmall_Buffer)
        {
            for(auto MaxBuffer = 1; MaxBuffer < 30; ++MaxBuffer )
            {
                auto Result = testit("$30\r\n012345678901234567890123456789\r\n", redis::ResponseHandler(5), 
                                      [](auto ParseId, const auto& myresult) { 
                    if ( myresult.type() != redis::Response::Type::BulkString ) return false;
                    if ( myresult.string() != "012345678901234567890123456789" ) return false;
                    return true;
                }, MaxBuffer
                );
                Assert::IsTrue(Result);
            }
            //auto Result = testit_complete("$30\r\n012345678901234567890123456789\r\n", 
            //                      [](auto ParseId, const auto& myresult) { 
            //    if ( myresult.type() != redis::Response::Type::BulkString ) return false;
            //    if ( myresult.string() != "012345678901234567890123456789" ) return false;
            //    return true;
            //}
            //);


        }

        static std::string bufferSequenceToString(const redis::Request::BufferSequence_t& BufferSequence)
        {
            std::string Result;
            for (const auto& SingleBuffer : BufferSequence)
                Result += std::string(boost::asio::buffer_cast<const char*>(SingleBuffer), boost::asio::buffer_size(SingleBuffer));
            return Result;
        }


        TEST_METHOD(Redis_Request_Construction)
        {
            redis::Request r("bingo");
            std::string a("a");
            std::string b("b");
            std::string test("test");
            redis::Request d(std::vector<boost::asio::const_buffer>{ boost::asio::buffer(a), boost::asio::buffer(b), boost::asio::buffer(test) });

            Assert::IsTrue(bufferSequenceToString(d.bufferSequence()) == "*3\r\n$1\r\na\r\n$1\r\nb\r\n$4\r\ntest\r\n");

            redis::Request e("e", "f", std::string("jj"));
            auto ee = e.bufferSequence();

            Assert::IsTrue(bufferSequenceToString(e.bufferSequence()) == "*3\r\n$1\r\ne\r\n$1\r\nf\r\n$2\r\njj\r\n");
        }

    };
}