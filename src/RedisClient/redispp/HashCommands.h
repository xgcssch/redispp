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
    Request hdelCommand( const T1_& Key, const T2_& Field )
    {
        Request r( "HDEL" );
        r << Key << Field;
        return r;
    }

    template <class Connection, class T1_, class T2_>
    auto hdel( Connection& con, boost::system::error_code& ec, const T1_& Key, const T2_& Field )
    {
        return Detail::sync_universal( con, ec, &hdelCommand<decltype(Key), decltype(Field)>, &IntResult, std::ref(Key), std::ref(Field) );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_hdel( Connection& con, CompletionToken&& token, const T1_& Key, const T2_& Field )
    {
        return Detail::async_universal( con, token, &hdelCommand<decltype(Key), decltype(Field)>, &IntResult, std::ref(Key), std::ref(Field) );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H G E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_>
    Request hgetCommand( const T1_& Key, const T2_& Field )
    {
        Request r( "HGET" );
        r << Key << Field;
        return r;
    }

    template <class Connection, class T1_, class T2_>
    auto hget( Connection& con, boost::system::error_code& ec, const T1_& Key, const T2_& Field )
    {
        return Detail::sync_universal( con, ec, &hgetCommand<decltype(Key), decltype(Field)>, &getResult, std::ref(Key), std::ref(Field) );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_hset( Connection& con, CompletionToken&& token, const T1_& Key, const T2_& Field )
    {
        return Detail::async_universal( con, token, &hsetCommand<decltype(Key), decltype(Field)>, &getResult, std::ref(Key), std::ref(Field) );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                 H I N C R B Y
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_>
    Request hincrbyCommand( const T1_& Key, const T2_& Field, int64_t Increment )
    {
        Request r( "HINCRBY" );
        r << Key << Field << Increment;
        return r;
    }

    // Call Redis INCR Command syncronously taking a Key and returning an int64_t result
    template <class Connection, class T1_, class T2_>
    auto hincrby( Connection& con, boost::system::error_code& ec, const T1_& Key, const T2_& Field, int64_t Increment )
    {
        return Detail::sync_universal( con, ec, &hincrbyCommand<decltype(Key)>, &incrResult, std::ref(Key), std::ref(Field), Increment );
    }

    // Call Redis INCR Command asyncronously taking a Key and returning an int64_t result
    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_hincrby( Connection& con, CompletionToken&& token, const T1_& Key, const T2_& Field, int64_t Increment )
    {
        return Detail::async_universal( con, token, &hincrbyCommand<decltype(Key)>, &incrResult, std::ref(Key), std::ref(Field), Increment );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H S E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_, class T3_>
    Request hsetCommand( const T1_& Key, const T2_& Field, const T3_& Value )
    {
        Request r( "HSET" );
        r << Key << Field << Value;
        return r;
    }

    template <class Connection, class T1_, class T2_, class T3_>
    auto hset( Connection& con, boost::system::error_code& ec, const T1_& Key, const T2_& Field, const T3_& Value )
    {
        return Detail::sync_universal( con, ec, &hsetCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, std::ref(Key), std::ref(Field), std::ref(Value) );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_, class T3_>
    auto async_hset( Connection& con, CompletionToken&& token, const T1_& Key, const T2_& Field, const T3_& Value )
    {
        return Detail::async_universal( con, token, &hsetCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, std::ref(Key), std::ref(Field), std::ref(Value) );
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                   H S E T N X
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_, class T2_, class T3_>
    Request hsetnxCommand( const T1_& Key, const T2_& Field, const T3_& Value )
    {
        Request r( "HSETNX" );
        r << Key << Field << Value;
        return r;
    }

    template <class Connection, class T1_, class T2_, class T3_>
    auto hsetnx( Connection& con, boost::system::error_code& ec, const T1_& Key, const T2_& Field, const T3_& Value )
    {
        return Detail::sync_universal( con, ec, &hsetnxCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, std::ref(Key), std::ref(Field), std::ref(Value) );
    }

    template <class Connection, class CompletionToken, class T1_, class T2_, class T3_>
    auto async_hsetnx( Connection& con, CompletionToken&& token, const T1_& Key, const T2_& Field, const T3_& Value )
    {
        return Detail::async_universal( con, token, &hsetnxCommand<decltype(Key), decltype(Field), decltype(Value)>, &IntResult, std::ref(Key), std::ref(Field), std::ref(Value) );
    }
}

#endif
