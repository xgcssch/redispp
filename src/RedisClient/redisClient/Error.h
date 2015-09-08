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
        protocol_error,
        no_data,
        user_permission_denied,
        user_out_of_memory = 4711
    };

    class redis_error_category_imp : public boost::system::error_category
    {
    public:
        redis_error_category_imp() : boost::system::error_category() { }
        const char * name() const BOOST_SYSTEM_NOEXCEPT { return "redis"; }

        boost::system::error_condition default_error_condition(int ev) const  BOOST_SYSTEM_NOEXCEPT
        {
            return boost::system::error_condition(ev, redis::redis_error_category);
        }

        std::string message(int ev) const
        {
            switch (ev)
            {
                case ErrorCodes::protocol_error: return "REDIS protocol error";
                default: return "Unknown REDIS error";
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
