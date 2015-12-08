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
        }

    public:
        typedef std::tuple<std::string, int> Host;
        typedef std::list<Host> HostContainer;

        class Instance
        {
        public:
            Instance( const Instance& ) = default;
            Instance& operator=( const Instance& ) = delete;

            Instance( const HostContainer& Hosts ) :
                Hosts_( Hosts )
            {
                CurrentHostIterator_ = Hosts_.begin();
                spInnerConnectionManager_ = std::make_shared<SingleHostConnectionManager>( *CurrentHostIterator_ );
            }

            boost::asio::ip::tcp::socket getConnectedSocket( boost::asio::io_service& io_service, boost::system::error_code& ec )
            {
                for( const auto& SingleHost : Hosts_ )
                {
                    auto ConnectedSocket = SingleHostConnectionManager( SingleHost ).getInstance().getConnectedSocket( io_service, ec );
                    if( !ec )
                        return ConnectedSocket;
                }

                ec = ::redis::make_error_code( ErrorCodes::no_usable_server );

                return boost::asio::ip::tcp::socket( io_service );
            }

            template <class	CompletionToken>
            auto async_getConnectedSocket( boost::asio::io_service& io_service, CompletionToken&& token )
            {
                using handler_type = typename boost::asio::handler_type<CompletionToken,
                    void( boost::system::error_code ec, std::shared_ptr<boost::asio::ip::tcp::socket> )>::type;
                handler_type handler( std::forward<decltype(token)>( token ) );
                boost::asio::async_result<decltype(handler)> result( handler );

                std::shared_ptr<SingleHostConnectionManager> spInnerConnectionManager( std::atomic_load( &spInnerConnectionManager_ ) );

                spInnerConnectionManager_->async_getConnectedSocket( io_service,
                                                                     Strand_.wrap(
                                                                         //&doit
                                                                         [this, handler]( const boost::system::error_code& ec, std::shared_ptr<boost::asio::ip::tcp::socket> spSocket ) mutable
                        {
                            handler( ec, spSocket );
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

            size_t size() const
            {
                return Hosts_.size();
            }

            void shiftHosts()
            {
                if( Hosts_.size() > 1 )
                    std::swap( *(Hosts_.begin()), *--(Hosts_.end()) );
            }

        private:
            HostContainer Hosts_;
            HostContainer::const_iterator CurrentHostIterator_;
            std::shared_ptr<SingleHostConnectionManager> spInnerConnectionManager_;
        };

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

        Instance getInstance() const
        {
            return Instance( Hosts_ );
        }

    private:
        boost::asio::io_service::strand Strand_;
        HostContainer Hosts_;
    };
}

#endif
