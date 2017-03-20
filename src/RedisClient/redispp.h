#pragma once

#ifndef REDISPP_REDISPP_INCLUDED
#define REDISPP_REDISPP_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

namespace redis
{
    using Host = std::tuple<std::string, int>;

    class NullNotificationSink
    {
    public:
        template <typename... Args>
        void debug( const Args & ... args ) {}
        template <typename... Args>
        void trace( const Args & ... args ) {}
        template <typename... Args>
        void warning( const Args & ... args ) {}
        template <typename... Args>
        void error( const Args & ... args ) {}
    };
}

std::ostream& operator<<( std::ostream& Out, const redis::Host& h )
{
    Out << "[" << std::get<0>(h) << ":" << std::get<1>(h) << "]";
    return Out;
}

#endif
