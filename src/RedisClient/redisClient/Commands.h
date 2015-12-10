#ifndef REDIS_COMMANDS_INCLUDED
#define REDIS_COMMANDS_INCLUDED

#include "redisClient\Response.h"
#include "redisClient\Error.h"

#include <boost\optional.hpp>

#include <chrono>
#include <list>
#include <map>
#include <iterator>

namespace redis
{
    namespace Detail {
        template <class Connection, class RequestT_, class ResponseT_, class ... Types>
        auto sync_universal( Connection& con, boost::system::error_code& ec, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args )
        {
            auto theResponse = con.transmitCommand( pPrepareFunction( args... ), ec );
            if( ec )
                return decltype(pResponseFunction( theResponse->top(), ec ))();

            if( theResponse->top().type() == Response::Type::Error )
            {
                ec = ::redis::make_error_code( ErrorCodes::server_error );
                con.setLastServerError( theResponse->top().string() );
                return decltype(pResponseFunction( theResponse->top(), ec ))();
            }

            return pResponseFunction( theResponse->top(), ec );
        }

        template <class Connection, class CompletionToken, class RequestT_, class ResponseT_, class ... Types>
        auto async_universal( Connection& con, CompletionToken&& token, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args )
        {
            using handler_type = typename boost::asio::handler_type<CompletionToken, void( boost::system::error_code, ResponseT_ )>::type;
            handler_type handler( std::forward<decltype(token)>( token ) );
            boost::asio::async_result<decltype(handler)> result( handler );

            auto spRequest = std::make_shared<Request>( pPrepareFunction( args... ) );

            con.async_command( *spRequest, [&con, spRequest, handler, pResponseFunction]( const auto& ec, const auto& Data )
            {
                if( ec )
                {
                    boost::system::error_code ec2;
                    handler( ec2, decltype(pResponseFunction( Data, ec2 ))() );
                }
                else
                    handler( ec, pResponseFunction( Data, ec ) );
            } );

            return result.get();
        }

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

    auto OKResult( const Response& Data, boost::system::error_code& ec )
    {
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

    Request execCommand()
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

    bool expireResult( const Response& Data, boost::system::error_code& ec )
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
    Request getCommand( T1_ Key )
    {
        Request r( "GET" );
        r << Key;
        return r;
    }

    boost::optional<boost::asio::const_buffer> getResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() == Response::Type::BulkString )
            return boost::asio::buffer( Data.data(), Data.size() );
        else
            if( Data.type() != Response::Type::Null )
                ec = ::redis::make_error_code( ErrorCodes::protocol_error );

        return boost::none;
    }

    template <class Connection, class T1_>
    auto get(Connection& con, boost::system::error_code& ec, T1_ Key)
    {
        return Detail::sync_universal(con, ec, &getCommand<decltype(Key)>, &getResult, Key);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_get(Connection& con, CompletionToken&& token, T1_ Key)
    {
        return Detail::async_universal(con, token, &getCommand<decltype(Key)>, &getResult, Key);
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

    int64_t incrResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() != Response::Type::Integer )
        {
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );
            return 0;
        }

        return std::stoll( std::string( Data.data(), Data.size() ) );
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
    //                                                S E L E C T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Request multiCommand()
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

    Request pingCommand()
    {
        return Request( "PING" );
    }

    void pingResult( const Response& Data, boost::system::error_code& ec )
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

    Request roleCommand()
    {
        return Request( "ROLE" );
    }

    auto roleResult( const Response& Data, boost::system::error_code& ec )
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

    Request selectCommand( int64_t Index )
    {
        Request r( "SELECT" );
        r << Index;
        return r;
    }

    template <class Connection>
    bool select( Connection& con, boost::system::error_code& ec, int64_t Index )
    {
        return Detail::sync_universal( con, ec, &selectCommand, &OKResult, Index );
    }

    template <class Connection, class CompletionToken>
    auto async_select( Connection& con, CompletionToken&& token, int64_t Index )
    {
        return Detail::async_universal( con, token, &selectCommand, &OKResult, Index );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                            S E N T I N E L (Get Master Addr By Name)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request sentinelGetMasterAddrByNameCommand( T1_ Mastername )
    {
        Request r( "SENTINEL", "get-master-addr-by-name" );
        r << Mastername;
        return r;
    }

    auto sentinelGetMasterAddrByNameResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() == Response::Type::Array && Data.elements().size() == 2 )
            return std::make_pair( Data[0].string(), Data[1].string() );
        else
        {
            if( Data.type() == Response::Type::Null )
                ec = ::redis::make_error_code( ErrorCodes::no_data );
            else
                ec = ::redis::make_error_code( ErrorCodes::protocol_error );

            return std::make_pair( std::string(), std::string() );
        }
    }

    /// <returns>Pair of HostIP and Port</returns>
    template <class Connection, class T1_>
    auto sentinel_getMasterAddrByName(Connection& con, boost::system::error_code& ec, T1_ Mastername)
    {
        return Detail::sync_universal(con, ec, &sentinelGetMasterAddrByNameCommand<decltype(Mastername)>, &sentinelGetMasterAddrByNameResult, Mastername);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_sentinel_getMasterAddrByName(Connection& con, CompletionToken&& token, T1_ Mastername)
    {
        return Detail::async_universal(con, token, &sentinelGetMasterAddrByNameCommand<decltype(Mastername)>, &sentinelGetMasterAddrByNameResult, Mastername);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                            S E N T I N E L (Sentinels)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request sentinelSentinelsCommand( T1_ Mastername )
    {
        Request r( "SENTINEL", "sentinels" );
        r << Mastername;
        return r;
    }

    auto sentinelSentinelsResult( const Response& Data, boost::system::error_code& ec )
    {
        using ResultcontainerInner_t = std::map<std::string, std::string>;
        using Resultcontainer_t = std::list<ResultcontainerInner_t>;

        Resultcontainer_t Resultcontainer;

        if( Data.type() == Response::Type::Array && !Data.elements().empty() )
        {
            for( const auto& Current : Data.elements() )
            {
                ResultcontainerInner_t NameValueContainer;
                if( Current->type() == Response::Type::Array && (Current->elements().size() % 2 == 0) )
                {
                    for( auto InnerIterator = Current->elements().cbegin(); InnerIterator < Current->elements().cend(); ++InnerIterator )
                    {
                        std::string Name = (*InnerIterator)->string();
                        std::string Value = (*++InnerIterator)->string();

                        NameValueContainer.emplace( std::move( Name ), std::move( Value ) );
                    }
                }
                Resultcontainer.push_back( std::move( NameValueContainer ) );
            }
            return Resultcontainer;
        }
        else
            ec = ::redis::make_error_code( ErrorCodes::protocol_error );

        return Resultcontainer;
    }

    template <class Connection, class T1_>
    auto sentinel_sentinels(Connection& con, boost::system::error_code& ec, T1_ Mastername)
    {
        return Detail::sync_universal(con, ec, &sentinelSentinelsCommand<decltype(Mastername)>, &sentinelSentinelsResult, Mastername);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_sentinel_sentinels(Connection& con, CompletionToken&& token, T1_ Mastername)
    {
        return Detail::async_universal(con, token, &sentinelSentinelsCommand<decltype(Mastername)>, &sentinelSentinelsResult, Mastername);
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
