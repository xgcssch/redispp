#ifndef REDIS_ERROR_INCLUDED
#define REDIS_ERROR_INCLUDED

#include <boost\system\error_code.hpp>

namespace redis
{
    const boost::system::error_category & get_redis_error_category() BOOST_SYSTEM_NOEXCEPT;
    const boost::system::error_category & redis_error_category = get_redis_error_category();

    enum class ErrorCodes
    {
        success = 0,
        server_error,
        protocol_error,
        no_data,
        no_usable_server
    };

    class redis_error_category_imp : public boost::system::error_category
    {
    public:
        redis_error_category_imp() : boost::system::error_category() { }
        const char * name() const BOOST_SYSTEM_NOEXCEPT { return "RedisClient"; }

        boost::system::error_condition default_error_condition(int ev) const  BOOST_SYSTEM_NOEXCEPT
        {
            return boost::system::error_condition(ev, redis::redis_error_category);
        }

        std::string message(int ev) const
        {
            switch (static_cast<ErrorCodes>(ev))
            {
                case ErrorCodes::server_error: return "Server signaled error";
                case ErrorCodes::protocol_error: return "Protocol error";
                case ErrorCodes::no_data: return "No data from server";
                case ErrorCodes::no_usable_server: return "No usable server found";
                default: return "Unknown error";
            }
        }
    };

    const boost::system::error_category & get_redis_error_category() BOOST_SYSTEM_NOEXCEPT
    {
        static const redis_error_category_imp redisCategory;
        return redisCategory;
    }

    inline boost::system::error_code make_error_code(ErrorCodes e)
    {
        return boost::system::error_code(static_cast<int>(e), redis_error_category);
    }
}

#endif
