#include "PluginStub.h"
#include "StorageIntf.h"
#include "ncbind.hpp"
#include "sqlite/sqlite3.h"

#include <string>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("sqlite3.dll")

namespace {

std::string toUtf8(const tjs_char *text) {
    const tjs_int length = TVPWideCharToUtf8String(text, nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    if(length > 0)
        TVPWideCharToUtf8String(text, out.data());
    return out;
}

int bindParam(sqlite3_stmt *stmt, const tTJSVariant &param, int pos) {
    switch(param.Type()) {
        case tvtInteger:
            return sqlite3_bind_int64(stmt, pos, param.AsInteger());
        case tvtReal:
            return sqlite3_bind_double(stmt, pos, param.AsReal());
        case tvtString: {
            tTJSVariantString *str = param.AsStringNoAddRef();
            return sqlite3_bind_text16(stmt, pos, *str,
                                       str->GetLength() * sizeof(tjs_char),
                                       SQLITE_TRANSIENT);
        }
        case tvtOctet: {
            tTJSVariantOctet *octet = param.AsOctetNoAddRef();
            return sqlite3_bind_blob(stmt, pos, octet->GetData(),
                                     octet->GetLength(), SQLITE_TRANSIENT);
        }
        default:
            return sqlite3_bind_null(stmt, pos);
    }
}

int getBindPos(sqlite3_stmt *stmt, const tTJSVariant &name) {
    switch(name.Type()) {
        case tvtInteger:
        case tvtReal:
            return static_cast<int>(name.AsInteger()) + 1;
        case tvtString:
            return sqlite3_bind_parameter_index(stmt,
                                                toUtf8(name.GetString()).c_str());
        default:
            return 0;
    }
}

class BindCaller : public tTJSDispatch {
public:
    explicit BindCaller(sqlite3_stmt *stmt) : stmt_(stmt) {}

    int errorCode() const { return errorCode_; }

    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char *,
                                       tjs_uint32 *, tTJSVariant *result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *) override {
        if(numparams > 1) {
            const tTVInteger flag = param[1]->AsInteger();
            if(!(flag & TJS_HIDDENMEMBER))
                errorCode_ = bindParam(stmt_, *param[1],
                                       getBindPos(stmt_, *param[0]));
        }
        if(result)
            *result = errorCode_ == SQLITE_OK;
        return TJS_S_OK;
    }

private:
    sqlite3_stmt *stmt_;
    int errorCode_ = SQLITE_OK;
};

int bindParams(sqlite3_stmt *stmt, const tTJSVariant &params) {
    if(params.Type() == tvtVoid)
        return SQLITE_OK;

    tTJSVariantClosure vc = params.AsObjectClosureNoAddRef();
    if(vc.IsInstanceOf(TJS_IGNOREPROP, nullptr, nullptr, TJS_W("Array"),
                       nullptr) == TJS_S_TRUE) {
        int ret = SQLITE_OK;
        tTJSVariant countValue;
        vc.PropGet(0, TJS_W("count"), nullptr, &countValue, nullptr);
        const int count = static_cast<int>(countValue.AsInteger());
        for(int i = 0; i < count; ++i) {
            tTJSVariant value;
            vc.PropGetByNum(0, i, &value, nullptr);
            ret = bindParam(stmt, value, i + 1);
            if(ret != SQLITE_OK)
                break;
        }
        return ret;
    }

    auto *caller = new BindCaller(stmt);
    tTJSVariantClosure closure(caller);
    vc.EnumMembers(TJS_IGNOREPROP, &closure, nullptr);
    const int errorCode = caller->errorCode();
    caller->Release();
    return errorCode;
}

void getColumnData(sqlite3_stmt *stmt, tTJSVariant &variant, int column) {
    switch(sqlite3_column_type(stmt, column)) {
        case SQLITE_INTEGER:
            variant = static_cast<tTVInteger>(sqlite3_column_int64(stmt, column));
            break;
        case SQLITE_FLOAT:
            variant = sqlite3_column_double(stmt, column);
            break;
        case SQLITE_TEXT:
            variant =
                reinterpret_cast<const tjs_char *>(sqlite3_column_text16(stmt,
                                                                          column));
            break;
        case SQLITE_BLOB:
            variant = tTJSVariant(
                static_cast<const tjs_uint8 *>(sqlite3_column_blob(stmt, column)),
                sqlite3_column_bytes(stmt, column));
            break;
        default:
            variant.Clear();
            break;
    }
}

ttstr localDatabaseName(const tjs_char *database) {
    if(!database || *database == 0 || *database == TJS_W(':'))
        return ttstr(database ? database : TJS_W(""));

    ttstr filename = TVPNormalizeStorageName(ttstr(database));
    ttstr local = TVPGetLocallyAccessibleName(filename);
    if(filename.length() && local.length())
        return local;

    TVPThrowExceptionMessage(
        (ttstr(TJS_W("Unable to open the database file: ")) + filename).c_str());
    return ttstr();
}

} // namespace

class Sqlite {
public:
    Sqlite(const tjs_char *database, bool readonly = false) {
        const ttstr name = localDatabaseName(database);
        const int flags = readonly ? SQLITE_OPEN_READONLY
                                   : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        int ret = sqlite3_open_v2(toUtf8(name.c_str()).c_str(), &db_, flags,
                                  nullptr);
        if(ret != SQLITE_OK && db_) {
            errorCode_ = ret;
            errorMessage_ =
                ttstr(reinterpret_cast<const tjs_char *>(sqlite3_errmsg16(db_)));
        }
    }

    ~Sqlite() {
        if(db_)
            sqlite3_close(db_);
    }

    sqlite3 *database() const { return db_; }

    static tjs_error TJS_INTF_METHOD exec(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **params, Sqlite *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare16_v2(self->db_, params[0]->GetString(), -1,
                                       &stmt, nullptr);
        if(ret == SQLITE_OK) {
            if(numparams <= 1 || (ret = bindParams(stmt, *params[1])) ==
                                     SQLITE_OK) {
                if(numparams > 2 && params[2]->Type() == tvtObject) {
                    tTJSVariantClosure callback =
                        params[2]->AsObjectClosureNoAddRef();
                    const int argc = sqlite3_column_count(stmt);
                    while((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
                        std::vector<tTJSVariant> values(argc);
                        std::vector<tTJSVariant *> args(argc);
                        for(int i = 0; i < argc; ++i) {
                            getColumnData(stmt, values[i], i);
                            args[i] = &values[i];
                        }
                        callback.FuncCall(0, nullptr, nullptr, nullptr, argc,
                                          args.data(), nullptr);
                    }
                } else {
                    while((ret = sqlite3_step(stmt)) == SQLITE_ROW) {}
                }
            }
            sqlite3_finalize(stmt);
        }

        self->captureError(ret);
        if(result)
            *result = ret == SQLITE_OK || ret == SQLITE_DONE;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD execValue(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **params,
                                               Sqlite *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare16_v2(self->db_, params[0]->GetString(), -1,
                                       &stmt, nullptr);
        if(ret == SQLITE_OK) {
            if(numparams <= 1 || (ret = bindParams(stmt, *params[1])) ==
                                     SQLITE_OK) {
                ret = sqlite3_step(stmt);
                if(ret == SQLITE_ROW && sqlite3_column_count(stmt) > 0 &&
                   result)
                    getColumnData(stmt, *result, 0);
            }
            sqlite3_finalize(stmt);
        }
        self->captureError(ret);
        return TJS_S_OK;
    }

    bool begin() { return execSimple("BEGIN TRANSACTION;"); }
    bool commit() { return execSimple("COMMIT;"); }
    bool rollback() { return execSimple("ROLLBACK;"); }

    tjs_int64 getLastInsertRowId() const {
        return db_ ? sqlite3_last_insert_rowid(db_) : 0;
    }

    int getErrorCode() const { return errorCode_; }
    ttstr getErrorMessage() const { return errorMessage_; }

    static tjs_error TJS_INTF_METHOD factory(Sqlite **result, tjs_int numparams,
                                             tTJSVariant **params,
                                             iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        *result =
            new Sqlite(params[0]->GetString(),
                       numparams > 1 && params[1]->AsInteger() != 0);
        return TJS_S_OK;
    }

private:
    bool execSimple(const char *sql) {
        const int ret = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
        captureError(ret);
        return ret == SQLITE_OK;
    }

    void captureError(int code) {
        errorCode_ = code;
        if(db_) {
            const void *message = sqlite3_errmsg16(db_);
            errorMessage_ = message
                                ? ttstr(reinterpret_cast<const tjs_char *>(
                                      message))
                                : ttstr();
        } else {
            errorMessage_ = TJS_W("database open failed");
        }
    }

    sqlite3 *db_ = nullptr;
    int errorCode_ = SQLITE_OK;
    ttstr errorMessage_;
};

#define SQLITE_ENUM(n) Variant(TJS_W(#n), static_cast<int>(n))

NCB_REGISTER_CLASS(Sqlite) {
    SQLITE_ENUM(SQLITE_OK);
    SQLITE_ENUM(SQLITE_ERROR);
    SQLITE_ENUM(SQLITE_INTERNAL);
    SQLITE_ENUM(SQLITE_PERM);
    SQLITE_ENUM(SQLITE_ABORT);
    SQLITE_ENUM(SQLITE_BUSY);
    SQLITE_ENUM(SQLITE_LOCKED);
    SQLITE_ENUM(SQLITE_NOMEM);
    SQLITE_ENUM(SQLITE_READONLY);
    SQLITE_ENUM(SQLITE_INTERRUPT);
    SQLITE_ENUM(SQLITE_IOERR);
    SQLITE_ENUM(SQLITE_CORRUPT);
    SQLITE_ENUM(SQLITE_NOTFOUND);
    SQLITE_ENUM(SQLITE_FULL);
    SQLITE_ENUM(SQLITE_CANTOPEN);
    SQLITE_ENUM(SQLITE_PROTOCOL);
    SQLITE_ENUM(SQLITE_EMPTY);
    SQLITE_ENUM(SQLITE_SCHEMA);
    SQLITE_ENUM(SQLITE_TOOBIG);
    SQLITE_ENUM(SQLITE_CONSTRAINT);
    SQLITE_ENUM(SQLITE_MISMATCH);
    SQLITE_ENUM(SQLITE_MISUSE);
    SQLITE_ENUM(SQLITE_NOLFS);
    SQLITE_ENUM(SQLITE_AUTH);
    SQLITE_ENUM(SQLITE_FORMAT);
    SQLITE_ENUM(SQLITE_RANGE);
    SQLITE_ENUM(SQLITE_NOTADB);
    SQLITE_ENUM(SQLITE_ROW);
    SQLITE_ENUM(SQLITE_DONE);
    Factory(&ClassT::factory);
    RawCallback(TJS_W("exec"), &Class::exec, 0);
    RawCallback(TJS_W("execValue"), &Class::execValue, 0);
    NCB_METHOD(begin);
    NCB_METHOD(commit);
    NCB_METHOD(rollback);
    NCB_PROPERTY_RO(lastInsertRowId, getLastInsertRowId);
    NCB_PROPERTY_RO(errorCode, getErrorCode);
    NCB_PROPERTY_RO(errorMessage, getErrorMessage);
}

class SqliteStatement {
public:
    explicit SqliteStatement(tTJSVariant &sqlite) : sqlite_(sqlite) {
        Sqlite *sq =
            ncbInstanceAdaptor<Sqlite>::GetNativeInstance(sqlite.AsObjectNoAddRef());
        if(sq)
            db_ = sq->database();
    }

    ~SqliteStatement() { close(); }

    static tjs_error TJS_INTF_METHOD factory(SqliteStatement **result,
                                             tjs_int numparams,
                                             tTJSVariant **params,
                                             iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(params[0]->AsObjectClosureNoAddRef().IsInstanceOf(
               0, nullptr, nullptr, TJS_W("Sqlite"), nullptr) != TJS_S_TRUE)
            TVPThrowExceptionMessage(TJS_W("use Sqlite class Object"));

        auto *state = new SqliteStatement(*params[0]);
        if(numparams > 1 &&
           state->_open(params[1]->GetString(),
                        numparams > 2 ? params[2] : nullptr) != SQLITE_OK) {
            delete state;
            TVPThrowExceptionMessage(TJS_W("failed to open state"));
        }
        tTJSVariant name(TJS_W("missing"));
        objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &name);
        *result = state;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **params,
                                          SqliteStatement *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const int ret = self->_open(params[0]->GetString(),
                                    numparams > 1 ? params[1] : nullptr);
        if(result)
            *result = ret;
        return TJS_S_OK;
    }

    void close() {
        if(stmt_) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    ttstr getSql() const {
        if(!stmt_)
            return ttstr();
        const char *sql = sqlite3_sql(stmt_);
        if(!sql)
            return ttstr();
        tjs_int length = TVPUtf8ToWideCharString(sql, nullptr);
        std::vector<tjs_char> wide(static_cast<size_t>(length) + 1, 0);
        if(length > 0)
            TVPUtf8ToWideCharString(sql, wide.data());
        return ttstr(wide.data());
    }

    int reset() {
        bindPos_ = 1;
        return stmt_ ? sqlite3_reset(stmt_) : SQLITE_MISUSE;
    }

    int bind(tTJSVariant params) { return bindParams(stmt_, params); }

    static tjs_error TJS_INTF_METHOD bindAt(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **params,
                                            SqliteStatement *self) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const int ret =
            self->_bindAt(*params[0], numparams > 1 ? params[1] : nullptr);
        if(result)
            *result = ret;
        return TJS_S_OK;
    }

    int exec() {
        const int ret = stmt_ ? sqlite3_step(stmt_) : SQLITE_MISUSE;
        if(ret != SQLITE_ROW)
            reset();
        return ret;
    }

    bool step() {
        if(stmt_ && sqlite3_step(stmt_) == SQLITE_ROW)
            return true;
        reset();
        return false;
    }

    int getCount() const { return stmt_ ? sqlite3_data_count(stmt_) : 0; }

    int getColumnCount() const {
        return stmt_ ? sqlite3_column_count(stmt_) : 0;
    }

    bool isNull(tTJSVariant column) const {
        return sqlite3_column_type(stmt_, getColumnNo(column)) == SQLITE_NULL;
    }

    int getType(tTJSVariant column) const {
        return sqlite3_column_type(stmt_, getColumnNo(column));
    }

    ttstr getName(tTJSVariant column) const {
        const void *name = sqlite3_column_name16(stmt_, getColumnNo(column));
        return name ? ttstr(reinterpret_cast<const tjs_char *>(name)) : ttstr();
    }

    static tjs_error TJS_INTF_METHOD get(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **params,
                                         SqliteStatement *self) {
        if(!result)
            return TJS_S_OK;

        if(numparams == 0) {
            const int count = sqlite3_column_count(self->stmt_);
            iTJSDispatch2 *line = TJSCreateArrayObject();
            for(int i = 0; i < count; ++i) {
                tTJSVariant value;
                getColumnData(self->stmt_, value, i);
                tTJSVariant *argv[] = { &value };
                line->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, argv, line);
            }
            *result = tTJSVariant(line, line);
            line->Release();
            return TJS_S_OK;
        }

        const int col = self->getColumnNo(*params[0]);
        if(sqlite3_column_type(self->stmt_, col) == SQLITE_NULL) {
            if(numparams > 1)
                *result = *params[1];
            else
                result->Clear();
        } else {
            getColumnData(self->stmt_, *result, col);
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD missing(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **params,
                                             SqliteStatement *self) {
        if(numparams < 3)
            return TJS_E_BADPARAMCOUNT;
        bool ret = false;
        if(!params[0]->AsInteger()) {
            const int col = self->getColumnNo(*params[1]);
            if(col >= 0) {
                tTJSVariant value;
                getColumnData(self->stmt_, value, col);
                params[2]->AsObjectClosureNoAddRef().PropSet(
                    0, nullptr, nullptr, &value, nullptr);
                ret = true;
            }
        }
        if(result)
            *result = ret;
        return TJS_S_OK;
    }

private:
    int getColumnNo(const tTJSVariant &column) const {
        switch(column.Type()) {
            case tvtInteger:
            case tvtReal:
                return static_cast<int>(column.AsInteger());
            case tvtString: {
                const tjs_char *name = column.GetString();
                const int count = sqlite3_column_count(stmt_);
                for(int i = 0; i < count; ++i) {
                    const auto *columnName = reinterpret_cast<const tjs_char *>(
                        sqlite3_column_name16(stmt_, i));
                    if(columnName && TJS_stricmp(name, columnName) == 0)
                        return i;
                }
                break;
            }
            default:
                break;
        }
        return -1;
    }

    int _open(const tjs_char *sql, const tTJSVariant *params = nullptr) {
        close();
        int ret = sqlite3_prepare16_v2(db_, sql, -1, &stmt_, nullptr);
        if(ret == SQLITE_OK) {
            reset();
            if(params)
                ret = bindParams(stmt_, *params);
        }
        return ret;
    }

    int _bindAt(tTJSVariant &value, tTJSVariant *pos = nullptr) {
        if(pos)
            bindPos_ = getBindPos(stmt_, *pos);
        return bindParam(stmt_, value, bindPos_++);
    }

    tTJSVariant sqlite_;
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    int bindPos_ = 1;
};

NCB_REGISTER_CLASS(SqliteStatement) {
    SQLITE_ENUM(SQLITE_INTEGER);
    SQLITE_ENUM(SQLITE_FLOAT);
    SQLITE_ENUM(SQLITE_TEXT);
    SQLITE_ENUM(SQLITE_BLOB);
    SQLITE_ENUM(SQLITE_NULL);
    Factory(&ClassT::factory);
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(close);
    NCB_PROPERTY_RO(sql, getSql);
    NCB_METHOD(reset);
    NCB_METHOD(bind);
    RawCallback(TJS_W("bindAt"), &Class::bindAt, 0);
    NCB_METHOD(exec);
    NCB_METHOD(step);
    NCB_PROPERTY_RO(count, getCount);
    NCB_PROPERTY_RO(columnCount, getColumnCount);
    NCB_METHOD(isNull);
    NCB_METHOD(getType);
    NCB_METHOD(getName);
    RawCallback(TJS_W("get"), &Class::get, 0);
    RawCallback(TJS_W("missing"), &Class::missing, 0);
}

#undef SQLITE_ENUM
