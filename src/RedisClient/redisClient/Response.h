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

        Response(const Response&) = delete;
        Response& operator=(const Response&) = delete;

        Response() :
            Type_(Type::Null),
            pData_(nullptr),
            Length_(0)
        {
        }

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
        const ElementContainer& elements() { return Elements_; }
    private:
        Type Type_;
        const char* pData_;
        size_t Length_;
        ElementContainer Elements_;
    };


    class ResponseHandler
    {
    public:
        typedef std::shared_ptr<ResponseHandler> ResponseHandle;
        typedef std::vector<char> InternalBufferType;

        static constexpr size_t DefaultBuffersize = 1024;

        ResponseHandler(size_t Buffersize = DefaultBuffersize) :
            InitialBuffersize_(Buffersize)
        {
            reset();
        }

        //ResponseHandler(ResponseHandler&& rhs)
        //{
        //    InternalBuffer_     = std::move(rhs.InternalBuffer_);
        //    InitialBuffersize_  = rhs.InitialBuffersize_;
        //    Buffersize_         = rhs.Buffersize_;
        //    BufferContainer_    = std::move(rhs.BufferContainer_);
        //    spTop_              = rhs.spTop_;

        //    Partstack_          = std::move(rhs.Partstack_);
        //    StartPosition_      = rhs.StartPosition_;
        //    ParsePosition_      = rhs.ParsePosition_;
        //    ValidBytesInBuffer_ = rhs.ValidBytesInBuffer_;
        //    CRSeen_             = rhs.CRSeen_;
        //    CRLFSeen_           = rhs.CRLFSeen_;
        //}

        ResponseHandler(const ResponseHandler&) = delete;
        ResponseHandler& operator=(const ResponseHandler&) = delete;

        bool dataReceived(size_t BytesReceived)
        {
            boost::asio::mutable_buffer CurrentBuffer = raw_buffer();
            BytesReceived = std::min(boost::asio::buffer_size(CurrentBuffer), BytesReceived);
            InternalBufferType::const_pointer pStart = boost::asio::buffer_cast<char*>(CurrentBuffer) + ParsePosition_;
            InternalBufferType::const_pointer pEnd = boost::asio::buffer_cast<char*>(CurrentBuffer) + ValidBytesInBuffer_ + BytesReceived;

            size_t BytesToExpect = 2;

            for (; pStart < pEnd; ++pStart, ++ParsePosition_, ++ValidBytesInBuffer_)
            {
                if (CRSeen_ && *pStart == '\n')
                {
                    InternalBufferType::const_pointer pTopEntryStart = raw_buffer_pointer() + StartPosition_;
                    size_t Length = ValidBytesInBuffer_ - 2;
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
                            ValidBytesInBuffer_ += BytesInBuffer;
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

                            ++StartPosition_;

                            CRSeen_ = false;
                            CRLFSeen_ = true;
                            ValidBytesInBuffer_ = -1;

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
                        ValidBytesInBuffer_ = -1;

                        while(!Partstack_.empty())
                        {
                            Partstack_.pop();

                            if (Partstack_.empty())
                            {
                                spTop_ = spPart;
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

            if (!FinishedParsing)
            {
                auto& TopEntry = Partstack_.top();
                InternalBufferType::const_pointer pTopEntryStart = raw_buffer_pointer() + StartPosition_;

                Buffersize_ *= 2;
                size_t MinimumRequiredBuffersize = std::max(BytesToExpect + ValidBytesInBuffer_, Buffersize_);
                BufferContainer_.emplace_back(MinimumRequiredBuffersize);

                memcpy(raw_buffer_pointer(), pTopEntryStart, ValidBytesInBuffer_);
                if (ValidBytesInBuffer_)
                    ParsePosition_ -= StartPosition_;
                else
                    ParsePosition_ = 0;

                StartPosition_ = 0;
            }

            return FinishedParsing;
        }

        boost::asio::mutable_buffer buffer() {
            return raw_buffer() + ValidBytesInBuffer_;
        }

        void reset()
        {
            Buffersize_ = InitialBuffersize_;
            spTop_.reset();
            BufferContainer_.clear();
            if (Buffersize_ != DefaultBuffersize)
                BufferContainer_.emplace_back(Buffersize_);
            while (!Partstack_.empty())
                Partstack_.pop();
            CRSeen_ = false;
            CRLFSeen_ = true;
            ParsePosition_ = 0;
            StartPosition_ = 0;
            ValidBytesInBuffer_ = 0;
        }

        const Response& top() const { return *spTop_; }

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

        boost::array<char,1024> InternalBuffer_;
        size_t InitialBuffersize_;
        size_t Buffersize_;
        std::list<InternalBufferType> BufferContainer_;
        std::shared_ptr<Response> spTop_;

        std::stack<ParseStackEntry> Partstack_;
        InternalBufferType::size_type StartPosition_;
        InternalBufferType::size_type ParsePosition_;
        InternalBufferType::size_type ValidBytesInBuffer_;
        bool CRSeen_;
        bool CRLFSeen_;

        const InternalBufferType::pointer raw_buffer_pointer() {
            if (BufferContainer_.empty())
                return InternalBuffer_.data();
            else
                return BufferContainer_.back().data();
        }

        boost::asio::mutable_buffer raw_buffer() {
            if (BufferContainer_.empty())
                return boost::asio::buffer(InternalBuffer_);
            else
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
    };
}

#endif
