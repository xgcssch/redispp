#ifndef REDISPP_SENTINELSCONNECTIONMANAGER_INCLUDED
#define REDISPP_SENTINELSCONNECTIONMANAGER_INCLUDED

// Copyright Soenke K. Schau 2016-2017
// See accompanying file LICENSE.txt for Lincense

#include <string>
#include <list>
#include <memory>
#include <thread>
#include <chrono>

#include "redispp/Connection.h"
#include "redispp/Commands.h"
#include "redispp/SentinelCommands.h"
#include "redispp/MultipleHostsConnectionManager.h"
#include "redispp/Error.h"

namespace redis
{
    template<class DebugStreamType_=NullDebugStream, class NotificationSinkType_=NullNotificationSink>
    class SentinelConnectionManager
    {
    public:
        typedef std::tuple<std::string, int> Host;
        typedef typename MultipleHostsConnectionManager<NotificationSinkType_>::HostContainer HostContainer;

        class Instance
        {
            constexpr double TimeoutInSeconds() const {
                return 60.;
            }
        public:
            Instance( const Instance& ) = default;
            Instance& operator=( const Instance& ) = delete;

            Instance( typename MultipleHostsConnectionManager<NotificationSinkType_>::HostContainer& InitialHosts, const std::string& MasterSet, NotificationSinkType_ NotificationSink ) :
                InitialHosts_( InitialHosts ),
                Hosts_( InitialHosts.get() ),
                MasterSet_( MasterSet ),
                NotificationSink_(NotificationSink)
            {}

            boost::asio::ip::tcp::socket getConnectedSocket( boost::asio::io_service& io_service, boost::system::error_code& ec )
            {
                MultipleHostsConnectionManager<NotificationSinkType_> mhcm( io_service, Hosts_, NotificationSink_ );
                redis::Connection<redis::MultipleHostsConnectionManager<NotificationSinkType_>, DebugStreamType_, NotificationSinkType_> SentinelConnection( io_service, mhcm, 0, NotificationSink_ );

                std::chrono::time_point<std::chrono::steady_clock> ConnectionStartTime;
                ConnectionStartTime = std::chrono::steady_clock::now();

                for( size_t Hostcount = Hosts_.size(); Hostcount; )
                {
                    std::chrono::duration<double> elapsed_seconds = std::chrono::steady_clock::now() - ConnectionStartTime;
                    if( elapsed_seconds.count() > TimeoutInSeconds() )
                    {
                        NotificationSink_.error( "SentinelConnectionManager::getConnectedSocket: No usable server after {} seconds", TimeoutInSeconds() );
                        break;
                    }

                    auto GetMasterAddrByNameResult = redis::sentinel_getMasterAddrByName( SentinelConnection, ec, MasterSet_ );
                    if( !ec )
                    {
                        auto rh = SentinelConnection.remote_endpoint();

                        NotificationSink_.debug( "SentinelConnectionManager::getConnectedSocket: Using Sentinel '{}'", rh );

                        NotificationSink_.debug( "SentinelConnectionManager::getConnectedSocket: Got Master '{}' for set '{}'", GetMasterAddrByNameResult.second, MasterSet_ );

                        // Update Sentinel List
                        auto GetSentinelsResult = redis::sentinel_sentinels( SentinelConnection, ec, MasterSet_ );
                        if( !ec )
                        {
                            Hosts_.clear();
                            // Current Sentinel becomes first in list
                            Hosts_.push_back( SentinelConnection.remote_endpoint() );
                            std::for_each( GetSentinelsResult.second.begin(), GetSentinelsResult.second.end(), [this]( const auto& SentinelProperties ) { Hosts_.emplace_back( SentinelProperties.at( "ip" ), std::stoi( SentinelProperties.at( "port" ) ) ); } );

                            InitialHosts_.set( Hosts_ );

                            NotificationSink_.debug( "SentinelConnectionManager::getConnectedSocket: Sentinel list updated - now {} sentinels available for next connection", Hosts_.size() );
                        }

                        SingleHostConnectionManager shcm( GetMasterAddrByNameResult.second );
                        redis::Connection<redis::SingleHostConnectionManager, DebugStreamType_, NotificationSinkType_> MasterConnection( io_service, shcm, 0, NotificationSink_ );

                        // Test if the master aggrees with its role
                        auto Role = redis::role( MasterConnection, ec );
                        if( !ec && Role.second == "master" )
                        {
                            // Return the active connection to the caller

                            NotificationSink_.trace( "SentinelConnectionManager::getConnectedSocket: Master '{}' agreed to role - using it for further requests", GetMasterAddrByNameResult.second );

                            return MasterConnection.passSocket();
                        }
                        else
                        {
                            using namespace std::literals;

                            if( ec )
                                NotificationSink_.warning( "SentinelConnectionManager::getConnectedSocket: server returned error during role command: {} ", ec.message() );
                            else
                                NotificationSink_.warning( "SentinelConnectionManager::getConnectedSocket: server returned wrong role '{}' ", Role.second );

                            // Wait a short amount of time
                            std::this_thread::sleep_for( 1s );
                            continue;
                        }
                    }
                    else
                        NotificationSink_.warning( "SentinelConnectionManager::getConnectedSocket: server returned error during getMasterAddrByName command: {} ", ec.message() );

                    //std::cerr << "Shifting Sentinel Hostslist" << std::endl;
                    SentinelConnection.instance().shiftHosts();
                    --Hostcount;
                }

                NotificationSink_.error( "SentinelConnectionManager::getConnectedSocket: no more sentinels left to ask!" );

                ec = ::redis::make_error_code( ErrorCodes::no_more_sentinels );

                return boost::asio::ip::tcp::socket( io_service );
            }

        private:
            typename MultipleHostsConnectionManager<NotificationSinkType_>::HostContainer& InitialHosts_;
            typename HostContainer::ContainerType Hosts_;
            const std::string& MasterSet_;
            NotificationSinkType_ NotificationSink_;
            std::shared_ptr<MultipleHostsConnectionManager<NotificationSinkType_> > spInnerConnectionManager_;
        };


        SentinelConnectionManager(const SentinelConnectionManager&) = delete;
        SentinelConnectionManager& operator=(const SentinelConnectionManager&) = delete;

        SentinelConnectionManager( boost::asio::io_service& io_service, const typename HostContainer::ContainerType& Hosts, const std::string& MasterSet, NotificationSinkType_ NotificationSink = NotificationSinkType_{} ) :
            Hosts_(Hosts),
            MasterSet_( MasterSet ),
            NotificationSink_(NotificationSink),
            Strand_(io_service)
        {
        }

        SentinelConnectionManager(boost::asio::io_service& io_service, typename HostContainer::ContainerType&& Hosts, const std::string& MasterSet, NotificationSinkType_ NotificationSink = NotificationSinkType_{} ) :
            Hosts_(std::move(Hosts)),
            MasterSet_(MasterSet),
            NotificationSink_(NotificationSink),
            Strand_(io_service)
        {
        }

        Instance getInstance() const
        {
            return Instance( Hosts_, MasterSet_, NotificationSink_ );
        }

    private:
        boost::asio::io_service::strand Strand_;
        mutable typename MultipleHostsConnectionManager<NotificationSinkType_>::HostContainer Hosts_;
        std::string MasterSet_;
        NotificationSinkType_ NotificationSink_;
    };
}

#endif
