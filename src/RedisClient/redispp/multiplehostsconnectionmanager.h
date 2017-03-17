#ifndef REDISPP_MULTIPLEHOSTSCONNECTIONMANAGER_INCLUDED
#define REDISPP_MULTIPLEHOSTSCONNECTIONMANAGER_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include <string>
#include <list>
#include <memory>
#include <shared_mutex>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "redispp/SingleHostConnectionManager.h"
#include "redispp/Error.h"

namespace redis
{
    class MultipleHostsConnectionManager
    {
    public:
        typedef std::tuple<std::string, int> Host;

        class HostContainer
        {
        public:
            typedef std::list<Host> ContainerType;

        private:
            ContainerType Container_;
            mutable std::shared_timed_mutex Mutex_;

        public:

            HostContainer( const ContainerType& Container ) :
                Container_( Container )
            {}
            HostContainer( ContainerType&& Container ) :
                Container_( std::move(Container) )
            {}

            ContainerType get() const
            {
                std::shared_lock<std::shared_timed_mutex> TheLock( Mutex_ );
                return Container_;
            }
            void set( const ContainerType& Container )
            {
                std::unique_lock<std::shared_timed_mutex> TheLock( Mutex_ );
                Container_ = Container;
            }
            void set( ContainerType&& Container )
            {
                std::unique_lock<std::shared_timed_mutex> TheLock( Mutex_ );
                Container_ = std::move(Container);
            }
            const ContainerType& container() const { return Container_; }
        };

        class Instance
        {
        public:
            Instance( const Instance& ) = default;
            Instance& operator=( const Instance& ) = delete;

            Instance( const HostContainer& Hosts ) :
                Hosts_( Hosts.get() )
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
            HostContainer::ContainerType Hosts_;
            HostContainer::ContainerType::const_iterator CurrentHostIterator_;
            std::shared_ptr<SingleHostConnectionManager> spInnerConnectionManager_;
        };

        MultipleHostsConnectionManager(const MultipleHostsConnectionManager&) = delete;
        MultipleHostsConnectionManager& operator=(const MultipleHostsConnectionManager&) = delete;

        MultipleHostsConnectionManager(boost::asio::io_service& io_service, const HostContainer::ContainerType& Hosts) :
            Hosts_(Hosts),
            Strand_(io_service)
        {
            commonContruction( Hosts );
        }

        MultipleHostsConnectionManager(boost::asio::io_service& io_service, HostContainer::ContainerType&& Hosts) :
            Hosts_(std::move(Hosts)),
            Strand_(io_service)
        {
            commonContruction( Hosts_.container() );
        }

        Instance getInstance() const
        {
            return Instance( Hosts_ );
        }

    private:
        void commonContruction( const HostContainer::ContainerType& Hosts )
        {
            if( Hosts.empty() )
                throw std::out_of_range( "Hostcontainer does not contain any hosts." );
        }

        boost::asio::io_service::strand Strand_;
        HostContainer Hosts_;
    };
}

#endif
