#ifndef REDIS_SIMPLECONNECTIONMANAGER_INCLUDED
#define REDIS_SIMPLECONNECTIONMANAGER_INCLUDED

namespace redis
{
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

#endif
