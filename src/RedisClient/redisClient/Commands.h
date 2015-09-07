#ifndef REDIS_COMMANDS_INCLUDED
#define REDIS_COMMANDS_INCLUDED

#include "redisClient\Response.h"

namespace redis
{
    template <class Connection, class RequestT_, class ResponseT_, class ... Types>
    auto sync_universal(Connection& con, boost::system::error_code& ec, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args)
    {
        auto theResponse = con.command(pPrepareFunction(args...), ec);
        if (ec)
            return decltype(pResponseFunction(theResponse->top(), ec))();

        return pResponseFunction(theResponse->top(), ec);
    }

    template <class Connection, class CompletionToken, class RequestT_, class ResponseT_, class ... Types>
    auto async_universal(Connection& con, CompletionToken&& token, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args)
    {
        using handler_type = typename boost::asio::handler_type<CompletionToken, void(boost::system::error_code, ResponseT_)>::type;
        handler_type handler(std::forward<decltype(token)>(token));
        boost::asio::async_result<decltype(handler)> result(handler);

        auto spRequest = std::make_shared<Request>(pPrepareFunction(args...));

        con.async_command(*spRequest, [&con, spRequest, handler, pResponseFunction](auto ec, const auto& Data)
        {
            if (ec)
                handler(ec, decltype(pResponseFunction(Data, ec))());
            else
                handler(ec, pResponseFunction(Data, ec));
        });

        return result.get();
    }

    template <class Connection, class CompletionToken, class RequestT_, class ResponseT_, class ... Types>
    auto async_universal_void(Connection& con, CompletionToken&& token, RequestT_ pPrepareFunction, ResponseT_ pResponseFunction, Types ... args)
    {
        using handler_type = typename boost::asio::handler_type<CompletionToken, void(boost::system::error_code, ResponseT_)>::type;
        handler_type handler(std::forward<decltype(token)>(token));
        boost::asio::async_result<decltype(handler)> result(handler);

        auto spRequest = std::make_shared<Request>(pPrepareFunction(args...));

        con.async_command(*spRequest, [&con, spRequest, handler, pResponseFunction](auto ec, const auto& Data)
        {
            if (!ec)
                pResponseFunction(Data, ec);

            handler(ec);
        });

        return result.get();
    }


    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     S E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    enum class SetOptions { None, SetIfNotExist, SetIfExist };

    template <class T1_, class T2_>
    Request prepareRequest_set(T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds, SetOptions Options)
    {
        Request r("SET");
        r << Key << Value;
        if (ExpireTimeInMilliseconds != std::chrono::milliseconds::zero())
            r << "PX" << ExpireTimeInMilliseconds.count();
        if (Options == SetOptions::SetIfNotExist)
            r << "NX";
        if (Options == SetOptions::SetIfExist)
            r << "XX";
        return r;
    }

    auto processResult_set(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() == Response::Type::SimpleString && Data.string() == "OK")
            return true;
        else
            if (Data.type() == Response::Type::Null)
                return false;
            else
            {
                ec = ::redis::make_error_code(ErrorCodes::protocol_error);
                return false;
            }
    }

    template <class Connection, class T1_, class T2_>
    auto set(Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None)
    {
        return sync_universal(con, ec, &prepareRequest_set<decltype(Key), decltype(Value) >, &processResult_set, Key, Value, ExpireTimeInMilliseconds, Options);
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_set(Connection& con, CompletionToken&& token, T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None)
    {
        return async_universal(con, token, &prepareRequest_set<decltype(Key), decltype(Value) >, &processResult_set, Key, Value, ExpireTimeInMilliseconds, Options);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     P I N G
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Request prepareRequest_ping()
    {
        return Request("PING");
    }

    void processResult_ping(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() != Response::Type::SimpleString || Data.string() != "PONG")
            ec = ::redis::make_error_code(ErrorCodes::protocol_error);
    }

    template <class Connection>
    auto ping(Connection& con, boost::system::error_code& ec)
    {
        return sync_universal(con, ec, &prepareRequest_ping, &processResult_ping);
    }

    template <class Connection, class CompletionToken>
    auto async_ping(Connection& con, CompletionToken&& token)
    {
        return async_universal_void(con, token, &prepareRequest_ping, &processResult_ping);
    }
}

#endif
