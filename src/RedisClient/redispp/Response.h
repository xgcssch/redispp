#ifndef REDISPP_RESPONSE_INCLUDED
#define REDISPP_RESPONSE_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include <vector>
#include <list>
#include <stack>
#include <memory>

#include <boost/asio/buffer.hpp>

#include <iostream>

#ifndef REDISPP_REDISPP_INCLUDED
#include "redispp.h"
#endif

namespace redis
{
    // Entity representing a part or all of the Response from a Redis server
    class Response
    {
    public:
        // Entity representing the number of nested Response objects
        using ElementContainer = std::vector<std::shared_ptr<Response>>;

        // Enumeration representing the native Redis types
        enum class Type {
            SimpleString, Error, Integer, BulkString, Null, Array
        };

        Response( const Response& ) = delete;
        Response( Response&& ) = default;
        Response& operator=( const Response& ) = delete;

        Response() :
            Type_( Type::Null ),
            pData_( nullptr ),
            Length_( 0 )
        {}

        Response( Type PartType, const char* pData, size_t Length ) :
            Type_( PartType ),
            pData_( pData ),
            Length_( Length )
        {}

        Response( ElementContainer&& Elements ) :
            Type_( Type::Array ),
            pData_( nullptr ),
            Length_( 0 ),
            Elements_( std::move( Elements ) )
        {}

        // helper function to generate a textual representation of the response
        std::string dump() const
        {
            switch( Type_ )
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
                    Result += "[" + std::to_string( Elements_.size() ) + ": ";
                    for( const auto& Element : Elements_ )
                        Result += Element->dump() + ",";
                    Result += "]";
                    return Result;
                }
                default:
                    return "Unknown";
            }
        }

        // returns the type of this object
        Type type() const { return Type_; }
        // returns a pointer to the start of the data
        const char* data() const { return pData_; }
        // returns the size of the data
        size_t size() const { return Length_; }
        // returns the data as a STL string
        std::string string() const { return Length_ ? std::string( pData_, Length_ ) : std::string(); }
        // returns the data as an signed 64 bit integer - no validation is made if the response really holds an integer
        int64_t asint() const { return std::stoll( string() ); }
        // returns the container of nested responses
        const ElementContainer& elements() const { return Elements_; }
        const ElementContainer::value_type::element_type& operator[]( size_t Index ) const 
        {
            if ( Elements_.size() < Index )
                throw std::runtime_error( "index out of bound for nested response" );
            return *Elements_.operator[]( Index );
        }
    private:
        Type Type_;
        const char* pData_;
        size_t Length_;
        ElementContainer Elements_;
    };

    // used to stream the textual type of this response
    template<class T_>
    inline T_& operator<<( T_ &os, const Response::Type& r )
    {
        switch( r )
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

    // used to stream the textual content of this response
    template<class T_>
    inline T_& operator<<( T_ &os, const Response& r )
    {
        return os << r.dump();
    }

    // This class handles responses from the Redis Server
    // It exposes a buffer where to put data in, functions to process a chunk of data and accessors to the result objects
    template<class NotificationSinkType_=NullNotificationSink>
    class ResponseHandler
    {
    public:
        // Type to
        using ResponseHandle = std::shared_ptr<ResponseHandler>;
        // Type of the internaly used buffer
        using InternalBufferType = std::vector<char>;
        // Containertype to manage all the internaly used buffers
        using BufferContainerType = std::list<InternalBufferType>;

        // Default initial buffersize
        static constexpr size_t DefaultBuffersize = 1024;

        // Constructs an ResponseHandler object
        ResponseHandler(
            // Initial buffersize to use
            size_t Buffersize = DefaultBuffersize,
            NotificationSinkType_ NotificationSink = NotificationSinkType_{}
        ) :
            InitialBuffersize_( Buffersize ),
            NotificationSink_(NotificationSink),
            spBufferContainer_( std::make_shared<BufferContainerType>() )
        {
            spBufferContainer_->emplace_back( Buffersize );

            reset();
        }

        ResponseHandler( const ResponseHandler& ) = delete;
        ResponseHandler& operator=( const ResponseHandler& ) = delete;

        // This function is called whenever data has been received
        // returns true if a parse at the topmost level has finished
        bool dataReceived(
            // Number of bytes received in this chunk
            size_t BytesReceived
        )
        {
            NotificationSink_.debug( "ResponseHandler::dataReceived(): BytesReceived: {} - ParsePosition:{} ParsedBytesInBuffer:{} UnparsedBytesInBuffer:{} Offset:{} StartPosition:{} Buffersize:{}", BytesReceived, ParsePosition_, ParsedBytesInBuffer_, UnparsedBytesInBuffer_, Offset_, StartPosition_, boost::asio::buffer_size( raw_buffer() ) );

            // Currently active boost::asio::mutable_buffer
            boost::asio::mutable_buffer CurrentBuffer = raw_buffer();

            // Number of bytes remaining in buffer before processing the current chunk
            size_t BuffersizeRemaining = boost::asio::buffer_size( CurrentBuffer ) - Offset_ -  ParsePosition_;
            if( BytesReceived > BuffersizeRemaining )
                BytesReceived = BuffersizeRemaining;

            BuffersizeRemaining -= BytesReceived;

            // Pointer to the current element in the buffer
            InternalBufferType::const_pointer pCurrent = boost::asio::buffer_cast<char*>(CurrentBuffer) + Offset_ + ParsePosition_;
            // Pointer one after the last valid element in the buffer
            InternalBufferType::const_pointer pEnd = boost::asio::buffer_cast<char*>(CurrentBuffer) + ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ + Offset_ + StartPosition_ + BytesReceived;

            // Minimum number of bytes expected when there was not enough data to finish the parsing.
            // At least two bytes - CRLF - are expected
            size_t BytesToExpect = 0;

            // Adjust ParsedBytesInBuffer_
            ParsedBytesInBuffer_ -= ParsedBytesInBufferAdjustment_;

            // Indicator for the completion of the parse at the topmost level
            bool ToplevelFinished = false;
            for( ; pCurrent < pEnd && !ToplevelFinished; ++pCurrent, ++ParsePosition_, ++ParsedBytesInBuffer_ )
            {
                //NotificationSink_.debug( "pCurrent {} pEnd:{} ToplevelFinished:{} ParsePosition:{} ParsedBytesInBuffer:{}", reinterpret_cast<size_t>(pCurrent), reinterpret_cast<size_t>(pEnd), ToplevelFinished, ParsePosition_, ParsedBytesInBuffer_ );

                // Is this a CR LF combination?
                if( CRSeen_ && *pCurrent == '\n' )
                {
                    // Pointer to the start of the current Entry
                    InternalBufferType::const_pointer pTopEntryStart = raw_buffer_pointer() + Offset_ + StartPosition_;
                    // Length of parsed Entry - ParsedBytesInBuffer_ with compensation for CRLF
                    size_t Length = ParsedBytesInBuffer_ - 2;

                    // The shared pointer for the part
                    std::shared_ptr<Response> spPart;

                    // look at the first byte of the entry to extract the type 
                    switch( *pTopEntryStart )
                    {
                        case '+':
                            // + denotes a simple string - it stretches from the first byte following the typeindicator to the CRLF
                            spPart = std::make_shared<Response>( Response::Type::SimpleString, pTopEntryStart + 1, Length );
                            NotificationSink_.debug( "ResponseHandler::dataReceived(): simple string parsed '{}'", std::string(pTopEntryStart + 1, Length) );
                            break;

                        case '-':
                            // - denotes an error - the attached message stretches from the first byte following the typeindicator to the CRLF
                            spPart = std::make_shared<Response>( Response::Type::Error, pTopEntryStart + 1, Length );
                            NotificationSink_.debug( "ResponseHandler::dataReceived(): error parsed '{}'", std::string(pTopEntryStart + 1, Length) );
                            break;

                        case ':':
                            // : denotes an integer - the value stretches from the first byte following the typeindicator to the CRLF
                            spPart = std::make_shared<Response>( Response::Type::Integer, pTopEntryStart + 1, Length );
                            NotificationSink_.debug( "ResponseHandler::dataReceived(): integer parsed '{}'", std::string(pTopEntryStart + 1, Length) );
                            break;

                        case '$':
                        {
                            // $ denotes an bulkstring - the integer value from the first byte following the typeindicator to the CRLF indicates the 
                            //   number of bytes in the string - without the required CRLF following the data

                            // Number of bytes in bulkstring
                            auto BulkstringSize = local_atoi( pTopEntryStart + 1, pTopEntryStart + 1 + Length );
                            // Support for "Null Bulk String" - returns a null object according to spec
                            if( BulkstringSize == -1 )
                            {
                                spPart = std::make_shared<Response>();
                                break;
                            }
                            // CRLF following the data
                            BulkstringSize += 2;
                            // unparsed bytes remaining in the current chunk
                            auto RemainingBytes = (pEnd - pCurrent) - 1;

                            // Simple case: bulkstring is complete in buffer
                            if( RemainingBytes >= BulkstringSize )
                            {
                                NotificationSink_.debug( "ResponseHandler::dataReceived(): bulkstring parsed, all bytes in buffer '{}'", std::string(pCurrent + 1, BulkstringSize - 2) );

                                // check \r\n
                                spPart = std::make_shared<Response>( Response::Type::BulkString, pCurrent + 1, BulkstringSize - 2 );
                                pCurrent += BulkstringSize;
                                ParsePosition_ += BulkstringSize;
                                ParsedBytesInBufferAdjustment_ = 0;
                                break;
                            }

                            NotificationSink_.debug( "ResponseHandler::dataReceived(): bulkstring parsed, not all bytes in buffer - BulkstringSize: {} RemainingBytes: {}", BulkstringSize, RemainingBytes );

                            // not all needed data is available - wait for more ...
                            BytesToExpect = BulkstringSize - RemainingBytes;
                            // Use all remaining bytes available - this forces the end of the parse loop
                            pCurrent += RemainingBytes;

                            ParsedBytesInBuffer_ += RemainingBytes;
                            ParsedBytesInBufferAdjustment_ = 1 + RemainingBytes;
                            ParsePosition_--;
                            break;
                        }

                        case '*':
                        {
                            // $ denotes an array - the integer value from the first byte following the typeindicator to the CRLF indicates the 
                            //   number of elements contained in the array

                            // Number of items in array
                            off_t Items = local_atoi( pTopEntryStart + 1, pTopEntryStart + 1 + Length );

                            NotificationSink_.debug( "ResponseHandler::dataReceived(): array parsed, itemcount {}", Items );

                            // Support for "Null Array" - returns a null object according to spec
                            if( Items == -1 )
                            {
                                spPart = std::make_shared<Response>();
                                break;
                            }
                            // Empty array
                            if( Items == 0 )
                            {
                                spPart = std::make_shared<Response>( Response::ElementContainer{} );
                                break;
                            }

                            // reset indicators now, as there is no spPart as a result
                            // and the reset is only performed when a part is specified in spPart
                            CRSeen_ = false;
                            CRLFSeen_ = true;

                            // reset - compensate for increment
                            ParsedBytesInBuffer_ = -1;
                            if ( ParsePosition_ )
                                StartPosition_ = ParsePosition_ + 1;
                            //ParsePosition_ = -1;

                            // add to stack of elements
                            Partstack_.emplace( Items );

                            break;
                        }

                        default:
                            // throw invalid type
                            break;
                    }

                    // Part parsed?
                    if( spPart )
                    {
                        NotificationSink_.debug( "ResponseHandler::dataReceived(): part of response completely parsed" );

                        // reset indicators
                        CRSeen_ = false;
                        CRLFSeen_ = true;
                        // reset - compensate for increment
                        ParsedBytesInBuffer_ = -1;
                        if ( ParsePosition_ )
                            StartPosition_ = ParsePosition_ + 1;


                        while( !Partstack_.empty() )
                        {
                            NotificationSink_.debug( "ResponseHandler::dataReceived(): Removing part from partstack" );

                            // every byte starts a new element and pushes an entry on the stack
                            // as we now have finished the latest element, we remove it from the stack
                            Partstack_.pop();

                            // if the stack at this point is empty, the complete parse has finished
                            if( Partstack_.empty() )
                            {
                                // the latest entry becomes the toplevel element of the parse
                                spTop_ = spPart;

                                // Indicate that the parse has finished
                                ToplevelFinished = true;

                                break;
                            }

                            // get the last entry on the partstack
                            auto& TopEntry = Partstack_.top();
                            // if spParts_ contains a nested parts collection (an array parse)
                            if( TopEntry.spParts_ )
                            {
                                // place spPart at the position indicated by CurrentEntry_
                                TopEntry.spParts_->at( TopEntry.CurrentEntry_++ ) = spPart;

                                // if all elements have beeen seen, move the nested partlist to the current part
                                if( TopEntry.CurrentEntry_ >= TopEntry.spParts_->size() )
                                    spPart = std::make_shared<Response>( std::move( *TopEntry.spParts_ ) );
                                else
                                    break;
                            }

                            // repeat the stack upward
                        }
                    }

                    // Skip "normal" processing
                    continue;
                }

                // Was the last processd byte part the LF of a CR LF combination?
                if( CRLFSeen_ )
                {
                    // then save the current position as the first position for the next component
                    StartPosition_ = ParsePosition_;

                    NotificationSink_.debug( "ResponseHandler::dataReceived(): CRLF (or first call) seen - setting StartPosition {}", StartPosition_ );

                    // reset the flag
                    CRLFSeen_ = false;

                    // and ready the next element
                    Partstack_.emplace();
                }
                else
                    if( '\r' == *pCurrent )
                        CRSeen_ = true;
            } // for

              // the parse is finished when there are no further parts pending on the stack and a CRLF combination has been seen
            bool FinishedParsing = Partstack_.empty() && CRLFSeen_;

            // update count
            UnparsedBytesInBuffer_ = pEnd - pCurrent;

            // if parsing is not finished at this point, there are two possibilities:
            // either is the number of bytes in the current chunk not sufficient enough to fullfill the request
            // or the current buffer is not large enough to hold the total number of expected bytes
            if( !FinishedParsing )
            {
                //BuffersizeRemaining -= Offset_;

                NotificationSink_.debug( "ResponseHandler::dataReceived(): not finished BuffersizeRemaining:{} ParsedBytesInBuffer:{} UnparsedBytesInBuffer:{} Offset:{} StartPosition:{} BytesToExpect:{} ParsePosition:{}", BuffersizeRemaining, ParsedBytesInBuffer_, UnparsedBytesInBuffer_, Offset_, StartPosition_, BytesToExpect, ParsePosition_ );

                // Is no buffer left or will the expected data not fit in the current buffer?
                auto RequiredSize = ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ + Offset_ + StartPosition_ + BytesToExpect + 1;

                auto bs = boost::asio::buffer_size( raw_buffer() );

                NotificationSink_.debug( "ResponseHandler::dataReceived(): not finished parsing - RequiredSize:{} Buffersize:{} BytesToExpect:{} StartPosition:{}", RequiredSize, bs, BytesToExpect, StartPosition_ );

                if( RequiredSize > bs )
                //if( !BuffersizeRemaining || BuffersizeRemaining < BytesToExpect )
                {
                    Buffersize_ *= 2;

                    auto RequiredBuffersize = std::max( RequiredSize, Buffersize_ );

                    // pointer to the data in the old buffer
                    InternalBufferType::const_pointer pTopEntryStart = raw_buffer_pointer() + Offset_ + StartPosition_;

                    // Add a new buffer with the computed size
                    spBufferContainer_->emplace_back( RequiredBuffersize );

                    NotificationSink_.debug( "ResponseHandler::dataReceived(): allocation new buffer - RequiredBuffersize:{} transfered bytes:{}", RequiredBuffersize, ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ );

                    // copy the still needed data from the old buffer to the new buffer
                    memcpy( raw_buffer_pointer(), pTopEntryStart, ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ );

                    // if there is already parsed data, then correct the latest parsed position
                    if( ParsedBytesInBuffer_ )
                        ParsePosition_ -= StartPosition_;
                    else
                    {
                        ParsePosition_ = 0;
                        CRLFSeen_ = false;
                        Partstack_.emplace();
                    }

                    Offset_ = 0;
                    StartPosition_ = 0;

                }
            }
            else
            {
                NotificationSink_.debug( "ResponseHandler::dataReceived(): finished parsing" );
            }

            return FinishedParsing;
        }

        // Return a boost::asio::mutable_buffer where data to be processed by this class should be placed
        boost::asio::mutable_buffer buffer()
        {
            if( (ParsedBytesInBuffer_ + UnparsedBytesInBuffer_) == boost::asio::buffer_size( raw_buffer() ) )
            {
                spBufferContainer_->emplace_back( Buffersize_ );

                NotificationSink_.debug( "ResponseHandler::buffer(): allocation new buffer level {} - ParsedBytesInBuffer:{} UnparsedBytesInBuffer:{} Buffersize:{}", spBufferContainer_->size(), ParsedBytesInBuffer_, UnparsedBytesInBuffer_, boost::asio::buffer_size( raw_buffer() ) );
                ParsePosition_ = 0;
                Offset_ = 0;
                StartPosition_ = 0;
                ParsedBytesInBuffer_ = 0;
            }
            else
                NotificationSink_.debug( "ResponseHandler::buffer(): using current buffer level {} - ParsedBytesInBuffer:{} UnparsedBytesInBuffer:{} Offset:{} StartPosition:{} Buffersize:{}", spBufferContainer_->size(), ParsedBytesInBuffer_, UnparsedBytesInBuffer_, Offset_, StartPosition_, boost::asio::buffer_size( raw_buffer() ) );

            return raw_buffer() + ParsedBytesInBuffer_ + UnparsedBytesInBuffer_ + Offset_ + StartPosition_;
        }

        // commits the current parsed element and tries to complete the next toplevel parse
        // returns true if a parse at the topmost level has finished
        bool commit( bool KeepBuffer = false )
        {
            NotificationSink_.debug( "ResponseHandler::commit(): ParsedBytesInBuffer:{} UnparsedBytesInBuffer:{} Offset:{} Buffersize:{} Keepbuffer:{}", ParsedBytesInBuffer_, UnparsedBytesInBuffer_, Offset_, boost::asio::buffer_size( raw_buffer() ), KeepBuffer );

            // Simple case: No valid data in buffer
            if( !UnparsedBytesInBuffer_ )
            {
                if( !KeepBuffer )
                    reset();

                spTop_.reset();
                ParsedBytesInBuffer_ = ParsePosition_;
                ParsePosition_ = 0;

                return false;
            }

            // Still Data available...
            if( !KeepBuffer )
            {
                // Free surplus Buffers
                resetBuffers();
            }

            spTop_.reset();
            Offset_ += ParsePosition_;
            ParsePosition_ = 0;
            ParsedBytesInBufferAdjustment_ = 0;
            StartPosition_ = 0;

            // CRLFSeen_ is already true when we reach here

            return dataReceived( 0 );
        }

        // clears existing buffers and resets all internal state, ready to begin some new processing
        void reset()
        {
            // resets Buffersize_, so call before reinit
            internalReset();

            resetBuffers();
        }

        // returns the topmost parsed result
        const Response& top() const { return *spTop_; }

        // After a completed parse this returns the toplevel Response element.
        std::shared_ptr<Response> spTop() { return spTop_; }

        std::shared_ptr<BufferContainerType> bufferContainer() { return spBufferContainer_; }

    private:
        // Entity representing an entry on the parsestack
        struct ParseStackEntry
        {
            // Nested parts
            std::shared_ptr<Response::ElementContainer> spParts_;
            // Index of the current entry in the spParts_ container
            size_t CurrentEntry_ = 0;

            ParseStackEntry()
            {}
            ParseStackEntry( size_t Items ) :
                spParts_( std::make_shared<Response::ElementContainer>( Items ) )
            {}
            ParseStackEntry( const ParseStackEntry& ) = delete;
            ParseStackEntry& operator=( const ParseStackEntry& ) = delete;

        };

        NotificationSinkType_ NotificationSink_;

        // Size of the initial buffer after first initialization or reset of the ResponseHandler
        size_t InitialBuffersize_;
        // Current Buffersize - dynamicly adjusted during processing
        size_t Buffersize_;
        std::shared_ptr<BufferContainerType> spBufferContainer_;
        // after a completed parse this member contains the toplevel Response element
        std::shared_ptr<Response> spTop_;

        // Stack of Responsecomponents
        std::stack<ParseStackEntry> Partstack_;

        // Position of the first element of the current toplevel element in the buffer
        InternalBufferType::size_type Offset_;
        // Position of the first element of the current element relative to the start of the current toplevel element in the buffer
        InternalBufferType::size_type StartPosition_;
        // Position of the last parsed position relative to the start of the current element in the buffer
        InternalBufferType::size_type ParsePosition_;
        // Number of bytes in the active buffer already visited
        InternalBufferType::size_type ParsedBytesInBuffer_;
        // Number of bytes not parsed in the active buffer
        InternalBufferType::size_type UnparsedBytesInBuffer_;
        // Number of bytes used to adjust ParsedBytesInBuffer_ during bulkstring reception
        size_t ParsedBytesInBufferAdjustment_;

        // Indikator if the last byte seen was an CR
        bool CRSeen_;
        // Indikator if the last two bytes seen was an CR LF combination
        bool CRLFSeen_;

        // returns a pointer to the current active buffer
        const InternalBufferType::pointer raw_buffer_pointer() 
        {
            return spBufferContainer_->back().data();
        }

        // returns the current active buffer
        boost::asio::mutable_buffer raw_buffer() 
        {
            return boost::asio::buffer( spBufferContainer_->back() );
        }

        // Local version of atoi with bounds checking
        static off_t local_atoi( const char *p, const char *pEnd ) {
            off_t  x = 0;
            bool neg = false;
            if ( p == pEnd )
                return 0;

            if( *p == '-' ) {
                neg = true;
                ++p;
            }
            while( p < pEnd && *p >= '0' && *p <= '9' ) {
                x = (x * 10) + (*p - '0');
                ++p;
            }
            if( neg ) {
                x = -x;
            }
            return x;
        }

        // resets all buffers
        void resetBuffers()
        {
            // Free surplus buffers
            if( spBufferContainer_->size() > 1 )
            {
                std::iter_swap( spBufferContainer_->begin(), --spBufferContainer_->end() );
                spBufferContainer_->resize( 1 );
            }
        }

        // resets all internal state
        void internalReset()
        {
            // Reset buffersize to default
            Buffersize_ = InitialBuffersize_;

            spTop_.reset();

            // Remove all previous parts - std::stack has no clear
            while( !Partstack_.empty() )
                Partstack_.pop();

            CRSeen_ = false;
            CRLFSeen_ = true;
            ParsePosition_ = 0;
            StartPosition_ = 0;
            ParsedBytesInBuffer_ = 0;
            UnparsedBytesInBuffer_ = 0;
            Offset_ = 0;
            ParsedBytesInBufferAdjustment_ = 0;
        }
    };
}

#endif
