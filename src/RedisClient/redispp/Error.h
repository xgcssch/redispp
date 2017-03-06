#ifndef REDIS_ERROR_INCLUDED
#define REDIS_ERROR_INCLUDED

#include <system_error>

namespace redis
{
    using base_error_category = boost::system::error_category;
    using base_error_condition = boost::system::error_condition;
    using base_error_code = boost::system::error_code;

    //const base_error_category & get_redis_error_category() noexcept;
    //const base_error_category & redis_error_category = get_redis_error_category();
    inline const base_error_category& redis_error_category() _NOEXCEPT;

    enum class ErrorCodes
    {
        success = 0,
        server_error,
        protocol_error,
        no_data,
        no_usable_server,
        incomplete_response,
        no_more_sentinels
    };

    class redis_error_category_imp : public base_error_category
    {
    public:
        constexpr redis_error_category_imp() : base_error_category() { }
        const char * name() const noexcept override { return "RedisClient"; }

        base_error_condition default_error_condition(int ev) const noexcept 
        {
            return base_error_condition(ev, redis::redis_error_category());
        }

        std::string message(int ev) const
        {
            switch (static_cast<ErrorCodes>(ev))
            {
                case ErrorCodes::server_error: return "Server signaled error";
                case ErrorCodes::protocol_error: return "Protocol error";
                case ErrorCodes::no_data: return "No data from server";
                case ErrorCodes::no_usable_server: return "No usable server found";
                case ErrorCodes::incomplete_response: return "Not enough data for expected responses";
                case ErrorCodes::no_more_sentinels: return "No more sentinels left to ask for master";
                default: return "Unknown error";
            }
        }
    };

    //inline const base_error_category& get_redis_error_category() noexcept
    //{
    //    static const redis_error_category_imp redisCategory;
    //    return redisCategory;
    //}

    inline base_error_code make_error_code(ErrorCodes e)
    {
        return base_error_code(static_cast<int>(e), redis_error_category());
    }

    template<class _Ty>
    struct _Immortalizer
    {	// constructs _Ty, never destroys
        _Immortalizer()
        {	// construct _Ty inside _Storage
            ::new (static_cast<void *>(&_Storage)) _Ty();
        }

        ~_Immortalizer() _NOEXCEPT
        {	// intentionally do nothing
        }

        _Immortalizer(const _Immortalizer&) = delete;
        _Immortalizer& operator=(const _Immortalizer&) = delete;

        typename std::aligned_union<1, _Ty>::type _Storage;
    };

    template<class _Ty> inline
        _Ty& _Immortalize()
    {	// return a reference to an object that will live forever
        /* MAGIC */ static _Immortalizer<_Ty> _Static;
        return (*reinterpret_cast<_Ty *>(&_Static._Storage));
    }

    inline const base_error_category& redis_error_category() _NOEXCEPT
    {	// get generic_category
        return (_Immortalize<redis_error_category_imp>());
    }
}

#endif
