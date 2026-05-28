#include "ncbind.hpp"
#include <spdlog/spdlog.h>

// The actual features of PackinOne.dll (fstat, dirlist, addFont, saveStruct, getMD5HashString)
// are already completely implemented in C++ across fstat/main.cpp, addFont.cpp, saveStruct.cpp, etc.
// They are registered globally at engine startup via TVPLoadInternalPlugins().
// We simply need to register the "packinone.dll" module identifier to satisfy the KAG script's linkage check.

class PackinOneDummy {
public:
    static void Stub() {
        // Dummy method
    }
};

class CompoundStorageMedia {
public:
    CompoundStorageMedia() = default;

    ttstr getArchiveUniqueKey() const {
        return TJS_W("AetherKiri.CompoundStorageMedia");
    }

    static tjs_error ok(tTJSVariant *result, tjs_int, tTJSVariant **,
                        CompoundStorageMedia *) {
        if(result)
            *result = static_cast<tjs_int>(1);
        return TJS_S_OK;
    }

    static tjs_error emptyString(tTJSVariant *result, tjs_int, tTJSVariant **,
                                 CompoundStorageMedia *) {
        if(result)
            *result = TJS_W("");
        return TJS_S_OK;
    }
};

#define NCB_MODULE_NAME TJS_W("packinone.dll")
NCB_REGISTER_CLASS(PackinOneDummy) {
    NCB_METHOD(Stub);
}

NCB_REGISTER_CLASS(CompoundStorageMedia) {
    Constructor();
    NCB_METHOD_RAW_CALLBACK(addArchive, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(addStorage, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(addAutoToolsPath, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(setCurrentDirectory, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(register, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(unregister, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(parseArchiveIndex, &CompoundStorageMedia::ok, 0);
    NCB_METHOD_RAW_CALLBACK(getLocallyAccessibleName,
                            &CompoundStorageMedia::emptyString, 0);
    NCB_PROPERTY_RO(archiveUniqueKey, getArchiveUniqueKey);
}
