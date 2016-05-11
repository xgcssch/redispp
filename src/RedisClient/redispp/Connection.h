#ifndef REDIS_CONNECTION_INCLUDED
#define REDIS_CONNECTION_INCLUDED

#include "redispp\Commands.h"
#include "redispp\Response.h"
#include "redispp\SocketConnectionManager.h"

namespace redis
{
    class Pipeline
    {
        std::list<Request> Requests_;
        Request::BufferSequence_t Buffers_;

    public:
        Pipeline( const Pipeline& ) = delete;
        Pipeline& operator=( const Pipeline& ) = delete;
        Pipeline() {}

        Pipeline& operator<<( Request&& Command )
        {
            const auto& Buffersequence( Command.bufferSequence() );
            Buffers_.insert( Buffers_.end(), Buffersequence.begin(), Buffersequence.end() );

            Requests_.emplace_back( std::move( Command ) );

            return *this;
        }
        const Request::BufferSequence_t& bufferSequence() const { return Buffers_; }
        size_t requestCount() const { return Requests_.size(); }
    };

    class PipelineResult
    {
        std::shared_ptr<Response::ElementContainer>           spResponses_;
        std::shared_ptr<ResponseHandler::BufferContainerType> spBufferContainer_;
    public:
        PipelineResult( const PipelineResult& ) = default;
        PipelineResult& operator=( const PipelineResult& ) = default;
        PipelineResult( std::shared_ptr<Response::ElementContainer>& spResponses, std::shared_ptr<ResponseHandler::BufferContainerType>& spBufferContainer ) :
            spResponses_( spResponses ),
            spBufferContainer_( spBufferContainer )
        {}

        const Response::ElementContainer::value_type::element_type& operator[]( size_t Position )
        {
            return *(spResponses_->at( Position ));
        }
    };

    class ConnectionBase : std::enable_shared_from_this<ConnectionBase>
    {
        ConnectionBase(const ConnectionBase&) = delete;
        ConnectionBase& operator=(const ConnectionBase&) = delete;

    public:
        ConnectionBase(boost::asio::io_service& io_service, int64_t Index ) :
            io_service_(io_service),
            Strand_(io_service),
            Socket_(io_service),
            Index_( Index )
        {}

        void requestCreated(ResponseHandler::ResponseHandle ResponseHandler)
        {
            _ResponseQueue.push(ResponseHandler);
        }

        void requestCompleted()
        {
            _ResponseQueue.pop();
        }

    protected:
        boost::asio::io_service& io_service_;
        boost::asio::io_service::strand Strand_;
        boost::asio::ip::tcp::socket Socket_;
        std::queue<ResponseHandler::ResponseHandle> _ResponseQueue;
        int64_t Index_;
        std::string LastServerError_;
    };

    template <class ConnectionManagerType>
    class Connection : private ConnectionBase
    {
    public:
        Connection(boost::asio::io_service& io_service, const ConnectionManagerType& Manager, int64_t Index=0 ) :
            ConnectionBase( io_service, Index ),
            ConnectionManagerInstance_(Manager.getInstance())
        {}

        auto transmit( const Request& Command, boost::system::error_code& ec )
        {
            auto res = std::make_unique<ResponseHandler>();
            for( ;;)
            {
                if( !Socket_.is_open() )
                {
                    auto Socket = ConnectionManagerInstance_.getConnectedSocket( io_service_, ec );
                    if( ec )
                        return res;
                    else
                    {
                        if( Index_ )
                        {
                            Detail::SocketConnectionManager scm( Socket );
                            Connection<Detail::SocketConnectionManager> CurrentConnection( io_service_, scm );

                            redis::select( CurrentConnection, ec, Index_ );

                            Socket_ = CurrentConnection.passSocket();
                        }
                        else
                            Socket_ = std::move( Socket );
                    }
                }

                boost::asio::write( Socket_, Command.bufferSequence(), ec );
                if( ec )
                {
                    Socket_.close();

                    // Try again!
                    continue;
                }

                break;
            }

            size_t BytesRead;
            do
            {
                BytesRead = Socket_.read_some( boost::asio::buffer( res->buffer() ), ec );
                if( ec )
                {
                    Socket_.close();
                    return res;
                }

            } while( !res->dataReceived( BytesRead ) );

            return res;
        }

        PipelineResult transmit(const Pipeline& thePipeline, boost::system::error_code& ec)
        {
            ResponseHandler res;
            size_t ExpectedResponses = thePipeline.requestCount();
            auto spResponses = std::make_shared<std::vector<std::shared_ptr<redis::Response>>>( ExpectedResponses );

            for( ;;)
            {
                if( !Socket_.is_open() )
                {
                    auto Socket = ConnectionManagerInstance_.getConnectedSocket( io_service_, ec );
                    if( ec )
                        return PipelineResult( spResponses, res.bufferContainer() );
                    else
                    {
                        if( Index_ )
                        {
                            Detail::SocketConnectionManager scm( Socket );
                            Connection<Detail::SocketConnectionManager> CurrentConnection( io_service_, scm );

                            redis::select( CurrentConnection, ec, Index_ );

                            Socket_ = CurrentConnection.passSocket();
                        }
                        else
                            Socket_ = std::move( Socket );
                    }
                }

                boost::asio::write( Socket_, thePipeline.bufferSequence(), ec );
                if( ec )
                {
                    Socket_.close();

                    // Try again!
                    continue;
                }

                break;
            }

            bool ParseCompleted = false;

            for( size_t CurrentResponse = 0; CurrentResponse < ExpectedResponses; ++CurrentResponse )
            {
                size_t BytesRead;
                do
                {
                    BytesRead = Socket_.read_some( boost::asio::buffer( res.buffer() ), ec );
                    if( ec )
                    {
                        Socket_.close();
                        return PipelineResult( spResponses, res.bufferContainer() );
                    }

                } while( !res.dataReceived( BytesRead ) );

                do
                {
                    spResponses->at(CurrentResponse++) = res.spTop();
                } while( res.commit( true ) );
            }

            return PipelineResult( spResponses, res.bufferContainer() );
        }

        template <class	CompletionToken>
        auto async_command(const Request& Command, CompletionToken&& token)
        {
            using handler_type = typename boost::asio::handler_type<CompletionToken,
                void(boost::system::error_code, Response Data)>::type;
            handler_type handler(std::forward<decltype(token)>(token));
            boost::asio::async_result<decltype(handler)> result(handler);

            if (!Socket_.is_open())
            {
                ConnectionManagerInstance_.async_getConnectedSocket(io_service_,
                                                  [this, &Command, handler](const boost::system::error_code& ec, std::shared_ptr<boost::asio::ip::tcp::socket>& spSocket) mutable {
                    if (ec)
                    {
                        handler(ec, Response());
                    }
                    else
                    {
                        Socket_ = std::move(*spSocket);

                        internalSendData(Command, std::forward<handler_type>(handler));
                    }
                });
            }
            else
                internalSendData(Command, std::forward<handler_type>(handler));

            return result.get();
        }

        template <class	ConnectHandler>
        void internalReceiveData(std::shared_ptr<ResponseHandler>& spServerResponse, ConnectHandler& handler)
        {
            Socket_.async_read_some(boost::asio::buffer(spServerResponse->buffer()),
                                    [this, spServerResponse, handler](const boost::system::error_code& ec, std::size_t BytesReceived) mutable
            {
                if (ec)
                    handler(ec, Response());
                else
                {
                    if (!spServerResponse->dataReceived(BytesReceived))
                        internalReceiveData( spServerResponse, handler );
                    else
                        handler(ec, spServerResponse->top());
                }
            });
        }

        template <class	ConnectHandler>
        void internalSendData(const Request& Command, ConnectHandler&& handler)
        {
            boost::system::error_code ec;

            Socket_.async_send(Command.bufferSequence(),
                               [this, handler](const boost::system::error_code& ec, std::size_t bytes_transferred) mutable {
                if (ec)
                    handler(ec, Response());
                else
                {
                    auto spServerResponse = std::make_shared<ResponseHandler>();

                    internalReceiveData(std::move(spServerResponse), std::forward<ConnectHandler>(handler));
                }
            });
        }

        boost::asio::ip::tcp::socket passSocket()
        {
            return boost::asio::ip::tcp::socket( std::move(Socket_) );
        }

        std::tuple<std::string, int> remote_endpoint()
        {
            return std::make_tuple( Socket_.remote_endpoint().address().to_string(), Socket_.remote_endpoint().port() );
        }

        typename ConnectionManagerType::Instance& instance()
        {
            return ConnectionManagerInstance_;
        }
        const std::string& lastServerError() const
        {
            return LastServerError_;
        }
        void setLastServerError( const std::string& LastServerError )
        {
            LastServerError_ = LastServerError;
        }

    private:
        typename ConnectionManagerType::Instance ConnectionManagerInstance_;
    };
}

#endif
