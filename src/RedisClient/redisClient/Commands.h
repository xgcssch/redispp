#ifndef REDIS_COMMANDS_INCLUDED
#define REDIS_COMMANDS_INCLUDED

#include "redisClient\Response.h"

#include <boost\optional.hpp>

#include <chrono>
#include <list>
#include <map>
#include <iterator>

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

        con.async_command(*spRequest, [&con, spRequest, handler, pResponseFunction](const auto& ec, const auto& Data)
        {
            if (ec)
            {
                boost::system::error_code ec2;
                handler(ec2, decltype(pResponseFunction(Data, ec2))());
            }
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

        con.async_command(*spRequest, [&con, spRequest, handler, pResponseFunction](const auto& ec, const auto& Data)
        {
            if (!ec)
            {
                boost::system::error_code ec2;
                pResponseFunction(Data, ec2);
            }

            handler(ec);
        });

        return result.get();
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     G E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request prepareRequest_get(T1_ Key)
    {
        Request r("GET");
        r << Key;
        return r;
    }

    boost::optional<boost::asio::const_buffer> processResult_get(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() == Response::Type::BulkString)
            return boost::asio::buffer(Data.data(), Data.size());
        else
            if (Data.type() != Response::Type::Null)
                ec = ::redis::make_error_code(ErrorCodes::protocol_error);

        return boost::none;
    }

    template <class Connection, class T1_>
    auto get(Connection& con, boost::system::error_code& ec, T1_ Key)
    {
        return sync_universal(con, ec, &prepareRequest_get<decltype(Key)>, &processResult_get, Key);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_get(Connection& con, CompletionToken&& token, T1_ Key)
    {
        return async_universal(con, token, &prepareRequest_get<decltype(Key)>, &processResult_get, Key);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     I N C R
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request prepareRequest_incr(T1_ Key)
    {
        Request r("INCR");
        r << Key;
        return r;
    }

    boost::optional<boost::asio::const_buffer> processResult_incr(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() != Response::Type::Integer)
            ec = ::redis::make_error_code(ErrorCodes::protocol_error);

        return boost::asio::buffer(Data.data(), Data.size());
    }

    template <class Connection, class T1_>
    auto incr(Connection& con, boost::system::error_code& ec, T1_ Key)
    {
        return sync_universal(con, ec, &prepareRequest_incr<decltype(Key)>, &processResult_incr, Key);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_incr(Connection& con, CompletionToken&& token, T1_ Key)
    {
        return async_universal(con, token, &prepareRequest_incr<decltype(Key)>, &processResult_incr, Key);
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

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                            S E N T I N E L (Get Master Addr By Name)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request prepareRequest_SentinelGetMasterAddrByName(T1_ Mastername)
    {
        Request r("SENTINEL", "get-master-addr-by-name");
        r << Mastername;
        return r;
    }

    auto processResult_SentinelGetMasterAddrByName(const Response& Data, boost::system::error_code& ec)
    {
        if (Data.type() == Response::Type::Array && Data.elements().size() == 2)
            return std::make_pair(Data[0].string(), Data[1].string());
        else
        {
            if (Data.type() == Response::Type::Null)
                ec = ::redis::make_error_code(ErrorCodes::no_data);
            else
                ec = ::redis::make_error_code(ErrorCodes::protocol_error);

            return std::make_pair(std::string(), std::string());
        }
    }

    template <class Connection, class T1_>
    auto sentinel_getMasterAddrByName(Connection& con, boost::system::error_code& ec, T1_ Mastername)
    {
        return sync_universal(con, ec, &prepareRequest_SentinelGetMasterAddrByName<decltype(Mastername)>, &processResult_SentinelGetMasterAddrByName, Mastername);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_sentinel_getMasterAddrByName(Connection& con, CompletionToken&& token, T1_ Mastername)
    {
        return async_universal(con, token, &prepareRequest_SentinelGetMasterAddrByName<decltype(Mastername)>, &processResult_SentinelGetMasterAddrByName, Mastername);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                            S E N T I N E L (Sentinels)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    template <class T1_>
    Request prepareRequest_SentinelSentinels(T1_ Mastername)
    {
        Request r("SENTINEL", "sentinels");
        r << Mastername;
        return r;
    }

    auto processResult_SentinelSentinels(const Response& Data, boost::system::error_code& ec)
    {
        using ResultcontainerInner_t = std::map<std::string, std::string>;
        using Resultcontainer_t = std::list<ResultcontainerInner_t>;

        Resultcontainer_t Resultcontainer;

        if (Data.type() == Response::Type::Array && !Data.elements().empty())
        {
            for (const auto& Current : Data.elements())
            {
                ResultcontainerInner_t NameValueContainer;
                if (Current->type() == Response::Type::Array && (Current->elements().size() % 2 == 0))
                {
                    for (auto InnerIterator = Current->elements().cbegin(); InnerIterator < Current->elements().cend(); ++InnerIterator )
                    {
                        std::string Name = (*InnerIterator)->string();
                        std::string Value = (*++InnerIterator)->string();

                        NameValueContainer.emplace( std::move(Name), std::move(Value) );
                    }
                }
                Resultcontainer.push_back(std::move(NameValueContainer));
            }
            return Resultcontainer;
        }
        else
            ec = ::redis::make_error_code(ErrorCodes::protocol_error);

        return Resultcontainer;
    }

    template <class Connection, class T1_>
    auto sentinel_sentinels(Connection& con, boost::system::error_code& ec, T1_ Mastername)
    {
        return sync_universal(con, ec, &prepareRequest_SentinelSentinels<decltype(Mastername)>, &processResult_SentinelSentinels, Mastername);
    }

    template <class Connection, class CompletionToken, class T1_>
    auto async_sentinel_sentinels(Connection& con, CompletionToken&& token, T1_ Mastername)
    {
        return async_universal(con, token, &prepareRequest_SentinelSentinels<decltype(Mastername)>, &processResult_SentinelSentinels, Mastername);
    }

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //                                                     S E T
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    enum class SetOptions { None, SetIfNotExist, SetIfExist };

    namespace Detail {
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
    }

    template <class Connection, class T1_, class T2_>
    auto set(Connection& con, boost::system::error_code& ec, T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None)
    {
        return Detail::sync_universal(con, ec, &prepareRequest_set<decltype(Key), decltype(Value)>, &processResult_set, Key, Value, ExpireTimeInMilliseconds, Options);
    }

    template <class Connection, class CompletionToken, class T1_, class T2_>
    auto async_set(Connection& con, CompletionToken&& token, T1_ Key, T2_ Value, std::chrono::milliseconds ExpireTimeInMilliseconds = std::chrono::milliseconds::zero(), SetOptions Options = SetOptions::None)
    {
        return Detail::async_universal(con, token, &prepareRequest_set<decltype(Key), decltype(Value)>, &processResult_set, Key, Value, ExpireTimeInMilliseconds, Options);
    }

}

#endif
