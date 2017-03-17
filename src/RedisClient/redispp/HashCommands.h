#pragma once

#ifndef REDISPP_HASH_COMMANDS_INCLUDED
#define REDISPP_HASH_COMMANDS_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include "redispp/Commands.h"

namespace redis
{
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H D E L
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_>
    Request hdelCommand( T1_ Key, T2_ Field )
    {
        Request r( "HDEL" );
        r << Key << Field;
        return r;
    }

    template <class Connection, class T1_, class T2_>
    auto hdel( Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Field )
    {
        return Detail::sync_universal( con, ec, &hdelCommand<decltype(Key), decltype(Field)>, &IntResult, Key, Field );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_hdel( Connection& con, CompletionToken&& token, T1_ Key, T2_ Field )
    {
        return Detail::async_universal( con, token, &hdelCommand<decltype(Key), decltype(Field)>, &IntResult, Key, Field );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H G E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_>
    Request hgetCommand( T1_ Key, T2_ Field )
    {
        Request r( "HGET" );
        r << Key << Field;
        return r;
    }

    template <class Connection, class T1_, class T2_>
    auto hget( Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Field )
    {
        return Detail::sync_universal( con, ec, &hgetCommand<decltype(Key), decltype(Field)>, &getResult, Key, Field );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_hset( Connection& con, CompletionToken&& token, T1_ Key, T2_ Field )
    {
        return Detail::async_universal( con, token, &hsetCommand<decltype(Key), decltype(Field)>, &getResult, Key, Field );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                 H I N C R B Y
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_>
    Request hincrbyCommand( T1_ Key, T2_ Field, int64_t Increment )
    {
        Request r( "HINCRBY" );
        r << Key << Field << Increment;
        return r;
    }

    // Call Redis INCR Command syncronously taking a Key and returning an int64_t result
    template <class Connection, class T1_, class T2_>
    auto hincrby( Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Field, int64_t Increment )
    {
        return Detail::sync_universal( con, ec, &hincrbyCommand<decltype(Key)>, &incrResult, Key, Field, Increment );
    }

    // Call Redis INCR Command asyncronously taking a Key and returning an int64_t result
    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_hincrby( Connection& con, CompletionToken&& token, T1_ Key, T2_ Field, int64_t Increment )
    {
        return Detail::async_universal( con, token, &hincrbyCommand<decltype(Key)>, &incrResult, Key, Field, Increment );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H S E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_, class T3_>
    Request hsetCommand( T1_ Key, T2_ Field, T3_ Value )
    {
        Request r( "HSET" );
        r << Key << Field << Value;
        return r;
    }

    template <class Connection, class T1_, class T2_, class T3_>
    auto hset( Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Field, T3_ Value )
    {
        return Detail::sync_universal( con, ec, &hsetCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, Key, Field, Value );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_, class T3_>
    auto async_hset( Connection& con, CompletionToken&& token, T1_ Key, T2_ Field, T3_ Value )
    {
        return Detail::async_universal( con, token, &hsetCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, Key, Field, Value );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H S E T N X
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_, class T3_>
    Request hsetnxCommand( T1_ Key, T2_ Field, T3_ Value )
    {
        Request r( "HSETNX" );
        r << Key << Field << Value;
        return r;
    }

    template <class Connection, class T1_, class T2_, class T3_>
    auto hsetnx( Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Field, T3_ Value )
    {
        return Detail::sync_universal( con, ec, &hsetnxCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, Key, Field, Value );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_, class T3_>
    auto async_hsetnx( Connection& con, CompletionToken&& token, T1_ Key, T2_ Field, T3_ Value )
    {
        return Detail::async_universal( con, token, &hsetnxCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, Key, Field, Value );
    }
}

#endif
