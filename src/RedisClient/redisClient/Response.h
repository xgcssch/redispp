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

        Response(size_t Buffersize = 1024) :
            Buffersize_(Buffersize)
        {
            BufferContainer_.emplace_back(Buffersize_);
            reset();
        }

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
                    auto& TopEntry = Partstack_.top();

                    InternalBufferType::const_pointer pTopEntryStart = BufferContainer_.back().data() + TopEntry.StartPosition_;
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
                            off_t ExpectedBytes = std::stoi(std::string(pTopEntryStart + 1, Length));
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
                            off_t Items = std::stoi(std::string(pTopEntryStart + 1, Length));
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

                            ParseStackEntry Entry(ParsePosition_+1);

                            CRSeen_ = false;
                            CRLFSeen_ = true;
                            ValidBytesInBuffer_ = -1;

                            Entry.spParts_ = std::make_shared<std::vector<std::shared_ptr<ResponsePart>>>(Items);

                            Partstack_.push(std::move(Entry));

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
                    ParseStackEntry Entry(ParsePosition_);

                    CRLFSeen_ = false;

                    Partstack_.push(std::move(Entry));
                }
                else
                {
                    switch (*pStart)
                    {
                        case '+':
                            break;
                        case '\r':
                            CRSeen_ = true;
                            break;
                    }
                }

            }
            bool FinishedParsing = Partstack_.empty() && CRLFSeen_;

            if (!FinishedParsing)
            {
                auto& TopEntry = Partstack_.top();
                InternalBufferType::const_pointer pTopEntryStart = BufferContainer_.back().data() + TopEntry.StartPosition_;

                Buffersize_ *= 2;
                size_t MinimumRequiredBuffersize = std::max(BytesToExpect + ValidBytesInBuffer_, Buffersize_);
                BufferContainer_.emplace_back(MinimumRequiredBuffersize);

                memcpy(BufferContainer_.back().data(), pTopEntryStart, ValidBytesInBuffer_);
                if (ValidBytesInBuffer_)
                    ParsePosition_ -= TopEntry.StartPosition_;
                else
                    ParsePosition_ = 0;

                TopEntry.StartPosition_ = 0;
            }

            return FinishedParsing;
        }

        boost::asio::mutable_buffer raw_buffer() {
            return boost::asio::buffer(BufferContainer_.back());
        }

        boost::asio::mutable_buffer buffer() {
            return raw_buffer() + ValidBytesInBuffer_;
        }

        void reset()
        {
            spTop_.reset();
            while (!Partstack_.empty())
                Partstack_.pop();
            CRSeen_ = false;
            CRLFSeen_ = true;
            ParsePosition_ = 0;
            ValidBytesInBuffer_ = 0;
        }

        const ResponsePart& top() const { return *spTop_; }

    private:
        size_t Buffersize_;
        std::list<InternalBufferType> BufferContainer_;
        std::shared_ptr<ResponsePart> spTop_;

        struct ParseStackEntry {
            std::shared_ptr<std::vector<std::shared_ptr<ResponsePart>>> spParts_;
            size_t CurrentEntry_ = 0;
            ParseStackEntry(InternalBufferType::size_type StartPosition) :
                StartPosition_(StartPosition)
            {}
        };

        std::stack<ParseStackEntry> Partstack_;
        InternalBufferType::size_type ParsePosition_;
        InternalBufferType::size_type ValidBytesInBuffer_;
        bool CRSeen_;
        bool CRLFSeen_;
    };
}
