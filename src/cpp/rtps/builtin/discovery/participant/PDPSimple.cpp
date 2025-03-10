// Copyright 2016 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file PDPSimple.cpp
 *
 */
#include <fastdds/rtps/builtin/discovery/participant/PDPSimple.h>

#include <mutex>

#include <fastdds/dds/builtin/typelookup/TypeLookupManager.hpp>
#include <fastdds/dds/log/Log.hpp>
#include <fastdds/rtps/builtin/BuiltinProtocols.h>
#include <fastdds/rtps/builtin/data/NetworkConfiguration.hpp>
#include <fastdds/rtps/builtin/data/ParticipantProxyData.h>
#include <fastdds/rtps/builtin/data/ReaderProxyData.h>
#include <fastdds/rtps/builtin/data/WriterProxyData.h>
#include <fastdds/rtps/builtin/discovery/endpoint/EDPSimple.h>
#include <fastdds/rtps/builtin/discovery/endpoint/EDPStatic.h>
#include <fastdds/rtps/builtin/discovery/participant/PDPListener.h>
#include <fastdds/rtps/builtin/liveliness/WLP.h>
#include <fastdds/rtps/history/ReaderHistory.h>
#include <fastdds/rtps/history/WriterHistory.h>
#include <fastdds/rtps/participant/RTPSParticipantListener.h>
#include <fastdds/rtps/reader/StatefulReader.h>
#include <fastdds/rtps/reader/StatelessReader.h>
#include <fastdds/rtps/resources/TimedEvent.h>
#include <fastdds/rtps/writer/StatelessWriter.h>
#include <fastrtps/utils/IPLocator.h>
#include <fastrtps/utils/TimeConversion.h>

#include <rtps/builtin/discovery/participant/simple/SimplePDPEndpoints.hpp>
#include <rtps/history/TopicPayloadPoolRegistry.hpp>
#include <rtps/participant/RTPSParticipantImpl.h>

using namespace eprosima::fastrtps;

namespace eprosima {
namespace fastrtps {
namespace rtps {


PDPSimple::PDPSimple (
        BuiltinProtocols* built,
        const RTPSParticipantAllocationAttributes& allocation)
    : PDP(built, allocation)
{
}

PDPSimple::~PDPSimple()
{
}

void PDPSimple::update_builtin_locators()
{
    auto endpoints = static_cast<fastdds::rtps::SimplePDPEndpoints*>(builtin_endpoints_.get());
    mp_builtin->updateMetatrafficLocators(endpoints->reader.reader_->getAttributes().unicastLocatorList);
}

void PDPSimple::initializeParticipantProxyData(
        ParticipantProxyData* participant_data)
{
    PDP::initializeParticipantProxyData(participant_data);

    if (getRTPSParticipant()->getAttributes().builtin.discovery_config.
                    use_SIMPLE_EndpointDiscoveryProtocol)
    {
        if (getRTPSParticipant()->getAttributes().builtin.discovery_config.m_simpleEDP.
                        use_PublicationWriterANDSubscriptionReader)
        {
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_PUBLICATION_ANNOUNCER;
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_SUBSCRIPTION_DETECTOR;
        }

        if (getRTPSParticipant()->getAttributes().builtin.discovery_config.m_simpleEDP.
                        use_PublicationReaderANDSubscriptionWriter)
        {
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_PUBLICATION_DETECTOR;
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_SUBSCRIPTION_ANNOUNCER;
        }

#if HAVE_SECURITY
        if (getRTPSParticipant()->getAttributes().builtin.discovery_config.m_simpleEDP.
                        enable_builtin_secure_publications_writer_and_subscriptions_reader)
        {
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_PUBLICATION_SECURE_ANNOUNCER;
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_SUBSCRIPTION_SECURE_DETECTOR;
        }

        if (getRTPSParticipant()->getAttributes().builtin.discovery_config.m_simpleEDP.
                        enable_builtin_secure_subscriptions_writer_and_publications_reader)
        {
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_SUBSCRIPTION_SECURE_ANNOUNCER;
            participant_data->m_availableBuiltinEndpoints |= DISC_BUILTIN_ENDPOINT_PUBLICATION_SECURE_DETECTOR;
        }
#endif // if HAVE_SECURITY
    }
    else if (!getRTPSParticipant()->getAttributes().builtin.discovery_config.
                    use_STATIC_EndpointDiscoveryProtocol)
    {
        EPROSIMA_LOG_ERROR(RTPS_PDP, "Neither EDP simple nor EDP static enabled. Endpoints will not be discovered.");
    }
}

bool PDPSimple::init(
        RTPSParticipantImpl* part)
{
    // The DATA(p) must be processed after EDP endpoint creation
    if (!PDP::initPDP(part))
    {
        return false;
    }

    //INIT EDP
    if (m_discovery.discovery_config.use_STATIC_EndpointDiscoveryProtocol)
    {
        mp_EDP = new EDPStatic(this, mp_RTPSParticipant);
        if (!mp_EDP->initEDP(m_discovery))
        {
            EPROSIMA_LOG_ERROR(RTPS_PDP, "Endpoint discovery configuration failed");
            delete mp_EDP;
            mp_EDP = nullptr;
            return false;
        }
    }
    else if (m_discovery.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol)
    {
        mp_EDP = new EDPSimple(this, mp_RTPSParticipant);
        if (!mp_EDP->initEDP(m_discovery))
        {
            EPROSIMA_LOG_ERROR(RTPS_PDP, "Endpoint discovery configuration failed");
            delete mp_EDP;
            mp_EDP = nullptr;
            return false;
        }
    }
    else
    {
        EPROSIMA_LOG_WARNING(RTPS_PDP, "No EndpointDiscoveryProtocol defined");
        return false;
    }

    return true;
}

ParticipantProxyData* PDPSimple::createParticipantProxyData(
        const ParticipantProxyData& participant_data,
        const GUID_t&)
{
    std::unique_lock<std::recursive_mutex> lock(*getMutex());

    // decide if we dismiss the participant using the ParticipantFilteringFlags
    const ParticipantFilteringFlags_t& flags = m_discovery.discovery_config.ignoreParticipantFlags;
    const GUID_t& remote = participant_data.m_guid;
    const GUID_t& local = getLocalParticipantProxyData()->m_guid;
    bool is_same_host = local.is_on_same_host_as(remote);
    bool is_same_process = local.is_on_same_process_as(remote);

    // Discard participants on different process when they don't have metatraffic locators
    if (participant_data.metatraffic_locators.multicast.empty() &&
            participant_data.metatraffic_locators.unicast.empty() &&
            !is_same_process)
    {
        return nullptr;
    }

    if (flags != ParticipantFilteringFlags_t::NO_FILTER)
    {
        if (!is_same_host)
        {
            if (flags & ParticipantFilteringFlags::FILTER_DIFFERENT_HOST)
            {
                return nullptr;
            }
        }
        else
        {
            bool filter_same = (flags& ParticipantFilteringFlags::FILTER_SAME_PROCESS) != 0;
            bool filter_different = (flags& ParticipantFilteringFlags::FILTER_DIFFERENT_PROCESS) != 0;

            if (filter_same && filter_different)
            {
                return nullptr;
            }

            if ((filter_same && is_same_process) || (filter_different && !is_same_process))
            {
                return nullptr;
            }
        }
    }

    ParticipantProxyData* pdata = add_participant_proxy_data(participant_data.m_guid, true, &participant_data);
    if (pdata != nullptr)
    {
        pdata->lease_duration_event->update_interval(pdata->m_leaseDuration);
        pdata->lease_duration_event->restart_timer();
    }

    return pdata;
}

// EDPStatic requires matching on ParticipantProxyData property updates
bool PDPSimple::updateInfoMatchesEDP()
{
    return dynamic_cast<EDPStatic*>(mp_EDP) != nullptr;
}

void PDPSimple::announceParticipantState(
        bool new_change,
        bool dispose /* = false */)
{
    WriteParams __wp = WriteParams::write_params_default();
    announceParticipantState(new_change, dispose, __wp);
}

void PDPSimple::announceParticipantState(
        bool new_change,
        bool dispose,
        WriteParams& wp)
{
    if (enabled_)
    {
        auto endpoints = static_cast<fastdds::rtps::SimplePDPEndpoints*>(builtin_endpoints_.get());
        StatelessWriter& writer = *(endpoints->writer.writer_);
        WriterHistory& history = *(endpoints->writer.history_);

        PDP::announceParticipantState(writer, history, new_change, dispose, wp);

        if (!(dispose || new_change))
        {
            writer.unsent_changes_reset();
        }
    }
}

bool PDPSimple::createPDPEndpoints()
{
    EPROSIMA_LOG_INFO(RTPS_PDP, "Beginning");

    const RTPSParticipantAttributes& pattr = mp_RTPSParticipant->getRTPSParticipantAttributes();
    const RTPSParticipantAllocationAttributes& allocation = pattr.allocation;
    const BuiltinAttributes& builtin_att = mp_builtin->m_att;

    auto endpoints = new fastdds::rtps::SimplePDPEndpoints();
    builtin_endpoints_.reset(endpoints);

    //SPDP BUILTIN RTPSParticipant READER
    HistoryAttributes hatt;
    hatt.payloadMaxSize = builtin_att.readerPayloadSize;
    hatt.memoryPolicy = builtin_att.readerHistoryMemoryPolicy;
    hatt.initialReservedCaches = 25;
    if (allocation.participants.initial > 0)
    {
        hatt.initialReservedCaches = (int32_t)allocation.participants.initial;
    }
    if (allocation.participants.maximum < std::numeric_limits<size_t>::max())
    {
        hatt.maximumReservedCaches = (int32_t)allocation.participants.maximum;
    }

    PoolConfig reader_pool_cfg = PoolConfig::from_history_attributes(hatt);
    endpoints->reader.payload_pool_ = TopicPayloadPoolRegistry::get("DCPSParticipant", reader_pool_cfg);
    endpoints->reader.payload_pool_->reserve_history(reader_pool_cfg, true);

    endpoints->reader.history_.reset(new ReaderHistory(hatt));

    ReaderAttributes ratt;
    ratt.endpoint.multicastLocatorList = mp_builtin->m_metatrafficMulticastLocatorList;
    ratt.endpoint.unicastLocatorList = mp_builtin->m_metatrafficUnicastLocatorList;
    ratt.endpoint.external_unicast_locators = mp_builtin->m_att.metatraffic_external_unicast_locators;
    ratt.endpoint.ignore_non_matching_locators = pattr.ignore_non_matching_locators;
    ratt.endpoint.topicKind = WITH_KEY;
    ratt.endpoint.durabilityKind = TRANSIENT_LOCAL;
    ratt.endpoint.reliabilityKind = BEST_EFFORT;
    ratt.matched_writers_allocation = allocation.participants;
    mp_listener = new PDPListener(this);
    RTPSReader* reader = nullptr;
    if (mp_RTPSParticipant->createReader(&reader, ratt,
            endpoints->reader.payload_pool_, endpoints->reader.history_.get(),
            mp_listener, c_EntityId_SPDPReader, true, false))
    {
        endpoints->reader.reader_ = dynamic_cast<StatelessReader*>(reader);
#if HAVE_SECURITY
        mp_RTPSParticipant->set_endpoint_rtps_protection_supports(reader, false);
#endif // if HAVE_SECURITY
    }
    else
    {
        EPROSIMA_LOG_ERROR(RTPS_PDP, "SimplePDP Reader creation failed");
        delete mp_listener;
        mp_listener = nullptr;
        endpoints->reader.release();
        return false;
    }

    //SPDP BUILTIN RTPSParticipant WRITER
    hatt.payloadMaxSize = mp_builtin->m_att.writerPayloadSize;
    hatt.initialReservedCaches = 1;
    hatt.maximumReservedCaches = 1;
    hatt.memoryPolicy = mp_builtin->m_att.writerHistoryMemoryPolicy;

    PoolConfig writer_pool_cfg = PoolConfig::from_history_attributes(hatt);
    endpoints->writer.payload_pool_ = TopicPayloadPoolRegistry::get("DCPSParticipant", writer_pool_cfg);
    endpoints->writer.payload_pool_->reserve_history(writer_pool_cfg, false);

    endpoints->writer.history_.reset(new WriterHistory(hatt));
    WriterAttributes watt;
    watt.endpoint.external_unicast_locators = mp_builtin->m_att.metatraffic_external_unicast_locators;
    watt.endpoint.ignore_non_matching_locators = pattr.ignore_non_matching_locators;
    watt.endpoint.endpointKind = WRITER;
    watt.endpoint.durabilityKind = TRANSIENT_LOCAL;
    watt.endpoint.reliabilityKind = BEST_EFFORT;
    watt.endpoint.topicKind = WITH_KEY;
    watt.endpoint.remoteLocatorList = m_discovery.initialPeersList;
    watt.matched_readers_allocation = allocation.participants;

    if (pattr.throughputController.bytesPerPeriod != UINT32_MAX && pattr.throughputController.periodMillisecs != 0)
    {
        watt.mode = ASYNCHRONOUS_WRITER;
    }

    RTPSWriter* wout = nullptr;
    if (mp_RTPSParticipant->createWriter(&wout, watt, endpoints->writer.payload_pool_, endpoints->writer.history_.get(),
            nullptr,
            c_EntityId_SPDPWriter, true))
    {
        endpoints->writer.writer_ = dynamic_cast<StatelessWriter*>(wout);
#if HAVE_SECURITY
        mp_RTPSParticipant->set_endpoint_rtps_protection_supports(wout, false);
#endif // if HAVE_SECURITY
        if (endpoints->writer.writer_ != nullptr)
        {
            const NetworkFactory& network = mp_RTPSParticipant->network_factory();
            LocatorList_t fixed_locators;
            for (const Locator_t& loc : mp_builtin->m_initialPeersList)
            {
                if (network.is_locator_remote_or_allowed(loc))
                {
                    // Add initial peers locator without transformation as we don't know whether the
                    // remote transport will allow localhost
                    fixed_locators.push_back(loc);

                    /**
                     * TCP special case:
                     *
                     * In TCP, it is not possible to open a socket with 'any' (0.0.0.0) address as it's done
                     * in UDP, so when the TCP transports receive a locator with 'any', they open an input
                     * channel for the specified port in each of the machine interfaces (with the exception
                     * of localhost). In fact, a participant with a TCP transport will only listen on localhost
                     * if localhost is the address of any of the initial peers.
                     *
                     * However, when the TCP enabled participant does not have a whitelist (or localhost is in
                     * it), it allows for transformation of its locators to localhost for performance optimizations.
                     * In this case, the remote TCP participant it will send data using a socket in localhost,
                     * and for that the participant with the initial peers list needs to be listening there
                     * to receive it.
                     *
                     * That means:
                     *   1. Checking that the initial peer is not already localhost
                     *   2. Checking that the initial peer locator is of TCP kind
                     *   3. Checking that the network configuration allows for localhost locators
                     */
                    Locator_t local_locator;
                    network.transform_remote_locator(loc, local_locator,
                            DISC_NETWORK_CONFIGURATION_LISTENING_LOCALHOST_ALL);
                    if (loc != local_locator
                            && (loc.kind == LOCATOR_KIND_TCPv4 || loc.kind == LOCATOR_KIND_TCPv6)
                            && network.is_locator_allowed(local_locator))
                    {
                        fixed_locators.push_back(local_locator);
                    }
                }
                else
                {
                    EPROSIMA_LOG_WARNING(RTPS_PDP, "Ignoring initial peers locator " << loc << " : not allowed.");
                }
            }
            endpoints->writer.writer_->set_fixed_locators(fixed_locators);
        }
    }
    else
    {
        EPROSIMA_LOG_ERROR(RTPS_PDP, "SimplePDP Writer creation failed");
        endpoints->writer.release();
        return false;
    }
    EPROSIMA_LOG_INFO(RTPS_PDP, "SPDP Endpoints creation finished");
    return true;
}

void PDPSimple::assignRemoteEndpoints(
        ParticipantProxyData* pdata)
{
    EPROSIMA_LOG_INFO(RTPS_PDP, "For RTPSParticipant: " << pdata->m_guid.guidPrefix);

    auto endpoints = static_cast<fastdds::rtps::SimplePDPEndpoints*>(builtin_endpoints_.get());

    const NetworkFactory& network = mp_RTPSParticipant->network_factory();
    uint32_t endp = pdata->m_availableBuiltinEndpoints;
    uint32_t auxendp = endp;
    bool use_multicast_locators = !mp_RTPSParticipant->getAttributes().builtin.avoid_builtin_multicast ||
            pdata->metatraffic_locators.unicast.empty();
    auxendp &= DISC_BUILTIN_ENDPOINT_PARTICIPANT_ANNOUNCER;
    if (auxendp != 0)
    {
        auto temp_writer_data = get_temporary_writer_proxies_pool().get();

        temp_writer_data->clear();
        temp_writer_data->guid().guidPrefix = pdata->m_guid.guidPrefix;
        temp_writer_data->guid().entityId = c_EntityId_SPDPWriter;
        temp_writer_data->persistence_guid(pdata->get_persistence_guid());
        temp_writer_data->set_persistence_entity_id(c_EntityId_SPDPWriter);
        temp_writer_data->set_remote_locators(pdata->metatraffic_locators, network, use_multicast_locators);
        temp_writer_data->m_qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;
        temp_writer_data->m_qos.m_durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;
        endpoints->reader.reader_->matched_writer_add(*temp_writer_data);
    }
    auxendp = endp;
    auxendp &= DISC_BUILTIN_ENDPOINT_PARTICIPANT_DETECTOR;
    if (auxendp != 0)
    {
        auto temp_reader_data = get_temporary_reader_proxies_pool().get();

        temp_reader_data->clear();
        temp_reader_data->m_expectsInlineQos = false;
        temp_reader_data->guid().guidPrefix = pdata->m_guid.guidPrefix;
        temp_reader_data->guid().entityId = c_EntityId_SPDPReader;
        temp_reader_data->set_remote_locators(pdata->metatraffic_locators, network, use_multicast_locators);
        temp_reader_data->m_qos.m_reliability.kind = BEST_EFFORT_RELIABILITY_QOS;
        temp_reader_data->m_qos.m_durability.kind = TRANSIENT_LOCAL_DURABILITY_QOS;
        endpoints->writer.writer_->matched_reader_add(*temp_reader_data);

        StatelessWriter* pW = endpoints->writer.writer_;

        if (pW != nullptr)
        {
            pW->unsent_changes_reset();
        }
        else
        {
            EPROSIMA_LOG_ERROR(RTPS_PDP, "Using PDPSimple protocol with a reliable writer");
        }
    }

#if HAVE_SECURITY
    // Validate remote participant
    mp_RTPSParticipant->security_manager().discovered_participant(*pdata);
#else
    //Inform EDP of new RTPSParticipant data:
    notifyAboveRemoteEndpoints(*pdata, true);
#endif // if HAVE_SECURITY
}

void PDPSimple::removeRemoteEndpoints(
        ParticipantProxyData* pdata)
{
    EPROSIMA_LOG_INFO(RTPS_PDP, "For RTPSParticipant: " << pdata->m_guid);

    auto endpoints = static_cast<fastdds::rtps::SimplePDPEndpoints*>(builtin_endpoints_.get());

    uint32_t endp = pdata->m_availableBuiltinEndpoints;
    uint32_t auxendp = endp;
    auxendp &= DISC_BUILTIN_ENDPOINT_PARTICIPANT_ANNOUNCER;
    if (auxendp != 0)
    {
        GUID_t writer_guid(pdata->m_guid.guidPrefix, c_EntityId_SPDPWriter);
        endpoints->reader.reader_->matched_writer_remove(writer_guid);
    }
    auxendp = endp;
    auxendp &= DISC_BUILTIN_ENDPOINT_PARTICIPANT_DETECTOR;
    if (auxendp != 0)
    {
        GUID_t reader_guid(pdata->m_guid.guidPrefix, c_EntityId_SPDPReader);
        endpoints->writer.writer_->matched_reader_remove(reader_guid);
    }
}

void PDPSimple::notifyAboveRemoteEndpoints(
        const ParticipantProxyData& pdata,
        bool notify_secure_endpoints)
{
    //Inform EDP of new RTPSParticipant data:
    if (mp_EDP != nullptr)
    {
        mp_EDP->assignRemoteEndpoints(pdata, (notify_secure_endpoints ? true : false));
    }

    if (mp_builtin->mp_WLP != nullptr)
    {
        mp_builtin->mp_WLP->assignRemoteEndpoints(pdata, (notify_secure_endpoints ? true : false));
    }

    if (mp_builtin->tlm_ != nullptr)
    {
        mp_builtin->tlm_->assign_remote_endpoints(pdata);
    }
}

bool PDPSimple::newRemoteEndpointStaticallyDiscovered(
        const GUID_t& pguid,
        int16_t userDefinedId,
        EndpointKind_t kind)
{
    string_255 pname;
    if (lookup_participant_name(pguid, pname))
    {
        if (kind == WRITER)
        {
            dynamic_cast<EDPStatic*>(mp_EDP)->newRemoteWriter(pguid, pname, userDefinedId);
        }
        else
        {
            dynamic_cast<EDPStatic*>(mp_EDP)->newRemoteReader(pguid, pname, userDefinedId);
        }
    }
    return false;
}

} /* namespace rtps */
} /* namespace fastrtps */
} /* namespace eprosima */
