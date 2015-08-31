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

        void requestCreated(Response::ResponseHandle Response)
        {
            _ResponseQueue.push(Response);
        }

        void requestCompleted()
        {
            _ResponseQueue.pop();
        }

    protected:
        boost::asio::io_service& io_service_;
        boost::asio::io_service::strand Strand_;
        boost::asio::ip::tcp::socket Socket_;
        std::queue<Response::ResponseHandle> _ResponseQueue;
    };

    template <class ConnectionManagerType>
    class Connection : ConnectionBase
    {
    public:
        typedef std::shared_ptr<Connection> x;

        Connection(boost::asio::io_service& io_service, const ConnectionManagerType& Manager) :
            ConnectionBase(io_service),
            Manager_(Manager)
        {}

        std::string command(const std::string& Command, boost::system::error_code& ec)
        {
            if (!Socket_.is_open())
            {
                auto Socket = Manager_.getConnectedSocket(io_service_, ec);
                if (!ec)
                    Socket_ = std::move(Socket);
            }

            std::string CommandWithLineEnding(Command);
            CommandWithLineEnding += "\r\n";
            Socket_.send(boost::asio::buffer(CommandWithLineEnding));
            std::array<char, 1024> Buffer;
            auto BytesRead = Socket_.read_some(boost::asio::buffer(Buffer));
            return std::string(Buffer.data(), BytesRead);
        }

        template <class	CompletionToken>
        auto async_command(const std::string& Command, BOOST_ASIO_MOVE_ARG(CompletionToken) token)
        {
            using handler_type = typename boost::asio::handler_type<CompletionToken,
                void(boost::system::error_code, std::string Data)>::type;
            handler_type handler(std::forward<decltype(token)>(token));
            boost::asio::async_result<decltype(handler)> result(handler);

            if (!Socket_.is_open())
            {
                Manager_.async_getConnectedSocket(io_service_,
                                                  [this, Command, handler](boost::system::error_code ec, boost::asio::ip::tcp::socket& Socket) mutable {
                    if (ec)
                        return handler(ec, std::string());
                    else
                    {
                        Socket_ = std::move(Socket);

                        internalSendData(Command, std::forward<handler_type>(handler));
                    }
                });
            }
            else
                internalSendData(Command, std::forward<handler_type>(handler));

            return result.get();
        }
        template <class	ConnectHandler>
        void internalSendData(const std::string& Command, ConnectHandler&& handler)
        {
            boost::system::error_code ec;

            auto spCommandWithLineEnding = std::make_shared<std::string>(Command);
            *spCommandWithLineEnding += "\r\n";
            Socket_.async_send(boost::asio::buffer(*spCommandWithLineEnding),
                               [this, spCommandWithLineEnding, handler](const boost::system::error_code& ec, std::size_t bytes_transferred) mutable {
                if (ec)
                    handler(ec, std::string());
                else
                {
                    auto buf = std::make_shared<std::vector<char>>(1024);
                    Socket_.async_read_some(boost::asio::buffer(*buf),
                                            [this, buf, handler](const boost::system::error_code& ec, std::size_t bytes_transferred) mutable {
                        if (ec)
                            handler(ec, std::string());
                        else
                        {
                            //spResponse->dataReceived(bytes_transferred);
                            handler(ec, std::string(buf->data(), bytes_transferred));
                            //io_service_.dispatch(Strand_.wrap([this]() {requestCompleted(); }));
                        }
                    });
                }
            });
        }

    private:
        ConnectionManagerType Manager_;
    };
}
