#ifndef REDIS_COMMANDS_INCLUDED
#define REDIS_COMMANDS_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include "redispp\Request.h"
#include "redispp\Response.h"
#include "redispp\Error.h"

#include <boost\optional.hpp>

#include <chrono>
#include <list>
#include <map>
#include <iterator>

// The implementation of a command consists of four functions:
// 1. a function that creates a redis::Request object from its parameters
// 2. a function that takes a redis::Response object as a parameter and converts it to a more C++ like form
// 3. a function that takes all parameters of the first function, synchronously executes the command and returns the 
//    result, prepared with the second function
// 4. a function that takes all parameters of the first function, asynchronously executes the command and executes the 
//    completion token with the result, prepared with the second function

namespace redis
{
    namespace Detail {

        // Common functions

        // Call a Redis Operation in synchronous mode
        template <class Connection, class RequestT_, class ResponseT_, class ... Types>
        auto sync_universal( Connection& con, boost::system::error_code& ec, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args )
        {
            // prepare request with given function and transmit it
            auto theResponse = con.transmit( pPrepareFunction( args... ), ec );
            if( ec )
                return std::make_pair( std::move(theResponse), decltype(pResponseFunction( theResponse->top(), ec ))() );

            // On failure an redis error is the topmost response entity
            if( theResponse->top().type() == Response::Type::Error )
            {
                ec = ::redis::make_error_code( ErrorCodes::server_error );

                // save error in connection
                con.setLastServerError( theResponse->top().string() );

                // return default constructed result and the error
                return std::make_pair( std::move(theResponse), decltype(pResponseFunction( theResponse->top(), ec ))() );
            }

            // prepare result with given function
            return std::make_pair( std::move(theResponse), pResponseFunction( theResponse->top(), ec ) );
        }

        // Call a Redis Operation in asynchronous mode
        template <class Connection, class CompletionToken, class RequestT_, class ResponseT_, class ... Types>
        auto async_universal( Connection& con, CompletionToken&& token, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args )
        {
            using handler_type = typename boost::asio::handler_type<CompletionToken, void( boost::system::error_code, ResponseT_ )>::type;
            handler_type handler( std::forward<decltype(token)>( token ) );
            boost::asio::async_result<decltype(handler)> result( handler );

            // prepare request
            auto spRequest = std::make_shared<Request>( pPrepareFunction( args... ) );

            // transmit command asynchronously
            con.async_command( *spRequest, [&con, spRequest, handler, pResponseFunction]( const auto& ec, const auto& Data )
            {
                if( ec )
                {
                    // call handler with default constructed result and the error
                    boost::system::error_code ec2;
                    handler( ec2, decltype(pResponseFunction( Data, ec2 ))() );
                }
                else
                    // call handler with prepared result
                    handler( ec, pResponseFunction( Data, ec ) );
            } );

            return result.get();
        }

        // Call a Redis Operation in asynchronous mode - specialized for Commands returning nothing
        template <class Connection, class CompletionToken, class RequestT_, class ResponseT_, class ... Types>
        auto async_universal_void( Connection& con, CompletionToken&& token, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args )
        {
            using handler_type = typename boost::asio::handler_type<CompletionToken, void( boost::system::error_code, ResponseT_ )>::type;
            handler_type handler( std::forward<decltype(token)>( token ) );
            boost::asio::async_result<decltype(handler)> result( handler );

            auto spRequest = std::make_shared<Request>( pPrepareFunction( args... ) );

            con.async_command( *spRequest, [&con, spRequest, handler, pResponseFunction]( const auto& ec, const auto& Data )
            {
                if( !ec )
                {
                    boost::system::error_code ec2;
                    pResponseFunction( Data, ec2 );
                }

                handler( ec );
            } );

            return result.get();
        }
    }

    // Common responses
    // This response types are common to many Redis commands

    inline auto OKResult( const Response& Data, boost::system::error_code& ec )
    {
        // if the Result is OK, the result is true, otherwise it's false

        if( Data.type() == Response::Type::SimpleString && Data.string() == "OK" )
            return true;
        else
            if( Data.type() == Response::Type::Null )
                return false;
            else
            {
                ec = ::redis::make_error_code( ErrorCodes::protocol_error );
                return false;
            }
    }

    inline int64_t IntResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() == Response::Type::Integer )
            return Data.asint();
        else
        {
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );
            return 0;
        }
    }

    inline int64_t incrResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() != Response::Type::Integer )
        {
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );
            return 0;
        }

        return std::stoll( std::string( Data.data(), Data.size() ) );
    }

    // Specific command implementations

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                          C L I E N T  S E T N A M E
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request clientSetnameRequest( const T1_ ConnectionName )
    {
        Request r( "CLIENT", "SETNAME" );
        r << ConnectionName;
        return r;
    }

    template <class Connection, class T1_>
    auto clientSetname( Connection& con, boost::system::error_code& ec, T1_ ConnectionName )
    {
        return Detail::sync_universal( con, ec, &clientSetnameRequest<decltype(ConnectionName)>, &OKResult, ConnectionName );
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_clientSetname( Connection& con, CompletionToken&& token, T1_ ConnectionName )
    {
        return Detail::async_universal( con, token, &clientSetnameRequest<decltype(ConnectionName)>, &OKResult, ConnectionName );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     E X E C
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    inline Request execCommand()
    {
        return Request("EXEC");
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                  E X P I R E
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request expireCommand( T1_ Key, std::chrono::milliseconds ExpireTimeInMilliseconds )
    {
        Request r( "PEXPIRE" );
        r << Key << ExpireTimeInMilliseconds.count();
        return r;
    }

    inline bool expireResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() != Response::Type::Integer )
        {
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );
            return false;
        }

        return Data.string() == "1";
    }

    template <class Connection, class T1_>
    auto expire( Connection& con, boost::system::error_code& ec, T1_ Key, std::chrono::milliseconds ExpireTimeInMilliseconds )
    {
        return Detail::sync_universal( con, ec, &expireCommand<decltype(Key)>, &expireResult, Key, ExpireTimeInMilliseconds );
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_expire( Connection& con, CompletionToken&& token, T1_ Key, std::chrono::milliseconds ExpireTimeInMilliseconds )
    {
        return Detail::async_universal_void( con, token, &expireCommand<decltype(Key), &expireResult, Key, ExpireTimeInMilliseconds );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     G E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request getCommand( const T1_& Key )
    {
        Request r( "GET" );
        r << Key;
        return r;
    }

    inline boost::optional<boost::asio::const_buffer> getResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() == Response::Type::BulkString )
            return boost::asio::buffer( Data.data(), Data.size() );
        else
            if( Data.type() != Response::Type::Null )
                ec = ::redis::make_error_code( ErrorCodes::protocol_error );

        return boost::none;
    }

    template <class Connection, class T1_>
    auto get(Connection& con, boost::system::error_code& ec, const T1_& Key)
    {
        return Detail::sync_universal(con, ec, &getCommand<decltype(Key)>, &getResult, std::ref(Key));
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_get(Connection& con, CompletionToken&& token, T1_ Key)
    {
        return Detail::async_universal(con, token, &getCommand<decltype(Key)>, &getResult, Key);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     D E L
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request delCommand( T1_ Key )
    {
        Request r( "DEL" );
        r << Key;
        return r;
    }

    template <class Connection, class T1_>
    auto del(Connection& con, boost::system::error_code& ec, T1_ Key)
    {
        return Detail::sync_universal(con, ec, &delCommand<decltype(Key)>, &IntResult, Key);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_del(Connection& con, CompletionToken&& token, T1_ Key)
    {
        return Detail::async_universal(con, token, &delCommand<decltype(Key)>, &IntResult, Key);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     I N C R
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request incrCommand( T1_ Key )
    {
        Request r( "INCR" );
        r << Key;
        return r;
    }

    // Call Redis INCR Command syncronously taking a Key and returning an int64_t result
    template <class Connection, class T1_>
    auto incr(Connection& con, boost::system::error_code& ec, T1_ Key)
    {
        return Detail::sync_universal(con, ec, &incrCommand<decltype(Key)>, &incrResult, Key);
    }

    // Call Redis INCR Command asyncronously taking a Key and returning an int64_t result
    template <class Connection, class CompletionToken, class T1_>
    auto async_incr(Connection& con, CompletionToken&& token, T1_ Key)
    {
        return Detail::async_universal(con, token, &incrCommand<decltype(Key)>, &incrResult, Key);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                M U L T I
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    inline Request multiCommand()
    {
        return Request( "MULTI" );
    }

    template <class Connection>
    bool multi( Connection& con, boost::system::error_code& ec )
    {
        return Detail::sync_universal( con, ec, &multiCommand, &OKResult );
    }

    template <class Connection, class CompletionToken>
    auto async_multi( Connection& con, CompletionToken&& token, int64_t Index )
    {
        return Detail::async_universal( con, token, &multiCommand, &OKResult );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     P I N G
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    inline Request pingCommand()
    {
        return Request( "PING" );
    }

    inline void pingResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() != Response::Type::SimpleString || Data.string() != "PONG" )
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );
    }

    template <class Connection>
    auto ping(Connection& con, boost::system::error_code& ec)
    {
        return Detail::sync_universal(con, ec, &pingCommand, &pingResult );
    }

    template <class Connection, class CompletionToken>
    auto async_ping(Connection& con, CompletionToken&& token)
    {
        return Detail::async_universal_void(con, token, &pingCommand, &pingResult );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     R O L E
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    inline Request roleCommand()
    {
        return Request( "ROLE" );
    }

    inline auto roleResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() != Response::Type::Array || Data.elements().empty() )
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );

        return Data[0].string();
    }

    template <class Connection>
    auto role( Connection& con, boost::system::error_code& ec )
    {
        return Detail::sync_universal( con, ec, &roleCommand, &roleResult );
    }

    template <class Connection, class CompletionToken>
    auto async_role( Connection& con, CompletionToken&& token )
    {
        return Detail::async_universal_void( con, token, &roleCommand, &roleResult );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                S E L E C T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    inline Request selectCommand( int64_t Index )
    {
        Request r( "SELECT" );
        r << Index;
        return r;
    }

    template <class Connection>
    bool select( Connection& con, boost::system::error_code& ec, int64_t Index )
    {
        return Detail::sync_universal( con, ec, &selectCommand, &OKResult, Index ).second;
    }

    template <class Connection, class CompletionToken>
    auto async_select( Connection& con, CompletionToken&& token, int64_t Index )
    {
        return Detail::async_universal( con, token, &selectCommand, &OKResult, Index );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     S E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    enum class SetOptions { None, SetIfNotExist, SetIfExist };

    template <class T1_, class T2_>
    Request setCommand( T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None )
    {
        Request r( "SET" );
        r << Key << Value;
        if( ExpireTimeInMilliseconds != std::chrono::milliseconds::zero() )
            r << "PX" << ExpireTimeInMilliseconds.count();
        if( Options == SetOptions::SetIfNotExist )
            r << "NX";
        if( Options == SetOptions::SetIfExist )
            r << "XX";
        return r;
    }

    template <class Connection, class T1_, class T2_>
    auto set(Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None)
    {
        return Detail::sync_universal(con, ec, &setCommand<decltype(Key), decltype(Value)>, &OKResult, Key, Value, ExpireTimeInMilliseconds, Options);
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_set(Connection& con, CompletionToken&& token, T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None)
    {
        return Detail::async_universal(con, token, &setCommand<decltype(Key), decltype(Value)>, &OKResult, Key, Value, ExpireTimeInMilliseconds, Options);
    }

}

#endif
