#include <boost\array.hpp>

namespace redis
{
    class ResponsePart
    {
    public:
        ResponsePart(const char* pData, size_t Length) :
            pData_(pData),
            Length_(Length)
        {
        }

        virtual std::string dump() const
        {
            return std::string(pData_, Length_);
        }
    private:
        const char* pData_;
        size_t Length_;
    };

    class ResponsePartSimpleString : public ResponsePart
    {
    public:
        ResponsePartSimpleString(const char* pData, size_t Length) :
            ResponsePart(pData, Length)
        {}
        std::string dump() const override
        {
            return "Simple:\"" + ResponsePart::dump() + "\"";
        }
    };

    class ResponsePartError : public ResponsePart
    {
    public:
        ResponsePartError(const char* pData, size_t Length) :
            ResponsePart(pData, Length)
        {}
        std::string dump() const override
        {
            return "Error:\"" + ResponsePart::dump() + "\"";
        }
    };

    class ResponsePartInteger : public ResponsePart
    {
    public:
        ResponsePartInteger(const char* pData, size_t Length) :
            ResponsePart(pData, Length)
        {}
        std::string dump() const override
        {
            return "Integer:\"" + ResponsePart::dump() + "\"";
        }
    };

    class ResponsePartBulkString : public ResponsePart
    {
    public:
        ResponsePartBulkString(const char* pData, size_t Length) :
            ResponsePart(pData, Length)
        {}
        std::string dump() const override
        {
            return "Bulk:\"" + ResponsePart::dump() + "\"";
        }
    };

    class ResponseNull : public ResponsePart
    {
    public:
        ResponseNull() :
            ResponsePart(nullptr, 0)
        {}
        std::string dump() const override
        {
            return "Null";
        }
    };

    class ResponseArray : public ResponsePart
    {
    public:
        ResponseArray(std::vector<std::shared_ptr<ResponsePart>>&& Elements) :
            ResponsePart(nullptr, 0)
        {
            Elements_ = std::move(Elements);
        }
        std::string dump() const override
        {
            std::string Result;
            Result += "[" + std::to_string(Elements_.size()) + ": ";
            for( const auto& Element : Elements_ )
                Result += Element->dump() + ",";
            Result += "]";
            return Result;
        }

        const std::vector<std::shared_ptr<ResponsePart>>& elements() const { return Elements_; }

    private:
        std::vector<std::shared_ptr<ResponsePart>> Elements_;
    };

    class Response
    {
    public:
        typedef std::shared_ptr<Response> ResponseHandle;
        typedef std::vector<char> InternalBufferType;

        static constexpr size_t DefaultBuffersize = 1024;

        Response(size_t Buffersize = DefaultBuffersize) :
            InitialBuffersize_(Buffersize)
        {
            reset();
        }

        Response(const Response&) = delete;
        Response& operator=(const Response&) = delete;

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

                    std::shared_ptr<ResponsePart> spPart;

                    switch (*pTopEntryStart)
                    {
                        case '+':
                            spPart = std::make_shared<ResponsePartSimpleString>(pTopEntryStart + 1, Length);
                            break;

                        case '-':
                            spPart = std::make_shared<ResponsePartError>(pTopEntryStart + 1, Length);
                            break;

                        case ':':
                            spPart = std::make_shared<ResponsePartInteger>(pTopEntryStart + 1, Length);
                            break;

                        case '$':
                        {
                            off_t ExpectedBytes = local_atoi(pTopEntryStart + 1);
                            if (ExpectedBytes == -1)
                            {
                                spPart = std::make_shared<ResponseNull>();
                                break;
                            }
                            ExpectedBytes += 2;
                            off_t BytesInBuffer = (pEnd - pStart) - 1;
                            // Einfachster Fall: Bulkstring ist vollständig im Puffer
                            if (BytesInBuffer >= ExpectedBytes)
                            {
                                // check \r\n
                                spPart = std::make_shared<ResponsePartBulkString>(pStart + 1, ExpectedBytes - 2);
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
                                spPart = std::make_shared<ResponseNull>();
                                break;
                            }
                            if (Items == 0)
                            {
                                spPart = std::make_shared<ResponseArray>(std::vector<std::shared_ptr<ResponsePart>>{});
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
                                    spPart = std::make_shared<ResponseArray>(std::move(*TopEntry.spParts_) );
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

        const ResponsePart& top() const { return *spTop_; }

    private:
        struct ParseStackEntry {
            std::shared_ptr<std::vector<std::shared_ptr<ResponsePart>>> spParts_;
            size_t CurrentEntry_ = 0;
            ParseStackEntry()
            {}
            ParseStackEntry(size_t Items) :
                spParts_(std::make_shared<std::vector<std::shared_ptr<ResponsePart>>>(Items))
            {
            }
        };

        boost::array<char,1024> InternalBuffer_;
        size_t InitialBuffersize_;
        size_t Buffersize_;
        std::list<InternalBufferType> BufferContainer_;
        std::shared_ptr<ResponsePart> spTop_;

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
