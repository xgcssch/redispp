#ifndef REDIS_SENTINELSCONNECTIONMANAGER_INCLUDED
#define REDIS_SENTINELSCONNECTIONMANAGER_INCLUDED

#include <string>
#include <list>
#include <memory>
#include <thread>
#include <chrono>

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
            constexpr double TimeoutInSeconds() const {
                return 60.;
            }
        public:
            Instance( const Instance& ) = default;
            Instance& operator=( const Instance& ) = delete;

            Instance( const MultipleHostsConnectionManager::HostContainer& InitialHosts, const std::string& MasterSet ) :
                Hosts_( InitialHosts ),
                MasterSet_( MasterSet )
            {}

            boost::asio::ip::tcp::socket getConnectedSocket( boost::asio::io_service& io_service, boost::system::error_code& ec )
            {
                MultipleHostsConnectionManager mhcm( io_service, Hosts_ );
                redis::Connection<redis::MultipleHostsConnectionManager> SentinelConnection( io_service, mhcm );

                std::chrono::time_point<std::chrono::steady_clock> ConnectionStartTime;
                ConnectionStartTime = std::chrono::steady_clock::now();

                for( size_t Hostcount = Hosts_.size(); Hostcount; )
                {
                    std::chrono::duration<double> elapsed_seconds = std::chrono::steady_clock::now() - ConnectionStartTime;
                    if( elapsed_seconds.count() > TimeoutInSeconds() )
                    {
                        std::cerr << "No usable server after " << TimeoutInSeconds() << " seconds" << std::endl;
                        break;
                    }

                    auto GetMasterAddrByNameResult = redis::sentinel_getMasterAddrByName( SentinelConnection, ec, MasterSet_ );
                    if( !ec )
                    {
                        auto rh = SentinelConnection.remote_endpoint();
                        std::cerr << "Using Sentinel " << std::get<0>( rh ) << ":" << std::get<1>( rh ) << std::endl;

                        // Update Sentinel List
                        auto GetSentinelsResult = redis::sentinel_sentinels( SentinelConnection, ec, MasterSet_ );
                        if( !ec )
                        {
                            Hosts_.clear();
                            // Current Sentinel becomes first in list
                            Hosts_.push_back( SentinelConnection.remote_endpoint() );
                            std::for_each( GetSentinelsResult.begin(), GetSentinelsResult.end(), [this]( const auto& SentinelProperties ) { Hosts_.emplace_back( SentinelProperties.at( "ip" ), std::stoi( SentinelProperties.at( "port" ) ) ); } );

                            std::cerr << "Sentinel list updated - now " << Hosts_.size()  << " available for next connection" << std::endl;
                        }

                        SingleHostConnectionManager shcm( GetMasterAddrByNameResult.first, std::stoi( GetMasterAddrByNameResult.second ) );
                        redis::Connection<redis::SingleHostConnectionManager> MasterConnection( io_service, shcm );

                        // Test if the master aggrees with its role
                        auto Role = redis::role( MasterConnection, ec );
                        if( !ec && Role == "master" )
                        {
                            // Return the active connection to the caller
                            return MasterConnection.passSocket();
                        }
                        else
                        {
                            using namespace std::literals;

                            if( ec )
                                std::cerr << "role returnded " << ec.message() << std::endl;
                            else
                                std::cerr << "role returnded wrong role " << Role << std::endl;

                            // Wait a short amount of time
                            std::this_thread::sleep_for( 1s );
                            continue;
                        }
                    }
                    else
                    {
                        std::cerr << "getMasterAddrByName returnded " << ec.message() << std::endl;
                    }

                    std::cerr << "Shifting Sentinel Hostslist" << std::endl;
                    SentinelConnection.instance().shiftHosts();
                    --Hostcount;
                }

                std::cerr << "no more sentinels left to ask" << std::endl;

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
