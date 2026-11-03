#ifndef KCE_DATABASE_MGR_H_
#define KCE_DATABASE_MGR_H_

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

/** SQLite3 数据库访问封装：所有 SQL 均通过参数绑定执行，避免拼接注入。 */
class DatabaseMgr
{
public:
    class Value
    {
    public:
        enum class Type
        {
            Null,
            Int,
            Real,
            Text
        };

        Value();
        explicit Value(int64_t value);
        explicit Value(double value);
        explicit Value(const std::string& value);
        explicit Value(const std::wstring& value);

        static Value Null();
        static Value Int(int64_t value);
        static Value Real(double value);
        static Value Text(const std::string& value);
        static Value Text(const std::wstring& value);

        Type GetType() const { return m_type; }
        int64_t AsInt() const;
        double AsDouble() const;
        const std::string& AsUtf8() const { return m_text; }
        std::wstring AsWide() const;

    private:
        Type m_type = Type::Null;
        int64_t m_int = 0;
        double m_real = 0.0;
        std::string m_text;
    };

    using Row = std::vector<Value>;
    using Rows = std::vector<Row>;

public:
    DatabaseMgr();
    ~DatabaseMgr();

    DatabaseMgr(const DatabaseMgr&) = delete;
    DatabaseMgr& operator = (const DatabaseMgr&) = delete;

    bool Open(const std::wstring& filePath);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    bool Execute(const std::string& sql, const std::vector<Value>& params = {});
    bool Execute(const std::string& sql, const std::vector<Value>& params, int64_t& lastInsertId);

    int64_t QueryInt(const std::string& sql,
                     const std::vector<Value>& params = {},
                     int64_t defaultValue = 0);
    std::string QueryText(const std::string& sql,
                          const std::vector<Value>& params = {},
                          const std::string& defaultValue = std::string());
    Rows Query(const std::string& sql, const std::vector<Value>& params = {});

    int64_t LastInsertRowId() const;
    int Changes() const;

    bool BeginTransaction();
    bool Commit();
    bool Rollback();

    const std::string& GetLastError() const { return m_lastError; }

    static std::string ToUtf8(const std::wstring& text);
    static std::wstring FromUtf8(const std::string& text);

private:
    bool Prepare(const std::string& sql, sqlite3_stmt** statement);
    bool BindParams(sqlite3_stmt* statement, const std::vector<Value>& params);
    void SetError(const std::string& message);

private:
    sqlite3* m_db = nullptr;
    std::string m_lastError;
};

#endif // KCE_DATABASE_MGR_H_
