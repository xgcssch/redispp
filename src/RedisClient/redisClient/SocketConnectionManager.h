#ifndef REDIS_SOCKETCONNECTIONMANAGER_INCLUDED
#define REDIS_SOCKETCONNECTIONMANAGER_INCLUDED

#include <string>
#include <boost\asio.hpp>

namespace redis
{
    namespace Detail
    {
        class SocketConnectionManager
        {
        public:
            class Instance
            {
            public:
                Instance( const Instance& ) = default;
                Instance& operator=( const Instance& ) = delete;

                Instance( boost::asio::ip::tcp::socket& Socket ) :
                    Socket_( Socket )
                {}

                boost::asio::ip::tcp::socket getConnectedSocket( boost::asio::io_service& io_service, boost::system::error_code& ec )
                {
                    return boost::asio::ip::tcp::socket( std::move( Socket_ ) );
                }

            private:
                boost::asio::ip::tcp::socket& Socket_;
            };

            SocketConnectionManager( const SocketConnectionManager& ) = delete;
            SocketConnectionManager& operator=( const SocketConnectionManager& ) = delete;

            SocketConnectionManager( boost::asio::ip::tcp::socket& Socket ) :
                Socket_( std::move( Socket ) )
            {}

            Instance getInstance() const
            {
                return Instance( Socket_ );
            }
        private:
            mutable boost::asio::ip::tcp::socket Socket_;
        };
    }
}

#endif
