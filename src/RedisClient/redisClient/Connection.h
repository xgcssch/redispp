#ifndef REDIS_CONNECTION_INCLUDED
#define REDIS_CONNECTION_INCLUDED

#include "redisClient\Response.h"

namespace redis
{
    class ConnectionBase : std::enable_shared_from_this<ConnectionBase>
    {
        ConnectionBase(const ConnectionBase&) = delete;
        ConnectionBase& operator=(const ConnectionBase&) = delete;

    public:
        ConnectionBase(boost::asio::io_service& io_service) :
            io_service_(io_service),
            Strand_(io_service),
            Socket_(io_service)
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
        bool Subscribed_ = false;
    };

    template <class ConnectionManagerType>
    class Connection : ConnectionBase
    {
    public:
        typedef std::shared_ptr<Connection> x;

        Connection(boost::asio::io_service& io_service, ConnectionManagerType& Manager) :
            ConnectionBase(io_service),
            Manager_(Manager)
        {}

        auto command(const Request& Command, boost::system::error_code& ec)
        {
            auto res = std::make_unique<ResponseHandler>();
            if (!Socket_.is_open())
            {
                auto Socket = Manager_.getConnectedSocket(io_service_, ec);
                if (ec)
                    return res;
                else
                    Socket_ = std::move(Socket);
            }

            boost::asio::write(Socket_, Command.bufferSequence(), ec);
            if (ec)
                return res;

            size_t BytesRead;
            do
            {
                BytesRead = Socket_.read_some(boost::asio::buffer(res->buffer()), ec);
                if (ec)
                    return res;

            } while (!res->dataReceived(BytesRead));

            return res;
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
                Manager_.async_getConnectedSocket(io_service_,
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

    private:
        ConnectionManagerType& Manager_;
    };
}

#endif
