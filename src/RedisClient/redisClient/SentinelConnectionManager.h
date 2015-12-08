#ifndef REDIS_SENTINELSCONNECTIONMANAGER_INCLUDED
#define REDIS_SENTINELSCONNECTIONMANAGER_INCLUDED

#include <string>
#include <list>
#include <memory>

#include "redisClient/Connection.h"
#include "redisClient/Commands.h"
#include "redisClient/MultipleHostsConnectionManager.h"
#include "redisClient/Error.h"

namespace redis
{
    class SentinelConnectionManager
    {
    public:
        typedef std::tuple<std::string, int> Host;
        typedef std::list<Host> HostContainer;

        class Instance
        {
        public:
            Instance( const Instance& ) = default;
            Instance& operator=( const Instance& ) = delete;

            Instance( const MultipleHostsConnectionManager::HostContainer& InitialHosts, const std::string& MasterSet ) :
                Hosts_( InitialHosts ),
                MasterSet_( MasterSet )
            {
            }

            boost::asio::ip::tcp::socket getConnectedSocket( boost::asio::io_service& io_service, boost::system::error_code& ec )
            {
                MultipleHostsConnectionManager mhcm( io_service, Hosts_ );
                redis::Connection<redis::MultipleHostsConnectionManager> SentinelConnection( io_service, mhcm );

                for( size_t Hostcount = Hosts_.size(); Hostcount; --Hostcount )
                {
                    auto GetMasterAddrByNameResult = redis::sentinel_getMasterAddrByName( SentinelConnection, ec, MasterSet_ );
                    if( !ec )
                    {
                        SingleHostConnectionManager shcm( GetMasterAddrByNameResult.first, std::stoi( GetMasterAddrByNameResult.second ) );
                        redis::Connection<redis::SingleHostConnectionManager> MasterConnection( io_service, shcm );

                        // Test if the master aggrees with its role
                        auto Role = redis::role( MasterConnection, ec );
                        if( !ec && Role == "master" )
                        {
                            // Update Sentinel List
                            auto GetSentinelsResult = redis::sentinel_sentinels( SentinelConnection, ec, MasterSet_ );
                            if( !ec )
                            {
                                Hosts_.clear();
                                // Current Sentinel becomes first in list
                                Hosts_.push_back( SentinelConnection.remote_endpoint() );
                                std::for_each( GetSentinelsResult.begin(), GetSentinelsResult.end(), [this]( const auto& SentinelProperties ) { Hosts_.emplace_back( SentinelProperties.at( "ip" ), std::stoi( SentinelProperties.at( "port" ) ) ); } );
                            }

                            // Return the active connection to the caller
                            return MasterConnection.passSocket();
                        }
                    }

                    SentinelConnection.instance().shiftHosts();
                }

                ec = ::redis::make_error_code( ErrorCodes::no_usable_server );

                return boost::asio::ip::tcp::socket( io_service );
            }

        private:
            MultipleHostsConnectionManager::HostContainer Hosts_;
            const std::string& MasterSet_;
            HostContainer::const_iterator CurrentHostIterator_;
            std::shared_ptr<MultipleHostsConnectionManager> spInnerConnectionManager_;
        };


        SentinelConnectionManager(const SentinelConnectionManager&) = delete;
        SentinelConnectionManager& operator=(const SentinelConnectionManager&) = delete;

        SentinelConnectionManager( boost::asio::io_service& io_service, const HostContainer& Hosts, const std::string& MasterSet ) :
            Hosts_(Hosts),
            MasterSet_( MasterSet ),
            Strand_(io_service)
        {
        }

        SentinelConnectionManager(boost::asio::io_service& io_service, HostContainer&& Hosts, const std::string& MasterSet ) :
            Hosts_(std::move(Hosts)),
            MasterSet_(MasterSet),
            Strand_(io_service)
        {
        }

        Instance getInstance() const
        {
            return Instance( Hosts_, MasterSet_ );
        }

    private:
        boost::asio::io_service::strand Strand_;
        MultipleHostsConnectionManager::HostContainer Hosts_;
        std::string MasterSet_;
    };
}

#endif
