#pragma once

#ifndef REDISPP_REDISPP_INCLUDED
#define REDISPP_REDISPP_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

namespace redis
{
    class NullStream
    {
    public:
        template<class DebugStreamType_>
        NullStream& operator<<( const DebugStreamType_& ) {
            return *this;
        }
    };

}

#endif
