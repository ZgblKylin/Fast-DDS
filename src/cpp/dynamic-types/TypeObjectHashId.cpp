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

/*!
 * @file TypeObjectHashId.cpp
 * This source file contains the definition of the described types in the IDL file.
 *
 * This file was generated by the tool gen and modified manually.
 */

#include <fastrtps/types/TypeObjectHashId.h>

#include <fastcdr/Cdr.h>
#include <fastcdr/CdrSizeCalculator.hpp>

#include <fastcdr/exceptions/BadParamException.h>
using namespace eprosima::fastcdr::exception;

#include <utility>

namespace eprosima {
namespace fastrtps {

using namespace rtps;

namespace types {

TypeObjectHashId::TypeObjectHashId()
{
    m__d = EK_COMPLETE;
}

TypeObjectHashId::~TypeObjectHashId()
{
}

TypeObjectHashId::TypeObjectHashId(
        const TypeObjectHashId& x)
{
    m__d = x.m__d;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            memcpy(m_hash, x.m_hash, 14);
            break;
        default:
            break;
    }
}

TypeObjectHashId::TypeObjectHashId(
        TypeObjectHashId&& x)
{
    m__d = x.m__d;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            memcpy(m_hash, x.m_hash, 14);
            break;
        default:
            break;
    }
}

TypeObjectHashId& TypeObjectHashId::operator =(
        const TypeObjectHashId& x)
{
    m__d = x.m__d;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            memcpy(m_hash, x.m_hash, 14);
            break;
        default:
            break;
    }

    return *this;
}

TypeObjectHashId& TypeObjectHashId::operator =(
        TypeObjectHashId&& x)
{
    m__d = x.m__d;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            memcpy(m_hash, x.m_hash, 14);
            break;
        default:
            break;
    }

    return *this;
}

void TypeObjectHashId::_d(
        uint8_t __d)                   // Special case to ease... sets the current active member
{
    bool b = false;
    m__d = __d;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            switch (__d)
            {
                case EK_COMPLETE:
                case EK_MINIMAL:
                    b = true;
                    break;
                default:
                    break;
            }
            break;
    }

    if (!b)
    {
        throw BadParamException("Discriminator doesn't correspond with the selected union member");
    }

    m__d = __d;
}

uint8_t TypeObjectHashId::_d() const
{
    return m__d;
}

uint8_t& TypeObjectHashId::_d()
{
    return m__d;
}

void TypeObjectHashId::hash(
        const EquivalenceHash& _hash)
{
    memcpy(m_hash, _hash, 14);
    m__d = EK_COMPLETE;
}

void TypeObjectHashId::hash(
        EquivalenceHash&& _hash)
{
    memcpy(m_hash, _hash, 14);
    m__d = EK_COMPLETE;
}

const EquivalenceHash& TypeObjectHashId::hash() const
{
    bool b = false;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            b = true;
            break;
        default:
            break;
    }
    if (!b)
    {
        throw BadParamException("This member is not been selected");
    }


    return m_hash;
}

EquivalenceHash& TypeObjectHashId::hash()
{
    bool b = false;

    switch (m__d)
    {
        case EK_COMPLETE:
        case EK_MINIMAL:
            b = true;
            break;
        default:
            break;
    }
    if (!b)
    {
        throw BadParamException("This member is not been selected");
    }


    return m_hash;
}

} // namespace types
} // namespace fastrtps
} // namespace eprosima

namespace eprosima {
namespace fastcdr {
template<>
size_t calculate_serialized_size(
        eprosima::fastcdr::CdrSizeCalculator& calculator,
        const eprosima::fastrtps::types::TypeObjectHashId& data,
        size_t& current_alignment)
{
    size_t calculated_size {calculator.begin_calculate_type_serialized_size(
                                eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR2, current_alignment)};

    calculated_size += calculator.calculate_member_serialized_size(eprosima::fastcdr::MemberId(
                        0), data._d(), current_alignment);

    switch (data._d())
    {
        case eprosima::fastrtps::types::EK_COMPLETE:
        case eprosima::fastrtps::types::EK_MINIMAL:
            calculated_size += ((14) * 1) + eprosima::fastcdr::Cdr::alignment(current_alignment, 1); break;
        default:
            break;
    }

    calculated_size += calculator.end_calculate_type_serialized_size(
        eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR2, current_alignment);

    return calculated_size;
}

template<>
void serialize(
        eprosima::fastcdr::Cdr& scdr,
        const eprosima::fastrtps::types::TypeObjectHashId& data)
{
    scdr << data._d();

    switch (data._d())
    {
        case eprosima::fastrtps::types::EK_COMPLETE:
        case eprosima::fastrtps::types::EK_MINIMAL:
            for (int i = 0; i < 14; ++i)
            {
                scdr << data.hash()[i];
            }
            break;
        default:
            break;
    }
}

template<>
void deserialize(
        eprosima::fastcdr::Cdr& dcdr,
        eprosima::fastrtps::types::TypeObjectHashId& data)
{
    dcdr >> data._d();

    switch (data._d())
    {
        case eprosima::fastrtps::types::EK_COMPLETE:
        case eprosima::fastrtps::types::EK_MINIMAL:
            for (int i = 0; i < 14; ++i)
            {
                dcdr >> data.hash()[i];
            }
            break;
        default:
            break;
    }
}

} // namespace fastcdr
} // namespace eprosima
