#include "DatabaseMgr.h"

#include "third-party/sqlite/sqlite3.h"

#include <Windows.h>

#include <cstdio>
#include <string>

DatabaseMgr::Value::Value() = default;

DatabaseMgr::Value::Value(int64_t value) :
    m_type(Type::Int),
    m_int(value)
{
}

DatabaseMgr::Value::Value(double value) :
    m_type(Type::Real),
    m_real(value)
{
}

DatabaseMgr::Value::Value(const std::string& value) :
    m_type(Type::Text),
    m_text(value)
{
}

DatabaseMgr::Value::Value(const std::wstring& value) :
    m_type(Type::Text),
    m_text(ToUtf8(value))
{
}

DatabaseMgr::Value DatabaseMgr::Value::Null()
{
    return Value();
}

DatabaseMgr::Value DatabaseMgr::Value::Int(int64_t value)
{
    return Value(value);
}

DatabaseMgr::Value DatabaseMgr::Value::Real(double value)
{
    return Value(value);
}

DatabaseMgr::Value DatabaseMgr::Value::Text(const std::string& value)
{
    return Value(value);
}

DatabaseMgr::Value DatabaseMgr::Value::Text(const std::wstring& value)
{
    return Value(value);
}

int64_t DatabaseMgr::Value::AsInt() const
{
    switch (m_type) {
    case Type::Int:
        return m_int;
    case Type::Real:
        return static_cast<int64_t>(m_real);
    case Type::Text:
        try {
            return std::stoll(m_text);
        }
        catch (...) {
            return 0;
        }
    default:
        return 0;
    }
}

double DatabaseMgr::Value::AsDouble() const
{
    switch (m_type) {
    case Type::Int:
        return static_cast<double>(m_int);
    case Type::Real:
        return m_real;
    case Type::Text:
        try {
            return std::stod(m_text);
        }
        catch (...) {
            return 0.0;
        }
    default:
        return 0.0;
    }
}

std::wstring DatabaseMgr::Value::AsWide() const
{
    if (m_type == Type::Text) {
        return DatabaseMgr::FromUtf8(m_text);
    }
    if (m_type == Type::Int) {
        return std::to_wstring(m_int);
    }
    if (m_type == Type::Real) {
        wchar_t buffer[64] = { 0 };
        swprintf_s(buffer, L"%.6f", m_real);
        return buffer;
    }
    return std::wstring();
}

DatabaseMgr::DatabaseMgr() = default;

DatabaseMgr::~DatabaseMgr()
{
    Close();
}

bool DatabaseMgr::Open(const std::wstring& filePath)
{
    Close();
    m_lastError.clear();

    const std::string utf8Path = ToUtf8(filePath);
    const int result = sqlite3_open_v2(utf8Path.c_str(), &m_db,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                       nullptr);
    if (result != SQLITE_OK) {
        if (m_db != nullptr) {
            SetError(sqlite3_errmsg(m_db));
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        else {
            SetError("sqlite3_open_v2 failed");
        }
        return false;
    }

    sqlite3_busy_timeout(m_db, 3000);
    sqlite3_exec(m_db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    return true;
}

void DatabaseMgr::Close()
{
    if (m_db != nullptr) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool DatabaseMgr::Prepare(const std::string& sql, sqlite3_stmt** statement)
{
    *statement = nullptr;
    if (m_db == nullptr) {
        SetError("database is not open");
        return false;
    }
    const int result = sqlite3_prepare_v2(m_db, sql.c_str(), -1, statement, nullptr);
    if (result != SQLITE_OK) {
        SetError(sqlite3_errmsg(m_db));
        if (*statement != nullptr) {
            sqlite3_finalize(*statement);
            *statement = nullptr;
        }
        return false;
    }
    return true;
}

bool DatabaseMgr::BindParams(sqlite3_stmt* statement, const std::vector<Value>& params)
{
    for (size_t i = 0; i < params.size(); ++i) {
        const int index = static_cast<int>(i + 1);
        const Value& value = params[i];
        int result = SQLITE_OK;
        switch (value.GetType()) {
        case Value::Type::Null:
            result = sqlite3_bind_null(statement, index);
            break;
        case Value::Type::Int:
            result = sqlite3_bind_int64(statement, index, value.AsInt());
            break;
        case Value::Type::Real:
            result = sqlite3_bind_double(statement, index, value.AsDouble());
            break;
        case Value::Type::Text: {
            const std::string& text = value.AsUtf8();
            result = sqlite3_bind_text(statement, index, text.c_str(),
                                       static_cast<int>(text.size()), SQLITE_TRANSIENT);
            break;
        }
        default:
            result = SQLITE_MISUSE;
            break;
        }
        if (result != SQLITE_OK) {
            SetError(sqlite3_errmsg(m_db));
            return false;
        }
    }
    return true;
}

bool DatabaseMgr::Execute(const std::string& sql, const std::vector<Value>& params)
{
    int64_t ignoredId = 0;
    return Execute(sql, params, ignoredId);
}

bool DatabaseMgr::Execute(const std::string& sql, const std::vector<Value>& params, int64_t& lastInsertId)
{
    lastInsertId = 0;
    sqlite3_stmt* statement = nullptr;
    if (!Prepare(sql, &statement)) {
        return false;
    }
    if (!BindParams(statement, params)) {
        sqlite3_finalize(statement);
        return false;
    }

    const int result = sqlite3_step(statement);
    const bool success = (result == SQLITE_DONE) || (result == SQLITE_ROW);
    if (!success) {
        SetError(sqlite3_errmsg(m_db));
    }
    if (success) {
        lastInsertId = sqlite3_last_insert_rowid(m_db);
    }
    sqlite3_finalize(statement);
    return success;
}

int64_t DatabaseMgr::QueryInt(const std::string& sql,
                              const std::vector<Value>& params,
                              int64_t defaultValue)
{
    const Rows rows = Query(sql, params);
    if (rows.empty() || rows.front().empty()) {
        return defaultValue;
    }
    return rows.front().front().AsInt();
}

std::string DatabaseMgr::QueryText(const std::string& sql,
                                   const std::vector<Value>& params,
                                   const std::string& defaultValue)
{
    const Rows rows = Query(sql, params);
    if (rows.empty() || rows.front().empty()) {
        return defaultValue;
    }
    return rows.front().front().AsUtf8();
}

DatabaseMgr::Rows DatabaseMgr::Query(const std::string& sql, const std::vector<Value>& params)
{
    Rows rows;
    sqlite3_stmt* statement = nullptr;
    if (!Prepare(sql, &statement)) {
        return rows;
    }
    if (!BindParams(statement, params)) {
        sqlite3_finalize(statement);
        return rows;
    }

    const int columnCount = sqlite3_column_count(statement);
    for (;;) {
        const int result = sqlite3_step(statement);
        if (result == SQLITE_ROW) {
            Row row;
            row.reserve(static_cast<size_t>(columnCount));
            for (int i = 0; i < columnCount; ++i) {
                switch (sqlite3_column_type(statement, i)) {
                case SQLITE_INTEGER:
                    row.emplace_back(static_cast<int64_t>(sqlite3_column_int64(statement, i)));
                    break;
                case SQLITE_FLOAT:
                    row.emplace_back(sqlite3_column_double(statement, i));
                    break;
                case SQLITE_TEXT: {
                    const unsigned char* text = sqlite3_column_text(statement, i);
                    const int size = sqlite3_column_bytes(statement, i);
                    row.emplace_back(std::string(reinterpret_cast<const char*>(text),
                                                 static_cast<size_t>(size)));
                    break;
                }
                default:
                    row.emplace_back(Value::Null());
                    break;
                }
            }
            rows.push_back(std::move(row));
        }
        else if (result == SQLITE_DONE) {
            break;
        }
        else {
            SetError(sqlite3_errmsg(m_db));
            break;
        }
    }
    sqlite3_finalize(statement);
    return rows;
}

int64_t DatabaseMgr::LastInsertRowId() const
{
    return m_db != nullptr ? sqlite3_last_insert_rowid(m_db) : 0;
}

int DatabaseMgr::Changes() const
{
    return m_db != nullptr ? sqlite3_changes(m_db) : 0;
}

bool DatabaseMgr::BeginTransaction()
{
    return Execute("BEGIN TRANSACTION;");
}

bool DatabaseMgr::Commit()
{
    return Execute("COMMIT TRANSACTION;");
}

bool DatabaseMgr::Rollback()
{
    return Execute("ROLLBACK TRANSACTION;");
}

std::string DatabaseMgr::ToUtf8(const std::wstring& text)
{
    if (text.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::string();
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring DatabaseMgr::FromUtf8(const std::string& text)
{
    if (text.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

void DatabaseMgr::SetError(const std::string& message)
{
    m_lastError = message;
}
