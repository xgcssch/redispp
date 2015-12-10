#ifndef REDIS_RESPONSE_INCLUDED
#define REDIS_RESPONSE_INCLUDED

#include <vector>
#include <stack>
#include <boost\array.hpp>
#include <boost\asio.hpp>

namespace redis
{
    class Response
    {
    public:
        typedef std::vector<std::shared_ptr<Response>> ElementContainer;
        enum class Type { SimpleString, Error, Integer, BulkString, Null, Array };

        Response( const Response& ) = delete;
        Response( Response&& ) = default;
        Response& operator=(const Response&) = delete;

        Response() :
            Type_(Type::Null),
            pData_(nullptr),
            Length_(0)
        {
        }

        //Response( Response&& rhs )
        //{
        //    Type_ = rhs.Type_;
        //    rhs.Type_ = Type::Null;
        //    pData_ = rhs.pData_;
        //    rhs.pData_ = nullptr;
        //    Length_ = rhs.Length_;
        //    rhs.Length_ = 0;
        //    Elements_ = std::move( rhs.Elements_ );
        //}

        Response(Type PartType, const char* pData, size_t Length) :
            Type_(PartType),
            pData_(pData),
            Length_(Length)
        {
        }

        Response(ElementContainer&& Elements) :
            Type_(Type::Array),
            pData_(nullptr),
            Length_(0)
        {
            Elements_ = std::move(Elements);
        }

        std::string dump() const
        {
            switch (Type_)
            {
                case Type::SimpleString:
                    return "Simple:\"" + string() + "\"";

                case Type::Error:
                    return "Error:\"" + string() + "\"";

                case Type::Integer:
                    return "Integer:\"" + string() + "\"";

                case Type::BulkString:
                    return "Bulkstring:\"" + string() + "\"";

                case Type::Null:
                    return "Null";

                case Type::Array:
                {
                    std::string Result;
                    Result += "[" + std::to_string(Elements_.size()) + ": ";
                    for (const auto& Element : Elements_)
                        Result += Element->dump() + ",";
                    Result += "]";
                    return Result;
                }
                default:
                    return "Unknown";
            }
        }

        Type type() const { return Type_; }
        const char* data() const { return pData_; }
        size_t size() const { return Length_; }
        std::string string() const { return Length_?std::string(pData_, Length_):std::string(); }
        int64_t asint() const { return std::stoll( string() ); }
        const ElementContainer& elements() const { return Elements_; }
        const ElementContainer::value_type::element_type& operator[]( size_t Index ) const { return *Elements_.operator[](Index); }
    private:
        Type Type_;
        const char* pData_;
        size_t Length_;
        ElementContainer Elements_;
    };

    std::ostream& operator<<(std::ostream &os, const Response::Type& r)
    {
        switch (r) 
        {
            case Response::Type::SimpleString:
                return os << "SimpleString";
            case Response::Type::Error:
                return os << "Error";
            case Response::Type::Integer:
                return os << "Integer";
            case Response::Type::BulkString:
                return os << "BulkString";
            case Response::Type::Null:
                return os << "Null";
            case Response::Type::Array:
                return os << "Array";
            default:
                return os;
        };
    }

    std::ostream& operator<<(std::ostream &os, const Response& r)
    {
        return os << r.dump();
    }

    class ResponseHandler
    {
    public:
        typedef std::shared_ptr<ResponseHandler> ResponseHandle;
        typedef std::vector<char> InternalBufferType;

        static constexpr size_t DefaultBuffersize = 1024;

        ResponseHandler(size_t Buffersize = DefaultBuffersize) :
            InitialBuffersize_(Buffersize)
        {
            BufferContainer_.emplace_back(Buffersize);

            reset();
        }

        ResponseHandler(const ResponseHandler&) = delete;
        ResponseHandler& operator=(const ResponseHandler&) = delete;

        bool dataReceived(size_t BytesReceived)
        {
            boost::asio::mutable_buffer CurrentBuffer = raw_buffer();

            size_t BuffersizeRemaining = boost::asio::buffer_size(CurrentBuffer) - UnparsedBytesInBuffer_;
            if (BytesReceived > BuffersizeRemaining)
                BytesReceived = BuffersizeRemaining;

            InternalBufferType::const_pointer pStart = boost::asio::buffer_cast<char*>(CurrentBuffer) + Offset_ + ParsePosition_;
            InternalBufferType::const_pointer pEnd = boost::asio::buffer_cast<char*>(CurrentBuffer) + Offset_ + ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ + BytesReceived;

            size_t BytesToExpect = 2;

            bool ToplevelFinished = false;
            for (; pStart < pEnd && !ToplevelFinished; ++pStart, ++ParsePosition_, ++ParsedBytesInBuffer_)
            {
                if (CRSeen_ && *pStart == '\n')
                {
                    InternalBufferType::const_pointer pTopEntryStart = raw_buffer_pointer() + Offset_ + StartPosition_;
                    size_t Length = ParsedBytesInBuffer_ - 2;
                    InternalBufferType::const_pointer pTopEntryEnd = pStart + Length;

                    std::shared_ptr<Response> spPart;

                    switch (*pTopEntryStart)
                    {
                        case '+':
                            spPart = std::make_shared<Response>(Response::Type::SimpleString, pTopEntryStart + 1, Length);
                            break;

                        case '-':
                            spPart = std::make_shared<Response>(Response::Type::Error, pTopEntryStart + 1, Length);
                            break;

                        case ':':
                            spPart = std::make_shared<Response>(Response::Type::Integer, pTopEntryStart + 1, Length);
                            break;

                        case '$':
                        {
                            off_t ExpectedBytes = local_atoi(pTopEntryStart + 1);
                            if (ExpectedBytes == -1)
                            {
                                spPart = std::make_shared<Response>();
                                break;
                            }
                            ExpectedBytes += 2;
                            off_t BytesInBuffer = (pEnd - pStart) - 1;
                            // Einfachster Fall: Bulkstring ist vollständig im Puffer
                            if (BytesInBuffer >= ExpectedBytes)
                            {
                                // check \r\n
                                spPart = std::make_shared<Response>(Response::Type::BulkString, pStart + 1, ExpectedBytes - 2);
                                pStart += ExpectedBytes;
                                ParsePosition_ += ExpectedBytes;
                                break;
                            }

                            BytesToExpect = ExpectedBytes - BytesInBuffer;
                            pStart += BytesInBuffer;
                            ParsedBytesInBuffer_ += BytesInBuffer;
                            ParsePosition_--;
                            break;
                        }

                        case '*':
                        {
                            off_t Items = local_atoi(pTopEntryStart + 1);
                            if (Items == -1)
                            {
                                spPart = std::make_shared<Response>();
                                break;
                            }
                            if (Items == 0)
                            {
                                spPart = std::make_shared<Response>(std::vector<std::shared_ptr<Response>>{});
                                break;
                            }

                            CRSeen_ = false;
                            CRLFSeen_ = true;
                            ParsedBytesInBuffer_ = -1;

                            Partstack_.emplace(Items);

                            break;
                        }

                        default:
                            // throw invalid type
                            break;
                    }

                    if (spPart)
                    {
                        CRSeen_ = false;
                        CRLFSeen_ = true;
                        ParsedBytesInBuffer_ = -1;

                        while(!Partstack_.empty())
                        {
                            Partstack_.pop();

                            if (Partstack_.empty())
                            {
                                spTop_ = spPart;
                                ToplevelFinished = true;
                                break;
                            }

                            auto& TopEntry = Partstack_.top();
                            if (TopEntry.spParts_)
                            {
                                TopEntry.spParts_->at(TopEntry.CurrentEntry_++) = spPart;
                                if (TopEntry.CurrentEntry_ < TopEntry.spParts_->size())
                                    break;
                                else
                                {
                                    spPart = std::make_shared<Response>(std::move(*TopEntry.spParts_) );
                                }
                            }
                        }
                    }
                    continue;
                }

                if (CRLFSeen_)
                {
                    StartPosition_ = ParsePosition_;

                    CRLFSeen_ = false;

                    Partstack_.emplace();
                }
                else
                    if ('\r'== *pStart)
                        CRSeen_ = true;
            }

            bool FinishedParsing = Partstack_.empty() && CRLFSeen_;

            UnparsedBytesInBuffer_ = pEnd - pStart;

            if (!FinishedParsing)
            {
                auto& TopEntry = Partstack_.top();
                InternalBufferType::const_pointer pTopEntryStart = raw_buffer_pointer() + Offset_ + StartPosition_;

                Buffersize_ *= 2;
                size_t MinimumRequiredBuffersize = std::max(BytesToExpect + ParsedBytesInBuffer_ + UnparsedBytesInBuffer_, Buffersize_);
                BufferContainer_.emplace_back(MinimumRequiredBuffersize);

                memcpy(raw_buffer_pointer(), pTopEntryStart, ParsedBytesInBuffer_ + UnparsedBytesInBuffer_);
                if (ParsedBytesInBuffer_)
                    ParsePosition_ -= StartPosition_;
                else
                    ParsePosition_ = 0;

                Offset_ = 0;
                StartPosition_ = 0;
            }

            return FinishedParsing;
        }

        boost::asio::mutable_buffer buffer() {
            return raw_buffer() + ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ + Offset_;
        }

        bool commit()
        {
            // Simple case: No valid data in buffer
            if (!UnparsedBytesInBuffer_)
            {
                reset();
                return false;
            }
            // Still Data available...

            // Free surplus Buffers
            resetBuffers();

            spTop_.reset();
            Offset_ += ParsePosition_;
            ParsePosition_ = 0;

            return dataReceived(0);
        }

        void reset()
        {
            // resets Buffersize_, so call before reinit
            internalReset();
            resetBuffers();
        }

        const Response& top() const { return *spTop_; }
        Response& top() { return *spTop_; }

    private:
        struct ParseStackEntry {
            std::shared_ptr<std::vector<std::shared_ptr<Response>>> spParts_;
            size_t CurrentEntry_ = 0;
            ParseStackEntry()
            {}
            ParseStackEntry(size_t Items) :
                spParts_(std::make_shared<std::vector<std::shared_ptr<Response>>>(Items))
            {
            }
        };

        size_t InitialBuffersize_;
        size_t Buffersize_;
        std::list<InternalBufferType> BufferContainer_;
        std::shared_ptr<Response> spTop_;

        std::stack<ParseStackEntry> Partstack_;
        InternalBufferType::size_type StartPosition_;
        InternalBufferType::size_type ParsePosition_;
        InternalBufferType::size_type ParsedBytesInBuffer_;
        InternalBufferType::size_type UnparsedBytesInBuffer_;
        InternalBufferType::size_type Offset_;
        bool CRSeen_;
        bool CRLFSeen_;

        const InternalBufferType::pointer raw_buffer_pointer() {
            return BufferContainer_.back().data();
        }

        boost::asio::mutable_buffer raw_buffer() {
            return boost::asio::buffer(BufferContainer_.back());
        }

        static off_t local_atoi(const char *p) {
            off_t  x = 0;
            bool neg = false;
            if (*p == '-') {
                neg = true;
                ++p;
            }
            while (*p >= '0' && *p <= '9') {
                x = (x * 10) + (*p - '0');
                ++p;
            }
            if (neg) {
                x = -x;
            }
            return x;
        }

        void resetBuffers()
        {
            // Free surplus Buffers
            if (BufferContainer_.size() > 1)
            {
                std::iter_swap(BufferContainer_.begin(), --BufferContainer_.end());
                BufferContainer_.resize(1);
            }
        }

        void internalReset()
        {
            Buffersize_ = InitialBuffersize_;
            spTop_.reset();
            while (!Partstack_.empty())
                Partstack_.pop();
            CRSeen_ = false;
            CRLFSeen_ = true;
            ParsePosition_ = 0;
            StartPosition_ = 0;
            ParsedBytesInBuffer_ = 0;
            UnparsedBytesInBuffer_ = 0;
            Offset_ = 0;
        }
    };
}

#endif
