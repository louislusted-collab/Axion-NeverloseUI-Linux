
#include "schemav2.hpp"
#include "../sdk/interfaces/ischemasystem.h"
#include "../utilities/fnv1a.h"
#include "../utilities/log.h"

std::optional<int32_t> CSchemaManager::GetSchemaOffsetInternal(const char* moduleName, const char* bindingName, const char* fieldName) {
    if (I::SchemaSystem == nullptr || moduleName == nullptr || bindingName == nullptr || fieldName == nullptr) {
        L_PRINT(LOG_ERROR) << CS_XOR("schema lookup rejected invalid input");
        return {};
    }
    CSchemaSystemTypeScope* typeScope = I::SchemaSystem->FindTypeScopeForModule(moduleName);
    if (!typeScope) {
        L_PRINT(LOG_ERROR) << CS_XOR("schema type scope not found for ") << moduleName;
        return {};
    }
    SchemaClassInfoData_t* classInfo = nullptr;

    typeScope->FindDeclaredClass(&classInfo, bindingName);
    if (!classInfo) {
        L_PRINT(LOG_INFO) << CS_XOR("schema binding not found: ") << bindingName
            << CS_XOR(" in ") << moduleName;

        return {};
    }

    uint32_t fieldHash = FNV1A::Hash(fieldName);
    for (int i = 0; classInfo->pFields && i < classInfo->nFieldSize; ++i) {
        auto& field = classInfo->pFields[i];
        if (field.szName != nullptr && FNV1A::Hash(field.szName) == fieldHash) {
            L_PRINT(LOG_INFO) << CS_XOR("schema offset resolved: ") << bindingName
                << CS_XOR("->") << fieldName;

            return field.nSingleInheritanceOffset;
        }
    }
    L_PRINT(LOG_ERROR) << CS_XOR("schema field not found: ") << bindingName
        << CS_XOR("->") << fieldName;

    return {};
}
