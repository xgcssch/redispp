#ifndef REDISPP_SINGLEHOSTCONNECTIONMANAGER_INCLUDED
#define REDISPP_SINGLEHOSTCONNECTIONMANAGER_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include <string>
#include <boost\asio.hpp>

namespace redis
{
    class SingleHostConnectionManager
    {
    public:
        class Instance
        {
        public:
            Instance( const Instance& ) = default;
            Instance& operator=( const Instance& ) = delete;

            Instance( const SingleHostConnectionManager& shcm ) :
                SingleHostConnectionManager_( shcm )
            {}

            boost::asio::ip::tcp::socket getConnectedSocket( boost::asio::io_service& io_service, boost::system::error_code& ec )
            {
                boost::asio::ip::tcp::socket Socket( io_service );

                boost::asio::ip::tcp::resolver resolver( io_service );
                boost::asio::ip::tcp::resolver::query query( SingleHostConnectionManager_.Servername_, std::to_string( SingleHostConnectionManager_.Port_ ) );
                boost::asio::connect( Socket, resolver.resolve( query ), ec );

                return Socket;
            }

            template <class	CompletionToken>
            auto async_getConnectedSocket( boost::asio::io_service& io_service, CompletionToken&& token )
            {
                using handler_type = typename boost::asio::handler_type<CompletionToken,
                    void( boost::system::error_code ec, std::shared_ptr<boost::asio::ip::tcp::socket> Socket )>::type;
                handler_type handler( std::forward<CompletionToken&&>( token ) );
                boost::asio::async_result<decltype(handler)> result( handler );

                std::shared_ptr<boost::asio::ip::tcp::resolver> spResolver = std::make_shared<boost::asio::ip::tcp::resolver>( io_service );
                boost::asio::ip::tcp::resolver::query query( SingleHostConnectionManager_.Servername_, std::to_string( SingleHostConnectionManager_.Port_ ) );
                spResolver->async_resolve( query,
                                           [&io_service, handler, spResolver]( const boost::system::error_code& error, const boost::asio::ip::tcp::resolver::iterator& iterator ) mutable {
                    if( error )
                        handler( error, std::shared_ptr<boost::asio::ip::tcp::socket>() );
                    else
                    {
                        std::shared_ptr<boost::asio::ip::tcp::socket> spSocket = std::make_shared<boost::asio::ip::tcp::socket>( io_service );
                        boost::asio::async_connect( *spSocket, iterator,
                                                    [spSocket, handler]( const boost::system::error_code& error, const boost::asio::ip::tcp::resolver::iterator& iterator ) mutable {
                            handler( error, spSocket );
                        }
                        );
                    }
                } );

                return result.get();
            }
        private:
            const SingleHostConnectionManager& SingleHostConnectionManager_;
        };

        SingleHostConnectionManager(const SingleHostConnectionManager&) = delete;
        SingleHostConnectionManager& operator=(const SingleHostConnectionManager&) = delete;

        SingleHostConnectionManager(const std::string& Servername = "localhost", int Port = 6379) :
            Servername_(Servername),
            Port_(Port)
        {}

        SingleHostConnectionManager(const std::tuple<std::string,int>& Servername) :
            SingleHostConnectionManager(std::get<0>(Servername), std::get<1>(Servername))
        {}

        Instance getInstance() const
        {
            return Instance( *this );
        }
    private:
        std::string Servername_;
        int Port_;
    };
}

#endif
