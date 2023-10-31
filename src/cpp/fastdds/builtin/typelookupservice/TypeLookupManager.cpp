// Copyright 2019 Proyectos y Sistemas de Mantenimiento SL (eProsima).
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
 * @file TypeLookupManager.cpp
 *
 */

#include <fastdds/builtin/typelookupservice/TypeLookupManager.hpp>
#include <fastcdr/CdrSizeCalculator.hpp>
#include <fastdds/rtps/builtin/BuiltinProtocols.h>
#include <fastdds/rtps/builtin/data/ParticipantProxyData.h>
#include <fastdds/rtps/builtin/data/WriterProxyData.h>
#include <fastdds/rtps/builtin/data/ReaderProxyData.h>
#include <fastdds/rtps/writer/StatefulWriter.h>
#include <fastdds/rtps/writer/RTPSWriter.h>
#include <fastdds/rtps/reader/StatefulReader.h>
#include <fastdds/rtps/reader/RTPSReader.h>
#include <fastdds/rtps/history/WriterHistory.h>
#include <fastdds/rtps/history/ReaderHistory.h>
#include <fastdds/rtps/attributes/HistoryAttributes.h>
#include <fastdds/rtps/attributes/WriterAttributes.h>
#include <fastdds/rtps/attributes/ReaderAttributes.h>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/log/Log.hpp>
#include <rtps/participant/RTPSParticipantImpl.h>
#include <algorithm>

namespace eprosima {

using namespace fastrtps::rtps;
using eprosima::fastdds::dds::Log;

namespace fastdds {
namespace dds {
namespace builtin {

TypeLookupManager::TypeLookupManager(
        BuiltinProtocols* bprot)
    : participant_(nullptr)
    , builtin_protocols_(bprot)
    , builtin_request_writer_(nullptr)
    , builtin_request_reader_(nullptr)
    , builtin_reply_writer_(nullptr)
    , builtin_reply_reader_(nullptr)
    , builtin_request_writer_history_(nullptr)
    , builtin_reply_writer_history_(nullptr)
    , builtin_request_reader_history_(nullptr)
    , builtin_reply_reader_history_(nullptr)
    , request_listener_(nullptr)
    , reply_listener_(nullptr)
    , temp_reader_proxy_data_(
        bprot->mp_participantImpl->getRTPSParticipantAttributes().allocation.locators.max_unicast_locators,
        bprot->mp_participantImpl->getRTPSParticipantAttributes().allocation.locators.max_multicast_locators)
    , temp_writer_proxy_data_(
        bprot->mp_participantImpl->getRTPSParticipantAttributes().allocation.locators.max_unicast_locators,
        bprot->mp_participantImpl->getRTPSParticipantAttributes().allocation.locators.max_multicast_locators)
{
}

TypeLookupManager::~TypeLookupManager()
{
    if (nullptr != builtin_reply_reader_)
    {
        participant_->deleteUserEndpoint(builtin_reply_reader_->getGuid());
    }
    if (nullptr != builtin_reply_writer_)
    {
        participant_->deleteUserEndpoint(builtin_reply_writer_->getGuid());
    }
    if (nullptr != builtin_request_reader_)
    {
        participant_->deleteUserEndpoint(builtin_request_reader_->getGuid());
    }
    if (nullptr != builtin_request_writer_)
    {
        participant_->deleteUserEndpoint(builtin_request_writer_->getGuid());
    }
    delete builtin_request_writer_history_;
    delete builtin_reply_writer_history_;
    delete builtin_request_reader_history_;
    delete builtin_reply_reader_history_;

    delete reply_listener_;
    delete request_listener_;
}

bool TypeLookupManager::init_typelookup_service(
        RTPSParticipantImpl* pi)
{
    EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Initializing TypeLookup Service");
    participant_ = pi;

    std::stringstream ss;
    ss << participant_->getGuid();
    std::string str = ss.str();
    std::transform(str.begin(), str.end(), str.begin(),
            [](unsigned char c)
            {
                return static_cast<unsigned char>(std::tolower(c));
            });
    str.erase(std::remove(str.begin(), str.end(), '.'), str.end());
    instance_name_ = "dds.builtin.TOS." + str;

    bool retVal = create_endpoints();

    return retVal;
}

bool TypeLookupManager::assign_remote_endpoints(
        const ParticipantProxyData& ppd)
{
    const NetworkFactory& network = participant_->network_factory();
    uint32_t endp = ppd.m_availableBuiltinEndpoints;
    uint32_t auxendp = endp;

    std::lock_guard<std::mutex> data_guard(temp_proxy_data_lock_);

    temp_writer_proxy_data_.guid().guidPrefix = ppd.m_guid.guidPrefix;
    temp_writer_proxy_data_.persistence_guid().guidPrefix = ppd.m_guid.guidPrefix;
    temp_writer_proxy_data_.set_remote_locators(ppd.metatraffic_locators, network, true);
    temp_writer_proxy_data_.topicKind(NO_KEY);
    temp_writer_proxy_data_.m_qos.m_durability.kind = fastrtps::VOLATILE_DURABILITY_QOS;
    temp_writer_proxy_data_.m_qos.m_reliability.kind = fastrtps::RELIABLE_RELIABILITY_QOS;

    temp_reader_proxy_data_.clear();
    temp_reader_proxy_data_.m_expectsInlineQos = false;
    temp_reader_proxy_data_.guid().guidPrefix = ppd.m_guid.guidPrefix;
    temp_reader_proxy_data_.set_remote_locators(ppd.metatraffic_locators, network, true);
    temp_reader_proxy_data_.topicKind(NO_KEY);
    temp_reader_proxy_data_.m_qos.m_durability.kind = fastrtps::VOLATILE_DURABILITY_QOS;
    temp_reader_proxy_data_.m_qos.m_reliability.kind = fastrtps::RELIABLE_RELIABILITY_QOS;

    EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "for RTPSParticipant: " << ppd.m_guid);

    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REQUEST_DATA_WRITER;

    if (auxendp != 0 && builtin_request_reader_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Adding remote writer to the local Builtin Request Reader");
        temp_writer_proxy_data_.guid().entityId = fastrtps::rtps::c_EntityId_TypeLookup_request_writer;
        temp_writer_proxy_data_.persistence_guid().entityId = fastrtps::rtps::c_EntityId_TypeLookup_request_writer;
        builtin_request_reader_->matched_writer_add(temp_writer_proxy_data_);
    }

    auxendp = endp;
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REPLY_DATA_WRITER;

    if (auxendp != 0 && builtin_reply_reader_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Adding remote writer to the local Builtin Reply Reader");
        temp_writer_proxy_data_.guid().entityId = fastrtps::rtps::c_EntityId_TypeLookup_reply_writer;
        temp_writer_proxy_data_.persistence_guid().entityId = fastrtps::rtps::c_EntityId_TypeLookup_reply_writer;
        builtin_reply_reader_->matched_writer_add(temp_writer_proxy_data_);
    }

    auxendp = endp;
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REQUEST_DATA_READER;

    if (auxendp != 0 && builtin_request_writer_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Adding remote reader to the local Builtin Request Writer");
        temp_reader_proxy_data_.guid().entityId = fastrtps::rtps::c_EntityId_TypeLookup_request_reader;
        builtin_request_writer_->matched_reader_add(temp_reader_proxy_data_);
    }

    auxendp = endp;
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REPLY_DATA_READER;

    if (auxendp != 0 && builtin_reply_writer_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Adding remote reader to the local Builtin Reply Writer");
        temp_reader_proxy_data_.guid().entityId = fastrtps::rtps::c_EntityId_TypeLookup_reply_reader;
        builtin_reply_writer_->matched_reader_add(temp_reader_proxy_data_);
    }

    return true;
}

void TypeLookupManager::remove_remote_endpoints(
        fastrtps::rtps::ParticipantProxyData* ppd)
{
    GUID_t tmp_guid;
    tmp_guid.guidPrefix() = ppd->m_guid.guidPrefix;

    EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "for RTPSParticipant: " << ppd->m_guid);
    uint32_t endp = ppd->m_availableBuiltinEndpoints;
    uint32_t partdet = endp;
    uint32_t auxendp = endp;
    partdet &= DISC_BUILTIN_ENDPOINT_PARTICIPANT_DETECTOR; //Habria que quitar esta linea que comprueba si tiene PDP.
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REQUEST_DATA_WRITER;

    if ((auxendp != 0 || partdet != 0) && builtin_request_reader_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Removing remote writer from the local Builtin Request Reader");
        tmp_guid.entityId() = fastrtps::rtps::c_EntityId_TypeLookup_request_writer;
        builtin_request_reader_->matched_writer_remove(tmp_guid);
    }

    auxendp = endp;
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REPLY_DATA_WRITER;

    if ((auxendp != 0 || partdet != 0) && builtin_reply_reader_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Removing remote writer from the local Builtin Reply Reader");
        tmp_guid.entityId() = fastrtps::rtps::c_EntityId_TypeLookup_reply_writer;
        builtin_reply_reader_->matched_writer_remove(tmp_guid);
    }

    auxendp = endp;
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REQUEST_DATA_READER;

    if ((auxendp != 0 || partdet != 0) && builtin_request_writer_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Removing remote reader from the local Builtin Request Writer");
        tmp_guid.entityId() = fastrtps::rtps::c_EntityId_TypeLookup_request_reader;
        builtin_request_writer_->matched_reader_remove(tmp_guid);
    }

    auxendp = endp;
    auxendp &= BUILTIN_ENDPOINT_TYPELOOKUP_SERVICE_REPLY_DATA_READER;

    if ((auxendp != 0 || partdet != 0) && builtin_reply_writer_ != nullptr)
    {
        EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Removing remote reader from the local Builtin Reply Writer");
        tmp_guid.entityId() = fastrtps::rtps::c_EntityId_TypeLookup_reply_reader;
        builtin_reply_writer_->matched_reader_remove(tmp_guid);
    }
}

eprosima::fastrps::rtps::SampleIdentity TypeLookupManager::get_type_dependencies(
        const xtypes1_3::TypeIdentifierSeq& id_seq) const
{
    eprosima::fastrps::rtps::SampleIdentity id = INVALID_SAMPLE_IDENTITY;
    if (builtin_protocols_->m_att.typelookup_config.use_client)
    {
        TypeLookup_getTypeDependencies_In in;
        in.type_ids() = id_seq;
        TypeLookup_RequestPubSubType type;
        TypeLookup_Request* request = static_cast<TypeLookup_Request*>(type.createData());
        request->data().getTypeDependencies(in);

        if (send_request(*request))
        {
            id = request->header().requestId();
        }
        type.deleteData(request);
    }
    return id;
}

eprosima::fastrps::rtps::SampleIdentity TypeLookupManager::get_types(
        const xtypes1_3::TypeIdentifierSeq& id_seq) const
{
    eprosima::fastrps::rtps::SampleIdentity id = INVALID_SAMPLE_IDENTITY;
    if (builtin_protocols_->m_att.typelookup_config.use_client)
    {
        TypeLookup_getTypes_In in;
        in.type_ids() = id_seq;
        TypeLookup_RequestPubSubType type;
        TypeLookup_Request* request = static_cast<TypeLookup_Request*>(type.createData());
        request->data().getTypes(in);

        if (send_request(*request))
        {
            id = request->header().requestId();
        }
        type.deleteData(request);
    }
    return id;
}

bool TypeLookupManager::create_endpoints()
{
    const RTPSParticipantAttributes& pattr = participant_->getRTPSParticipantAttributes();

    // Built-in history attributes.
    HistoryAttributes hatt;
    hatt.initialReservedCaches = 20;
    hatt.maximumReservedCaches = 1000;
    hatt.payloadMaxSize = TYPELOOKUP_DATA_MAX_SIZE;

    WriterAttributes watt;
    watt.endpoint.unicastLocatorList = builtin_protocols_->m_metatrafficUnicastLocatorList;
    watt.endpoint.multicastLocatorList = builtin_protocols_->m_metatrafficMulticastLocatorList;
    watt.endpoint.external_unicast_locators = builtin_protocols_->m_att.metatraffic_external_unicast_locators;
    watt.endpoint.ignore_non_matching_locators = pattr.ignore_non_matching_locators;
    watt.endpoint.remoteLocatorList = builtin_protocols_->m_initialPeersList;
    watt.matched_readers_allocation = pattr.allocation.participants;
    watt.endpoint.topicKind = fastrtps::rtps::NO_KEY;
    watt.endpoint.reliabilityKind = fastrtps::rtps::RELIABLE;
    watt.endpoint.durabilityKind = fastrtps::rtps::VOLATILE;

    // Built-in request writer
    if (builtin_protocols_->m_att.typelookup_config.use_client)
    {
        builtin_request_writer_history_ = new WriterHistory(hatt);

        RTPSWriter* req_writer;
        if (participant_->createWriter(
                    &req_writer,
                    watt,
                    builtin_request_writer_history_,
                    nullptr,
                    fastrtps::rtps::c_EntityId_TypeLookup_request_writer,
                    true))
        {
            builtin_request_writer_ = dynamic_cast<StatefulWriter*>(req_writer);
            EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Builtin Typelookup request writer created.");
        }
        else
        {
            EPROSIMA_LOG_ERROR(TYPELOOKUP_SERVICE, "Typelookup request writer creation failed.");
            delete builtin_request_writer_history_;
            builtin_request_writer_history_ = nullptr;
            return false;
        }
    }

    // Built-in reply writer
    if (builtin_protocols_->m_att.typelookup_config.use_server)
    {
        builtin_reply_writer_history_ = new WriterHistory(hatt);

        RTPSWriter* rep_writer;
        if (participant_->createWriter(
                    &rep_writer,
                    watt,
                    builtin_reply_writer_history_,
                    nullptr,
                    fastrtps::rtps::c_EntityId_TypeLookup_reply_writer,
                    true))
        {
            builtin_reply_writer_ = dynamic_cast<StatefulWriter*>(rep_writer);
            EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Builtin Typelookup reply writer created.");
        }
        else
        {
            EPROSIMA_LOG_ERROR(TYPELOOKUP_SERVICE, "Typelookup reply writer creation failed.");
            delete builtin_reply_writer_history_;
            builtin_reply_writer_history_ = nullptr;
            return false;
        }
    }

    ReaderAttributes ratt;
    ratt.endpoint.unicastLocatorList = builtin_protocols_->m_metatrafficUnicastLocatorList;
    ratt.endpoint.multicastLocatorList = builtin_protocols_->m_metatrafficMulticastLocatorList;
    ratt.endpoint.external_unicast_locators = builtin_protocols_->m_att.metatraffic_external_unicast_locators;
    ratt.endpoint.ignore_non_matching_locators = pattr.ignore_non_matching_locators;
    ratt.endpoint.remoteLocatorList = builtin_protocols_->m_initialPeersList;
    ratt.matched_writers_allocation = pattr.allocation.participants;
    ratt.expectsInlineQos = true;
    ratt.endpoint.topicKind = fastrtps::rtps::NO_KEY;
    ratt.endpoint.reliabilityKind = fastrtps::rtps::RELIABLE;
    ratt.endpoint.durabilityKind = fastrtps::rtps::VOLATILE;

    // Built-in request reader
    if (builtin_protocols_->m_att.typelookup_config.use_server)
    {
        request_listener_ = new TypeLookupRequestListener(this);
        builtin_request_reader_history_ = new ReaderHistory(hatt);

        RTPSReader* req_reader;
        if (participant_->createReader(
                    &req_reader,
                    ratt,
                    builtin_request_reader_history_,
                    request_listener_,
                    fastrtps::rtps::c_EntityId_TypeLookup_request_reader,
                    true))
        {
            builtin_request_reader_ = dynamic_cast<StatefulReader*>(req_reader);
            EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Builtin Typelookup request reader created.");
        }
        else
        {
            EPROSIMA_LOG_ERROR(TYPELOOKUP_SERVICE, "Typelookup request reader creation failed.");
            delete builtin_request_reader_history_;
            builtin_request_reader_history_ = nullptr;
            delete request_listener_;
            request_listener_ = nullptr;
            return false;
        }
    }

    // Built-in reply reader
    if (builtin_protocols_->m_att.typelookup_config.use_client)
    {
        reply_listener_ = new TypeLookupReplyListener(this);
        builtin_reply_reader_history_ = new ReaderHistory(hatt);

        RTPSReader* rep_reader;
        if (participant_->createReader(
                    &rep_reader,
                    ratt,
                    builtin_reply_reader_history_,
                    reply_listener_,
                    fastrtps::rtps::c_EntityId_TypeLookup_reply_reader,
                    true))
        {
            builtin_reply_reader_ = dynamic_cast<StatefulReader*>(rep_reader);
            EPROSIMA_LOG_INFO(TYPELOOKUP_SERVICE, "Builtin Typelookup reply reader created.");
        }
        else
        {
            EPROSIMA_LOG_ERROR(TYPELOOKUP_SERVICE, "Typelookup reply reader creation failed.");
            delete builtin_reply_reader_history_;
            builtin_reply_reader_history_ = nullptr;
            delete reply_listener_;
            reply_listener_ = nullptr;
            return false;
        }
    }

    return true;
}

bool TypeLookupManager::send_request(
        TypeLookup_Request& req) const
{
    req.header().instanceName() = get_instanceName();
    req.header().requestId().writer_guid(builtin_request_writer_->getGuid());
    req.header().requestId().sequence_number(request_seq_number_);
    ++request_seq_number_;

    CacheChange_t* change = builtin_request_writer_->new_change(
        [&req]()
        {
            eprosima::fastcdr::CdrSizeCalculator calculator(eprosima::fastcdr::CdrVersion::XCDRv1);
            size_t current_alignment {0};
            return static_cast<uint32_t>(calculator.calculate_serialized_size(req, current_alignment) + 4);
        },
        ALIVE);

    if (change != nullptr)
    {
        CDRMessage_t msg(change->serializedPayload);

        bool valid = CDRMessage::addOctet(&msg, 0);
        change->serializedPayload.encapsulation = static_cast<uint16_t>(PL_DEFAULT_ENCAPSULATION);
        msg.msg_endian = DEFAULT_ENDIAN;
        valid &= CDRMessage::addOctet(&msg, PL_DEFAULT_ENCAPSULATION);
        valid &= CDRMessage::addUInt16(&msg, 0);

        change->serializedPayload.pos = msg.pos;
        change->serializedPayload.length = msg.length;

        SerializedPayload_t payload;
        payload.max_size = change->serializedPayload.max_size - 4;
        payload.data = change->serializedPayload.data + 4;
        if (valid && request_type_.serialize(&req, &payload, DataRepresentationId_t::XCDR2_DATA_REPRESENTATION))
        {
            change->serializedPayload.length += payload.length;
            change->serializedPayload.pos += payload.pos;
            payload.data = nullptr;
            return builtin_request_writer_history_->add_change(change);
        }
    }
    builtin_request_writer_history_->remove_change(change);
    return false;

}

bool TypeLookupManager::send_reply(
        TypeLookup_Reply& rep) const
{
    CacheChange_t* change = builtin_reply_writer_->new_change(
        [&rep]()
        {
            eprosima::fastcdr::CdrSizeCalculator calculator(eprosima::fastcdr::CdrVersion::XCDRv1);
            size_t current_alignment {0};
            return static_cast<uint32_t>(calculator.calculate_serialized_size(rep, current_alignment) + 4);
        },
        ALIVE);

    if (change != nullptr)
    {
        CDRMessage_t msg(change->serializedPayload);

        bool valid = CDRMessage::addOctet(&msg, 0);
        change->serializedPayload.encapsulation = static_cast<uint16_t>(PL_DEFAULT_ENCAPSULATION);
        msg.msg_endian = DEFAULT_ENDIAN;
        valid &= CDRMessage::addOctet(&msg, PL_DEFAULT_ENCAPSULATION);
        valid &= CDRMessage::addUInt16(&msg, 0);

        change->serializedPayload.pos = msg.pos;
        change->serializedPayload.length = msg.length;

        SerializedPayload_t payload;
        payload.max_size = change->serializedPayload.max_size - 4;
        payload.data = change->serializedPayload.data + 4;
        if (valid && reply_type_.serialize(&rep, &payload, DataRepresentationId_t::XCDR2_DATA_REPRESENTATION))
        {
            change->serializedPayload.length += payload.length;
            change->serializedPayload.pos += payload.pos;
            payload.data = nullptr;
            return builtin_reply_writer_history_->add_change(change);
        }
    }
    builtin_request_writer_history_->remove_change(change);
    return false;
}

bool TypeLookupManager::request_reception(
        fastrtps::rtps::CacheChange_t& change,
        TypeLookup_Request& req) const
{
    CDRMessage_t msg(change.serializedPayload);
    msg.pos += 1;
    octet encapsulation = 0;
    CDRMessage::readOctet(&msg, &encapsulation);
    if (encapsulation == PL_CDR_BE)
    {
        msg.msg_endian = BIGEND;
    }
    else if (encapsulation == PL_CDR_LE)
    {
        msg.msg_endian = LITTLEEND;
    }
    else
    {
        return false;
    }
    change.serializedPayload.encapsulation = static_cast<uint16_t>(encapsulation);
    msg.pos += 2; // Skip encapsulation options.

    SerializedPayload_t payload;
    payload.max_size = change.serializedPayload.max_size - 4;
    payload.length = change.serializedPayload.length - 4;
    payload.data = change.serializedPayload.data + 4;
    bool result = request_type_.deserialize(&payload, &req);
    payload.data = nullptr;
    return result;
}

bool TypeLookupManager::reply_reception(
        fastrtps::rtps::CacheChange_t& change,
        TypeLookup_Reply& rep) const
{
    CDRMessage_t msg(change.serializedPayload);
    msg.pos += 1;
    octet encapsulation = 0;
    CDRMessage::readOctet(&msg, &encapsulation);
    if (encapsulation == PL_CDR_BE)
    {
        msg.msg_endian = BIGEND;
    }
    else if (encapsulation == PL_CDR_LE)
    {
        msg.msg_endian = LITTLEEND;
    }
    else
    {
        return false;
    }
    change.serializedPayload.encapsulation = static_cast<uint16_t>(encapsulation);
    msg.pos += 2; // Skip encapsulation options.

    SerializedPayload_t payload;
    payload.max_size = change.serializedPayload.max_size - 4;
    payload.length = change.serializedPayload.length - 4;
    payload.data = change.serializedPayload.data + 4;
    bool result = reply_type_.deserialize(&payload, &rep);
    payload.data = nullptr;
    return result;
}

ReturnCode_t TypeLookupManager::get_registered_type_object(
    const TypeLookup_getTypes_In& in,
    TypeLookup_getTypes_Out& out)
{
    // Check if there is any EK_COMPLETE TypeIdentifiers in the request.
    bool request_has_complete_ids = false;
    for (const TypeIdentifier& type_id : in.type_ids())
    {
        if (type_id._d() == EK_COMPLETE)
        {
            request_has_complete_ids = true;
            break;
        }
    }

    for (const TypeIdentifier& type_id : in.type_ids())
    {
        // Ask the TypeObjectRegistry for the TypeObject of the current TypeIdentifier.
        TypeObjectPair objs;
        ReturnCode_t ret_code = DomainParticipantFactory::get_instance()->type_object_registry().get_type_object(type_id, objs);
        if(ret_code != RETCODE_OK)
        {
            //RETCODE_NO_DATA if the given TypeIdentifier is not found in the registry.
            continue;
        }
        // Create TypeIdentifierTypeObjectPair with the TypeObject registered for this TypeIdentifier.
        TypeIdentifierTypeObjectPair pair;
        pair.type_identifier(type_id);
        pair.type_object(objs.complete_type_object());
        out.types().push_back(std::move(pair));

        // If the request does not have any EK_COMPLETE TypeIdentifiers, fill the complete_to_minimal field.
        if(!request_has_complete_ids && GET_TYPES_REPLY_WITH_MINIMAL)
        {
        TypeObject complete_obj;
        ReturnCode_t ret_code = DomainParticipantFactory::get_instance()->type_object_registry().get_type_object(type_id, objs);


            TypeIdentifierPair pair;
            pair.type_identifier1(type_id);
            /air.type_identifier2(type_id);
            out.complete_to_minimal().push_back(std::move(pair));
        }
    }
    return RETCODE_OK;
}

ReturnCode_t TypeLookupManager::get_registered_type_dependencies(
    const TypeLookup_getTypeDependencies_In& in,
    TypeLookup_getTypeDependencies_Out& out)
{
    const size_t max_size = 255;
    TypeIdentfierWithSizeSeq result;    
    size_t continuation_point = fastrtps::types::to_size_t(in.continuation_point());
    size_t start_index = max_size * continuation_point;

    {
        std::lock_guard<std::mutex> lock(dependencies_requests_cache_mutex);
        // Check if the identifier is already in the cache
        auto [dependencies_requests_cache_it, new_inserted] = dependencies_requests_cache_.emplace(in.type_ids(), std::unordered_set<TypeIdentfierWithSize>{});

        if (new_inserted)
        {
            // If not in cache, query the registry and handle errors
            std::unordered_set<TypeIdentfierWithSize> full_type_dependencies;
            ReturnCode_t ret_code = DomainParticipantFactory::get_instance()->type_object_registry().get_type_dependencies(in.type_ids(), full_type_dependencies);
            if(ret_code == RETCODE_OK)
            {
                // Moving the retrieved data into the cache entry.
                dependencies_requests_cache_it->second = std::move(full_type_dependencies);
            }else{
                //RETCODE_NO_DATA if any given TypeIdentifier is unknown to the registry.
                //RETCODE_BAD_PARAMETER if any given TypeIdentifier is not a direct hash.
                return ret_code;
            }
        }

        // At this point, dependencies_requests_cache_ it is guaranteed to be valid
        const auto& dependencies = dependencies_requests_cache_it->second;
        auto dependencies_it = dependencies.begin();
        
        // Advance the iterator to the starting point, respecting the set boundaries
        for (size_t i = 0; i < start_index && dependencies_it != dependencies.end(); ++i)
        {
            ++dependencies_it;
        }

        bool reached_end = false;
        // Collect up to max_size dependencies
        for (size_t count = 0; dependencies_it != dependencies.end() && count < max_size; ++count)
        {
            out.dependent_typeids().push_back(*dependencies_it);

            // Advance the iterator and check if we've reached the end.
            ++dependencies_it;
            if(dependencies_it == dependencies.end())
            {
                reached_end = true;
            }
        }

        // If dependencies end reached, remove entry from the map.
        if (reached_end)
        {
            dependencies_requests_cache_.erase(in.type_ids());
        }
        // If max_size reached, increment out_continuation_point.
        else if (out.dependent_typeids().size() >= max_size) 
        {
            ++out.continuation_point();
        }
    }

    return RETCODE_OK;
}



std::string TypeLookupManager::get_instanceName() const
{
    return instance_name_;
}

RTPSParticipantImpl* TypeLookupManager::get_RTPS_participant()
{
    return participant_;
}

BuiltinProtocols* TypeLookupManager::get_builtin_protocols()
{
    return builtin_protocols_;
}

StatefulWriter* TypeLookupManager::get_builtin_request_writer()
{
    return builtin_request_writer_;
}

StatefulWriter* TypeLookupManager::get_builtin_reply_writer()
{
    return builtin_reply_writer_;
}

WriterHistory* TypeLookupManager::get_builtin_request_writer_history()
{
    return builtin_request_writer_history_;
}

WriterHistory* TypeLookupManager::get_builtin_reply_writer_history()
{
    return builtin_reply_writer_history_;
}

StatefulReader* TypeLookupManager::get_builtin_request_reader()
{
    return builtin_request_reader_;
}

StatefulReader* TypeLookupManager::get_builtin_reply_reader()
{
    return builtin_reply_reader_;
}

ReaderHistory* TypeLookupManager::get_builtin_request_reader_history()
{
    return builtin_request_reader_history_;
}

ReaderHistory* TypeLookupManager::get_builtin_reply_reader_history()
{
    return builtin_reply_reader_history_;
}

} // namespace builtin
} // namespace dds
} // namespace fastdds
} // namespace eprosima
