#pragma once

#ifndef REDISPP_SENTINEL_COMMANDS_INCLUDED
#define REDISPP_SENTINEL_COMMANDS_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include "redispp\Commands.h"

namespace redis
{
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                            S E N T I N E L (Get Master Addr By Name)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request sentinelGetMasterAddrByNameCommand( const T1_& Mastername )
    {
        Request r( "SENTINEL", "get-master-addr-by-name" );
        r << Mastername;
        return r;
    }

    inline Host sentinelGetMasterAddrByNameResult( const Response& Data, boost::system::error_code& ec )
    {
        if( Data.type() == Response::Type::Array && Data.elements().size() == 2 )
            return std::make_tuple( Data[0].string(), std::stoi( Data[1].string() ) );
        else
        {
            if( Data.type() == Response::Type::Null )
                ec = ::redis::make_error_code( ErrorCodes::no_data );
            else
                ec = ::redis::make_error_code( ErrorCodes::protocol_error );

            return std::make_tuple( std::string(), 0 );
        }
    }

    /// <returns>Pair of HostIP and Port</returns>
    template <class Connection, class T1_>
    auto sentinel_getMasterAddrByName(Connection& con, boost::system::error_code& ec, const T1_& Mastername)
    {
        return Detail::sync_universal(con, ec, &sentinelGetMasterAddrByNameCommand<decltype(Mastername)>, &sentinelGetMasterAddrByNameResult, std::ref(Mastername));
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_sentinel_getMasterAddrByName(Connection& con, CompletionToken&& token, const T1_& Mastername)
    {
        return Detail::async_universal(con, token, &sentinelGetMasterAddrByNameCommand<decltype(Mastername)>, &sentinelGetMasterAddrByNameResult, std::ref(Mastername));
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                            S E N T I N E L (Sentinels)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request sentinelSentinelsCommand( const T1_& Mastername )
    {
        Request r( "SENTINEL", "sentinels" );
        r << Mastername;
        return r;
    }

    inline auto sentinelSentinelsResult( const Response& Data, boost::system::error_code& ec )
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
    auto sentinel_sentinels(Connection& con, boost::system::error_code& ec, const T1_& Mastername)
    {
        return Detail::sync_universal(con, ec, &sentinelSentinelsCommand<decltype(Mastername)>, &sentinelSentinelsResult, std::ref(Mastername));
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_sentinel_sentinels(Connection& con, CompletionToken&& token, const T1_& Mastername)
    {
        return Detail::async_universal(con, token, &sentinelSentinelsCommand<decltype(Mastername)>, &sentinelSentinelsResult, std::ref(Mastername));
    }

}

#endif
