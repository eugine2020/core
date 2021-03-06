/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <sal/config.h>

#include <com/sun/star/container/NoSuchElementException.hpp>
#include <rtl/ustring.hxx>
#include <tools/inetmime.hxx>

#include "mcnttype.hxx"

using namespace com::sun::star::uno;
using namespace com::sun::star::container;
using namespace std;
using namespace osl;

CMimeContentType::CMimeContentType( const OUString& aCntType )
{
    init( aCntType );
}

OUString SAL_CALL CMimeContentType::getMediaType( )
{
    return m_MediaType;
}

OUString SAL_CALL CMimeContentType::getMediaSubtype( )
{
    return m_MediaSubtype;
}

OUString SAL_CALL CMimeContentType::getFullMediaType( )
{
    return m_MediaType + "/" + m_MediaSubtype;
}

Sequence< OUString > SAL_CALL CMimeContentType::getParameters( )
{
    MutexGuard aGuard( m_aMutex );

    Sequence< OUString > seqParams;

    map< OUString, OUString >::iterator iter;
    map< OUString, OUString >::iterator iter_end = m_ParameterMap.end( );

    for ( iter = m_ParameterMap.begin( ); iter != iter_end; ++iter )
    {
        seqParams.realloc( seqParams.getLength( ) + 1 );
        seqParams[seqParams.getLength( ) - 1] = iter->first;
    }

    return seqParams;
}

sal_Bool SAL_CALL CMimeContentType::hasParameter( const OUString& aName )
{
    MutexGuard aGuard( m_aMutex );
    return ( m_ParameterMap.end( ) != m_ParameterMap.find( aName.toAsciiLowerCase() ) );
}

OUString SAL_CALL CMimeContentType::getParameterValue( const OUString& aName )
{
    auto const lower = aName.toAsciiLowerCase();

    MutexGuard aGuard( m_aMutex );

    if ( !hasParameter( lower ) )
        throw NoSuchElementException( );

    return m_ParameterMap.find( lower )->second;
}

void CMimeContentType::init( const OUString& aCntType )
{
    INetContentTypeParameterList params;
    if (INetMIME::scanContentType(aCntType, &m_MediaType, &m_MediaSubtype, &params)
        != aCntType.getStr() + aCntType.getLength())
    {
        throw css::lang::IllegalArgumentException(
            "illegal media type " + aCntType, css::uno::Reference<css::uno::XInterface>(), -1);
    }
    for (auto const & i: params) {
        if (!i.second.m_bConverted) {
            throw css::lang::IllegalArgumentException(
                "illegal parameter value in media type " + aCntType,
                css::uno::Reference<css::uno::XInterface>(), -1);
        }
        m_ParameterMap[OUString::fromUtf8(i.first)] = i.second.m_sValue;
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
