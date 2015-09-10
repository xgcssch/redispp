#ifndef REDIS_MULTIPLEHOSTSCONNECTIONMANAGER_INCLUDED
#define REDIS_MULTIPLEHOSTSCONNECTIONMANAGER_INCLUDED

#include <string>
#include <list>
#include <memory>

#include <boost\asio.hpp>
#include <boost\bind.hpp>

#include "redisClient/SingleHostConnectionManager.h"
#include "redisClient/Error.h"

namespace redis
{
    class MultipleHostsConnectionManager
    {
        void commonContruction()
        {
            if (Hosts_.empty())
                throw std::out_of_range("Hostcontainer does not contain any hosts.");
            CurrentHostIterator_ = Hosts_.begin();
            spInnerConnectionManager_ = std::make_shared<SingleHostConnectionManager>(*CurrentHostIterator_);
        }

    public:
        typedef std::tuple<std::string, int> Host;
        typedef std::list<Host> HostContainer;

        MultipleHostsConnectionManager(const MultipleHostsConnectionManager&) = delete;
        MultipleHostsConnectionManager& operator=(const MultipleHostsConnectionManager&) = delete;

        MultipleHostsConnectionManager(boost::asio::io_service& io_service, const HostContainer& Hosts) :
            Hosts_(Hosts),
            Strand_(io_service)
        {
            commonContruction();
        }

        MultipleHostsConnectionManager(boost::asio::io_service& io_service, HostContainer&& Hosts) :
            Hosts_(std::move(Hosts)),
            Strand_(io_service)
        {
            commonContruction();
        }

        // The sync version is not threadsafe!
        boost::asio::ip::tcp::socket getConnectedSocket(boost::asio::io_service& io_service, boost::system::error_code& ec)
        {
            HostContainer::const_iterator StartHostIterator = CurrentHostIterator_;

            do
            {
                auto ConnectedSocket = spInnerConnectionManager_->getConnectedSocket(io_service, ec);
                if (!ec)
                    return ConnectedSocket;

                if (++CurrentHostIterator_ == Hosts_.end())
                    CurrentHostIterator_ == Hosts_.begin();

                spInnerConnectionManager_ = std::make_shared<SingleHostConnectionManager>(*CurrentHostIterator_);

            } while (ec && StartHostIterator != CurrentHostIterator_);

            ec = ::redis::make_error_code(ErrorCodes::no_usable_server);

            return boost::asio::ip::tcp::socket(io_service);
        }

        template <class	CompletionToken>
        auto async_getConnectedSocket(boost::asio::io_service& io_service, CompletionToken&& token)
        {
            using handler_type = typename boost::asio::handler_type<CompletionToken,
                void(boost::system::error_code ec, std::shared_ptr<boost::asio::ip::tcp::socket>)>::type;
            handler_type handler(std::forward<decltype(token)>(token));
            boost::asio::async_result<decltype(handler)> result(handler);

            std::shared_ptr<SingleHostConnectionManager> spInnerConnectionManager(std::atomic_load(&spInnerConnectionManager_));

            spInnerConnectionManager_->async_getConnectedSocket(io_service,
                                                                Strand_.wrap(
                                                                    //&doit
                                                                    [this, handler](const boost::system::error_code& ec, std::shared_ptr<boost::asio::ip::tcp::socket> spSocket) mutable
            {
                handler(ec, spSocket);
                //if (!ec)
                //    handler(ec, spSocket);
                //else
                //{

                //}
            }
            )
                );

            return result.get();
        }
    private:
        boost::asio::io_service::strand Strand_;
        HostContainer Hosts_;
        HostContainer::const_iterator CurrentHostIterator_;
        std::shared_ptr<SingleHostConnectionManager> spInnerConnectionManager_;
    };
}

#endif
