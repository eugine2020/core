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

#include <comphelper/string.hxx>
#include "createparser.hxx"
#include "utils.hxx"
#include <com/sun/star/sdbc/DataType.hpp>

using namespace ::comphelper;
using namespace css::sdbc;

namespace
{
/// Returns substring of sSql from the first occurrence of '(' until the
/// last occurrence of ')' (excluding the parenthesis)
OUString lcl_getColumnPart(const OUString& sSql)
{
    sal_Int32 nBeginIndex = sSql.indexOf("(") + 1;
    if (nBeginIndex < 0)
    {
        SAL_WARN("dbaccess", "No column definitions found");
        return OUString();
    }
    sal_Int32 nCount = sSql.lastIndexOf(")") - nBeginIndex;
    return sSql.copy(nBeginIndex, nCount);
}

/// Constructs a vector of strings that represents the definitions of each
/// column or constraint.
///
/// @param sColumnPart part of the create statement inside the parenthesis
/// containing the column definitions
std::vector<OUString> lcl_splitColumnPart(const OUString& sColumnPart)
{
    std::vector<OUString> sParts = string::split(sColumnPart, sal_Unicode(u','));
    std::vector<OUString> sReturn;

    OUStringBuffer current;
    for (auto const& part : sParts)
    {
        current.append(part);
        if (current.lastIndexOf("(") > current.lastIndexOf(")"))
            current.append(","); // it was false split
        else
            sReturn.push_back(current.makeStringAndClear());
    }
    return sReturn;
}

sal_Int32 lcl_getAutoIncrementDefault(const OUString& sColumnDef)
{
    // TODO what if there are more spaces?
    if (sColumnDef.indexOf("GENERATED BY DEFAULT AS IDENTITY") > 0)
    {
        // TODO parse starting sequence stated by "START WITH"
        return 0;
    }
    return -1;
}

bool lcl_isNullable(const OUString& sColumnDef)
{
    if (sColumnDef.indexOf("NOT NULL") >= 0)
    {
        return false;
    }
    return true;
}

bool lcl_isPrimaryKey(const OUString& sColumnDef)
{
    if (sColumnDef.indexOf("PRIMARY KEY") >= 0)
    {
        return true;
    }
    return false;
}

sal_Int32 lcl_getDataTypeFromHsql(const OUString& sTypeName)
{
    if (sTypeName == "CHAR")
        return DataType::CHAR;
    else if (sTypeName == "VARCHAR" || sTypeName == "VARCHAR_IGNORECASE")
        return DataType::VARCHAR;
    else if (sTypeName == "TINYINT")
        return DataType::TINYINT;
    else if (sTypeName == "SMALLINT")
        return DataType::SMALLINT;
    else if (sTypeName == "INTEGER")
        return DataType::INTEGER;
    else if (sTypeName == "BIGINT")
        return DataType::BIGINT;
    else if (sTypeName == "NUMERIC")
        return DataType::NUMERIC;
    else if (sTypeName == "DECIMAL")
        return DataType::DECIMAL;
    else if (sTypeName == "BOOLEAN")
        return DataType::BOOLEAN;
    else if (sTypeName == "LONGVARCHAR")
        return DataType::LONGVARCHAR;
    else if (sTypeName == "LONGVARBINARY")
        return DataType::LONGVARBINARY;
    else if (sTypeName == "CLOB")
        return DataType::CLOB;
    else if (sTypeName == "BLOB")
        return DataType::BLOB;
    else if (sTypeName == "BINARY")
        return DataType::BINARY;
    else if (sTypeName == "VARBINARY")
        return DataType::VARBINARY;
    else if (sTypeName == "DATE")
        return DataType::DATE;
    else if (sTypeName == "TIME")
        return DataType::TIME;
    else if (sTypeName == "TIMESTAMP")
        return DataType::TIMESTAMP;
    else if (sTypeName == "DOUBLE")
        return DataType::DOUBLE;
    else if (sTypeName == "REAL")
        return DataType::REAL;
    else if (sTypeName == "FLOAT")
        return DataType::FLOAT;

    assert(false);
    return -1;
}

void lcl_addDefaultParameters(std::vector<sal_Int32>& aParams, sal_Int32 eType)
{
    if (eType == DataType::CHAR || eType == DataType::BINARY || eType == DataType::VARBINARY
        || eType == DataType::VARCHAR)
        aParams.push_back(8000); // from SQL standard
}

} // unnamed namespace

namespace dbahsql
{
CreateStmtParser::CreateStmtParser() {}

void CreateStmtParser::parsePrimaryKeys(const OUString& sPrimaryPart)
{
    sal_Int32 nParenPos = sPrimaryPart.indexOf("(");
    if (nParenPos > 0)
    {
        OUString sParamStr
            = sPrimaryPart.copy(nParenPos + 1, sPrimaryPart.lastIndexOf(")") - nParenPos - 1);
        auto sParams = string::split(sParamStr, sal_Unicode(u','));
        for (auto& sParam : sParams)
        {
            m_PrimaryKeys.push_back(sParam);
        }
    }
}

void CreateStmtParser::parseColumnPart(const OUString& sColumnPart)
{
    auto sColumns = lcl_splitColumnPart(sColumnPart);
    for (OUString& sColumn : sColumns)
    {
        if (sColumn.startsWithIgnoreAsciiCase("PRIMARY KEY"))
        {
            parsePrimaryKeys(sColumn);
            continue;
        }

        if (sColumn.startsWithIgnoreAsciiCase("CONSTRAINT"))
        {
            m_aForeignParts.push_back(sColumn);
            continue;
        }

        bool bIsQuoteUsedForColumnName(sColumn[0] == '\"');
        // find next quote after the initial quote
        // or next space if quote isn't used as delimiter
        // to fetch the whole column name, including quotes
        auto nEndColumnName
            = bIsQuoteUsedForColumnName ? sColumn.indexOf("\"", 1) : sColumn.indexOf(" ");
        const OUString& rColumnName
            = sColumn.copy(0, bIsQuoteUsedForColumnName ? nEndColumnName + 1 : nEndColumnName);

        // create a buffer which begins on column type
        // with extra spaces removed
        const OUString& buffer
            = sColumn.copy(bIsQuoteUsedForColumnName ? nEndColumnName + 1 : nEndColumnName).trim();

        // Now let's manage the column type
        // search next space to get the whole type name
        // eg: INTEGER, VARCHAR(10), DECIMAL(6,3)
        auto nNextSpace = buffer.indexOf(" ");
        OUString sFullTypeName;
        OUString sTypeName;
        if (nNextSpace > 0)
            sFullTypeName = buffer.copy(0, nNextSpace);
        // perhaps column type corresponds to the last info here
        else
            sFullTypeName = buffer;

        auto nParenPos = sFullTypeName.indexOf("(");
        std::vector<sal_Int32> aParams;
        if (nParenPos > 0)
        {
            sTypeName = sFullTypeName.copy(0, nParenPos).trim();
            OUString sParamStr
                = sFullTypeName.copy(nParenPos + 1, sFullTypeName.indexOf(")") - nParenPos - 1);
            auto sParams = string::split(sParamStr, sal_Unicode(u','));
            for (auto& sParam : sParams)
            {
                aParams.push_back(sParam.toInt32());
            }
        }
        else
        {
            sTypeName = sFullTypeName.trim();
            lcl_addDefaultParameters(aParams, lcl_getDataTypeFromHsql(sTypeName));
        }

        bool bCaseInsensitive = sTypeName.indexOf("IGNORECASE") >= 0;
        bool isPrimaryKey = lcl_isPrimaryKey(sColumn);

        if (isPrimaryKey)
            m_PrimaryKeys.push_back(rColumnName);

        ColumnDefinition aColDef(rColumnName, lcl_getDataTypeFromHsql(sTypeName), aParams,
                                 isPrimaryKey, lcl_getAutoIncrementDefault(sColumn),
                                 lcl_isNullable(sColumn), bCaseInsensitive);

        m_aColumns.push_back(aColDef);
    }
}

void CreateStmtParser::parse(const OUString& sSql)
{
    // TODO Foreign keys
    if (!sSql.startsWith("CREATE"))
    {
        SAL_WARN("dbaccess", "Not a create statement");
        return;
    }

    m_sTableName = utils::getTableNameFromStmt(sSql);
    OUString sColumnPart = lcl_getColumnPart(sSql);
    parseColumnPart(sColumnPart);
}

} // namespace dbahsql

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
