#include "stdafx.h"

namespace redis
{
    class ResponsePart
    {
    public:
        virtual std::string dump() const = 0;
    private:

    };

    class ResponsePartSimpleString : public ResponsePart
    {
    public:
        ResponsePartSimpleString(const char* pData, size_t Length) :
            pData_(pData),
            Length_(Length)
        {
        }
        std::string dump() const override
        {
            return std::string(pData_, Length_);
        }
    private:
        const char* pData_;
        size_t Length_;
    };

    class Response
    {
    public:
        typedef std::shared_ptr<Response> ResponseHandle;
        typedef boost::function<void(const boost::system::error_code& ec, const std::string&)> ResponseHandlerType;

        Response(ResponseHandlerType ResponseHandler) :
            ResponseHandler_(ResponseHandler),
            Buffer_(1024)
        {}

        void fail(const boost::system::error_code& ec)
        {
            ResponseHandler_(ec, std::string());
        }

        bool dataReceived(size_t BytesReceived)
        {
            parse(BytesReceived);

            ResponseHandler_(boost::system::errc::make_error_code(boost::system::errc::success), std::string(Buffer_.data(), BytesReceived));
            return false;
        }

        std::vector<char>& buffer() {
            return Buffer_;
        }
        std::list<std::shared_ptr<ResponsePart>> parts() const { return Parts_; }

    private:
        typedef std::vector<char> InternalBufferType;
        InternalBufferType Buffer_;
        std::list<std::shared_ptr<ResponsePart>> Parts_;
        ResponseHandlerType ResponseHandler_;

        struct ParseStackEntry {
            std::shared_ptr<ResponsePart> spPart_;
            InternalBufferType::size_type StartPosition_;
            size_t CurrentEntry_;
            size_t ExpectedEntries_;
            char TypeSeen_;
            ParseStackEntry(InternalBufferType::size_type StartPosition) :
                StartPosition_(StartPosition)
            {}
        };

        struct ParseContext {
            std::stack<ParseStackEntry> Partstack_;
            InternalBufferType::size_type ParsePosition_;
            bool CRSeen_ = false;
            bool CRLFSeen_ = true;
        };

        bool parse(size_t BytesReceived)
        {
            auto spContext = std::make_shared<ParseContext>();

            auto& Context = *spContext;
            InternalBufferType::const_pointer pStart = Buffer_.data() + Context.ParsePosition_;
            InternalBufferType::const_pointer pEnd = pStart + BytesReceived;

            for (; pStart < pEnd; ++pStart, ++Context.ParsePosition_)
            {
                if (Context.CRSeen_ && *pStart == '\n')
                {
                    Context.CRSeen_ = false;
                    Context.CRLFSeen_ = true;

                    auto& TopEntry = Context.Partstack_.top();

                    InternalBufferType::const_pointer pTopEntryStart = Buffer_.data() + TopEntry.StartPosition_;
                    size_t Length = Context.ParsePosition_ - 2;
                    InternalBufferType::const_pointer pTopEntryEnd = pStart + Length;

                    std::shared_ptr<ResponsePart> spPart;

                    switch (*pTopEntryStart)
                    {
                        case '+':
                            spPart = std::make_shared<ResponsePartSimpleString>(pTopEntryStart + 1, Length);

                        default:
                            break;
                    }

                    Parts_.push_back(spPart);
                    Context.Partstack_.pop();
                    continue;
                }

                if (spContext->CRLFSeen_)
                {
                    ParseStackEntry Entry(Context.ParsePosition_);

                    Context.CRLFSeen_ = false;

                    Context.Partstack_.push(std::move(Entry));
                }
                else
                {
                    switch (*pStart)
                    {
                        case '+':
                            break;
                        case '\r':
                            Context.CRSeen_ = true;
                            break;
                    }
                }

            }
            return Context.Partstack_.empty() && Context.CRLFSeen_;
        }

    };

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
                        return handler(ec,std::string());
                    else
                    {
                        Socket_ = std::move(Socket);

                        internalSendData(Command, std::forward<handler_type>(handler) );
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

    class SimpleConnectionManager
    {
    public:

        SimpleConnectionManager(const std::string& Servername = "localhost", int Port = 6379) :
            Servername_(Servername),
            Port_(Port)
        {}

        boost::asio::ip::tcp::socket getConnectedSocket(boost::asio::io_service& io_service, boost::system::error_code& ec)
        {
            boost::asio::ip::tcp::socket Socket(io_service);

            boost::asio::ip::tcp::resolver resolver(io_service);
            boost::asio::ip::tcp::resolver::query query(Servername_, std::to_string(Port_));
            boost::asio::connect(Socket, resolver.resolve(query), ec);

            return Socket;
        }

        template <class	CompletionToken>
        auto async_getConnectedSocket(boost::asio::io_service& io_service, BOOST_ASIO_MOVE_ARG(CompletionToken)	token)
        {
            typedef
                boost::asio::detail::async_result_init<
                CompletionToken,
                void(const boost::system::error_code& ec, boost::asio::ip::tcp::socket& Socket)> t_completion;

            auto spCompletion = std::make_shared<t_completion>(BOOST_ASIO_MOVE_CAST(CompletionToken)(token));

            std::shared_ptr<boost::asio::ip::tcp::resolver> spResolver = std::make_shared<boost::asio::ip::tcp::resolver>(io_service);
            boost::asio::ip::tcp::resolver::query query(Servername_, std::to_string(Port_));
            spResolver->async_resolve(query,
                                      [&io_service, spResolver, spCompletion](boost::system::error_code error, boost::asio::ip::tcp::resolver::iterator iterator) {
                if (error)
                    spCompletion->handler(error, boost::asio::ip::tcp::socket(io_service));
                else
                {
                    std::shared_ptr<boost::asio::ip::tcp::socket> spSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_service);
                    boost::asio::async_connect(*spSocket, iterator,
                                               [spSocket, spCompletion](boost::system::error_code error, boost::asio::ip::tcp::resolver::iterator iterator) {
                        spCompletion->handler(error, *spSocket);
                    }
                    );
                }
            });

            return spCompletion->result.get();
        }
    private:
        std::string Servername_;
        int Port_;
    };
}

int main(int argc, char**argv)
{
    try
    {
#ifdef _DEBUG
        boost::asio::io_service io_service;

        redis::SimpleConnectionManager scm("bingo.de", 26379);

        redis::Connection<redis::SimpleConnectionManager> con(io_service, scm);

        //boost::system::error_code ec;
        //auto Result = con.command("PING", ec);

        //// Callback Version
        //con.async_command("PING", [&con](auto ec, auto Data)
        //{
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //        std::cerr << Data << std::endl;
        //});
        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();

        // Futures Version
        boost::asio::io_service::work work(io_service);
        std::thread thread([&io_service]() { io_service.run(); });

        boost::system::error_code ec;
        auto f = con.async_command("PING", boost::asio::use_future);
        f.wait();
        std::cerr << f.get() << std::endl;

        io_service.stop();
        thread.join();

        //// Coroutine
        //boost::asio::spawn(io_service,
        //                   [&](boost::asio::yield_context yield)
        //{
        //    boost::system::error_code ec;
        //    auto Data = con.async_command("PING", yield[ec]);
        //    if (ec)
        //        std::cerr << ec.message() << std::endl;
        //    else
        //    	std::cerr << Data << std::endl;
        //});

        //std::thread thread([&io_service]() { io_service.run(); });
        //thread.join();

#else
        redis::Response res(
            [](auto ec, auto Data)
        {
            if (ec)
                std::cerr << ec.message() << std::endl;
            else
                std::cerr << Data << std::endl;
        }
        );

        std::string ResponseString("+PONG\r\n");
        res.buffer() = std::vector<char>(ResponseString.data(), ResponseString.data() + ResponseString.size());
        bool MoreDataRequired = res.dataReceived(ResponseString.size());
        std::cerr << res.parts().begin()->get()->dump() << std::endl;
#endif
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    return 0;
    }
