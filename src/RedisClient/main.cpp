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
                        spPart = std::make_shared<ResponsePartSimpleString>(pTopEntryStart+1,Length);

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
        void async_command(const std::string& Command, Response::ResponseHandlerType ResponseHandler)
		{
            auto spCurrentResponse = std::make_shared<Response>(ResponseHandler);
            io_service_.dispatch(Strand_.wrap([this, spCurrentResponse]() {requestCreated(spCurrentResponse); }));

            if (!Socket_.is_open())
            {
                Manager_.async_getConnectedSocket(io_service_,
                    [this, Command, spCurrentResponse](const boost::system::error_code& ec, boost::asio::ip::tcp::socket& Socket) {
                    if (ec)
                        spCurrentResponse->fail(ec);
                    else
                    {
                        Socket_ = std::move(Socket);

                        internalSendData(Command, spCurrentResponse);
                    }
                });
            }
            else
                internalSendData(Command, spCurrentResponse);
		}
        void internalSendData(const std::string& Command, Response::ResponseHandle spResponse)
        {
            boost::system::error_code ec;

            auto spCommandWithLineEnding = std::make_shared<std::string>(Command);
            *spCommandWithLineEnding += "\r\n";
            Socket_.async_send(boost::asio::buffer(*spCommandWithLineEnding),
                [this, spCommandWithLineEnding, spResponse](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (ec)
                    spResponse->fail(ec);
                else
                {
                    Socket_.async_read_some(boost::asio::buffer(spResponse->buffer()),
                        [this, spResponse](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                        if (ec)
                            spResponse->fail(ec);
                        else
                        {
                            spResponse->dataReceived(bytes_transferred);
                            io_service_.dispatch(Strand_.wrap([this]() {requestCompleted(); }));
                        }
                    });
                }
            } );
        }

    private:
        ConnectionManagerType Manager_;
	};
	
	class SimpleConnectionManager 
	{
	public:

		SimpleConnectionManager( const std::string& Servername="localhost", int Port=6379) :
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
		void async_getConnectedSocket(boost::asio::io_service& io_service, boost::function<void(const boost::system::error_code& ec, boost::asio::ip::tcp::socket& Socket)> SocketConnectedHandler)
		{
			std::shared_ptr<boost::asio::ip::tcp::resolver> spResolver = std::make_shared<boost::asio::ip::tcp::resolver>(io_service);
			boost::asio::ip::tcp::resolver::query query(Servername_, std::to_string(Port_));
			spResolver->async_resolve(query, 
                [&io_service, spResolver, SocketConnectedHandler] (const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator) {
                if (error)
                    SocketConnectedHandler(error, boost::asio::ip::tcp::socket(io_service));
                else
                {
                    std::shared_ptr<boost::asio::ip::tcp::socket> spSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_service);
                    boost::asio::async_connect(*spSocket, iterator,
                        [spSocket, SocketConnectedHandler](const boost::system::error_code& error, boost::asio::ip::tcp::resolver::iterator iterator) {
                        SocketConnectedHandler(error, *spSocket);
                    }
                    );
                }
            });
		}
	private:
		std::string Servername_;
		int Port_;
	};
}

int main( int argc, char**argv)
{
	try
	{
#ifdef fff
		boost::asio::io_service io_service;

		redis::SimpleConnectionManager scm;

		redis::Connection<redis::SimpleConnectionManager> con(io_service, scm);

		//boost::system::error_code ec;
		//auto Result = con.command("PING", ec);

		con.async_command("PING", [&con](auto ec, auto Data)
		{
            if (ec)
                std::cerr << ec.message() << std::endl;
            else
                std::cerr << Data << std::endl;
		});

		io_service.run();
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
