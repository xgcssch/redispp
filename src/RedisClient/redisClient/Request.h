#ifndef REDIS_REQUEST_INCLUDED
#define REDIS_REQUEST_INCLUDED

namespace redis
{
    class Request
    {
    public:
        typedef std::vector<boost::asio::const_buffer> BufferSequence_t;

        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;

        template<typename... Ts>
        Request(Ts... args)
        {
            Strings_.emplace_back(pCRLF_);
            const int size = sizeof...(args);
            std::string res[size] = { args... };
            BufferSequence_t Arguments(size);
            size_t Index = 0;
            for (auto& s : res)
            {
                Strings_.push_back(std::move(s));
                Arguments[Index++] = boost::asio::buffer(Strings_.back());
            }
            construct(std::move(Arguments));
        }
        Request(BufferSequence_t&& Arguments)
        {
            Strings_.emplace_back(pCRLF_);
            construct(std::move(Arguments));
        }

        const BufferSequence_t& bufferSequence()
        {
            Arraycount_ = std::string("*") + std::to_string((Components_.size() - 1) / 3) + Strings_.front();
            Components_[0] = boost::asio::buffer(Arraycount_);
            return Components_;
        }

    private:
        BufferSequence_t Components_;
        std::string Arraycount_;
        std::list<std::string> Strings_;

        static constexpr const char* pCRLF_ = "\r\n";

        void construct(BufferSequence_t&& Arguments)
        {
            Components_ = std::move(Arguments);

            size_t Argumentcount = Components_.size();
            Components_.reserve(Argumentcount * 5 + 1);
            Components_.resize(Argumentcount * 3 + 1);
            for (size_t Index = Argumentcount; Index > 0;--Index)
            {
                size_t TargetIndex = Index * 3;
                std::swap(Components_[Index - 1], Components_[TargetIndex - 1]);
                Strings_.emplace_back("$" + std::to_string(boost::asio::buffer_size(Components_[TargetIndex - 1])) + Strings_.front());
                Components_[TargetIndex - 2] = boost::asio::buffer(Strings_.back());
                Components_[TargetIndex] = boost::asio::buffer(Strings_.front());
            }
        }

    };
}

#endif
