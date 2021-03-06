#include <cstring>
#include <stdexcept>
#include <sstream>
#include <cstdlib>
#include <string>
#include <vector>
#include <Windows.h>
#include <mysql.h>
#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "SpDatabaseWrapper.h"
#include "Log.h"

using namespace std;

class SpDatabaseWrapperImpl;
class QueryException;
class MysqlResult;
class MysqlValue;

typedef boost::shared_ptr<MysqlResult> MysqlResultPtr;

ConnectionSettings::ConnectionSettings(const string& host, unsigned int port, const string& userName, const string& password)
    : host(host), port(port), userName(userName), password(password)
{
}

const string& ConnectionSettings::GetHost() const
{
    return this->host;
}

unsigned int ConnectionSettings::GetPort() const
{
    return this->port;
}

const string& ConnectionSettings::GetUserName() const
{
    return this->userName;
}

const string& ConnectionSettings::GetPassword() const
{
    return this->password;
}

class MysqlValue
{
public:
    MysqlValue(const char* value);
    bool IsNull() const;
    string AsString() const;
    int AsInt() const;
    long long int AsLongLongInt() const;
    bool AsBool() const;

private:
    const char* value;
};

MysqlValue::MysqlValue(const char* value)
    : value(value)
{
}

bool MysqlValue::IsNull() const
{
    return this->value == 0;
}

string MysqlValue::AsString() const
{
    if (this->IsNull())
        throw logic_error("A null MySQL value cannot be converted to string.");
    return string(this->value);
}

int MysqlValue::AsInt() const
{
    if (this->IsNull())
        throw logic_error("A null MySQL value cannot be converted to int.");
    return atoi(this->value);
}

long long int MysqlValue::AsLongLongInt() const
{
    if (this->IsNull())
        throw logic_error("A null MySQL value cannot be converted to int.");
    return boost::lexical_cast<long long int, const char*>(this->value);
}

bool MysqlValue::AsBool() const
{
    if (this->IsNull())
        throw logic_error("A null MySQL value cannot be converted to bool.");
    return (this->AsInt() != 0);
}

class MysqlResult
{
public:
    MysqlResult(MYSQL* mysql, MYSQL_RES* mysqlRes);
    ~MysqlResult();
    bool IsNull() const;
    int GetRowCount() const;
    int GetAutoGeneratedId() const;

    MysqlValue GetValue();
    MysqlValue GetValue(const char* columnName);
    MysqlValue GetValue(int columnIndex);
    MysqlValue GetValue(int rowIndex, const char* columnName);
    MysqlValue GetValue(int rowIndex, int columnIndex);

private:
    int FindColumnIndex(const char* columnName);
    MYSQL_RES* res;
    int rowCount;
    int columnCount;
    vector<string> columnNames;
    vector<MYSQL_ROW> fetchedRows;
    int autoGeneratedId;
};

MysqlResult::MysqlResult(MYSQL* mysql, MYSQL_RES* res)
    : res(res), fetchedRows(), columnNames(), rowCount(0), columnCount(0), autoGeneratedId(0)
{
    autoGeneratedId = (int)mysql_insert_id(mysql);

    if (res == 0) return;

    this->rowCount = (int)mysql_num_rows(res);
    this->columnCount = (int)mysql_num_fields(res);
}

MysqlResult::~MysqlResult()
{
    if (this->res != 0)
        mysql_free_result(this->res);
}

bool MysqlResult::IsNull() const
{
    return this->res == 0;
}

int MysqlResult::GetRowCount() const
{
    if (this->res == 0)
        throw logic_error("Cannot retrieve the row count because the result is null.");
    return this->rowCount;
}

int MysqlResult::GetAutoGeneratedId() const
{
    return this->autoGeneratedId;
}

int MysqlResult::FindColumnIndex(const char* columnName)
{
    int columnIndex;

    if (this->res == 0)
        throw logic_error("Cannot retrieve the name of a column because the result is null.");
    if (this->columnCount == 0)
        throw logic_error("Cannot retrieve the name of a column because there are no columns.");

    columnIndex = -1;
    if (this->columnNames.empty())
    {
        MYSQL_FIELD* mysqlFields = mysql_fetch_fields(res);
        for (int i = 0; i < this->columnCount; i++)
        {
            this->columnNames.push_back(string(mysqlFields[i].name));
            if (columnIndex == -1 && strcmp(mysqlFields[i].name, columnName) == 0)
                columnIndex = i;
        }
    }
    else
    {
        columnIndex = std::find(this->columnNames.begin(), this->columnNames.end(), string(columnName)) - this->columnNames.begin();
    }

    if (columnIndex == -1 ||
        columnIndex == this->columnNames.size()) // if std::find did not find it
        throw logic_error("Column name not found.");
    
    return columnIndex;
}

MysqlValue MysqlResult::GetValue()
{
    return this->GetValue(0);
}

MysqlValue MysqlResult::GetValue(const char* columnName)
{
    return this->GetValue(FindColumnIndex(columnName));
}

MysqlValue MysqlResult::GetValue(int columnIndex)
{
    return this->GetValue(0, columnIndex);
}

MysqlValue MysqlResult::GetValue(int rowIndex, const char* columnName)
{
    return this->GetValue(rowIndex, FindColumnIndex(columnName));
}

MysqlValue MysqlResult::GetValue(int rowIndex, int columnIndex)
{
    if (this->res == 0)
        throw logic_error("Cannot retrieve a value because the result is null.");
    if (this->columnCount == 0)
        throw out_of_range("Cannot retrieve a value because there are no columns.");
    if (this->rowCount == 0)
        throw out_of_range("Cannot retrieve a value because there are no rows.");
    if (columnIndex < 0)
        throw out_of_range("Column index cannot be negative.");
    if (rowIndex < 0)
        throw out_of_range("Row index cannot be negative.");
    if (columnIndex > this->columnCount - 1)
        throw out_of_range("Column index is out of range.");
    if (rowIndex > this->rowCount - 1)
        throw out_of_range("Row index is out of range.");

    if (rowIndex > (int)this->fetchedRows.size() - 1)
    {
        for (int i = this->fetchedRows.size(); i <= rowIndex; i++)
        {
            this->fetchedRows.push_back(mysql_fetch_row(this->res));
        }
    }

    return this->fetchedRows[rowIndex][columnIndex];
}

class SpDatabaseWrapperImpl
{
public:
    SpDatabaseWrapperImpl(const string& host, unsigned int port, const string& userName, const string& password);
    
    int CreateUser(const string& name, const string& password, bool isMale, const string& creationIp = string());
    int GetUserId(const string& userName);
    UserLoginInfo GetUserLoginInfo(int userId);
    IpBanInfo GetIpBanInfo(const string& ip);
    int CreateUserBan(int userId, time_t banExpirationDate = -1);
    int CreateIpBan(const string& ip, time_t banExpirationDate = -1);
    void CreateOrUpdateUserIp(int userId, const string& ip);
    void UpdateUserLastLoginDate(int userId);
    void UpdateUserLastLoginServerOnlineDate(int userId);
    void UpdateUserLastGameServerOnlineDate(int userId);
    UserPostLoginInfo GetUserPostLoginInfo(int userId);

private:
    string GetLastMysqlError();
    void Connect(const string& host, unsigned int port, const string& userName, const string& password);
    MysqlResultPtr ExecuteQuery(const string& queryString);
    MysqlResultPtr ExecuteQuery(const stringstream& queryString);
    MysqlResultPtr ExecuteQueryUnsafe(const string& queryString);
    MysqlResultPtr ExecuteQueryUnsafe(const stringstream& queryString);
    MYSQL mysql;
    boost::mutex queryLock;
};

class QueryException : public std::exception
{
public:
	QueryException(const char* query);
    QueryException(const char* query, const char* cause);
    QueryException(const stringstream& query);
    QueryException(const stringstream& query, const char* cause);
	virtual const char* what() const;
	string GetQuery() const;

private:
    void Init(const char* cause);
	string query;
    string description;
};

QueryException::QueryException(const stringstream& query)
    : query(query.str())
{
    Init(0);
}

QueryException::QueryException(const char* query)
    : query(query)
{
    Init(0);
}

QueryException::QueryException(const stringstream& query, const char* cause)
    : query(query.str())
{
    Init(cause);
}

QueryException::QueryException(const char* query, const char* cause)
    : query(query)
{
    Init(cause);
}

void QueryException::Init(const char* cause)
{
    stringstream description;
    if (cause == 0)
    {
        description << "An error occurred when processing a query.";
    }
    else
    {
        description << "An error occurred when processing a query: " << cause;
    }
    description << " The query string was: " << this->query;
    this->description = description.str();
}

const char* QueryException::what() const
{
    return this->description.c_str();
}

string QueryException::GetQuery() const
{
    return this->query;
}

SpDatabaseWrapper::SpDatabaseWrapper(const string& host, unsigned int port, const string& userName, const string& password)
    : impl(new SpDatabaseWrapperImpl(host, port, userName, password))
{
}

SpDatabaseWrapper::SpDatabaseWrapper(ConnectionSettings settings)
    : impl(new SpDatabaseWrapperImpl(settings.GetHost(), settings.GetPort(), settings.GetUserName(), settings.GetPassword()))
{
}

SpDatabaseWrapper::SpDatabaseWrapper()
    : impl(new SpDatabaseWrapperImpl(defaultConnectionSettings.GetHost(), defaultConnectionSettings.GetPort(), defaultConnectionSettings.GetUserName(), defaultConnectionSettings.GetPassword()))
{
}

SpDatabaseWrapper::~SpDatabaseWrapper()
{
    delete this->impl;
}

void SpDatabaseWrapper::SetDefaultConnectionSettings(ConnectionSettings settings)
{
    defaultConnectionSettings = settings;
}

SpDatabaseWrapperImpl::SpDatabaseWrapperImpl(const string& host, unsigned int port, const string& userName, const string& password)
{
    Connect(host, port, userName, password);
}

string SpDatabaseWrapperImpl::GetLastMysqlError()
{
    return string(mysql_error(&this->mysql));
}

void SpDatabaseWrapperImpl::Connect(const string& host, unsigned int port, const string& userName, const string& password)
{
    mysql_init(&this->mysql);
	if (!mysql_real_connect(&this->mysql, host.c_str(), userName.c_str(), password.c_str(), "sp", port, 0, 0))
    {
        string error = this->GetLastMysqlError();
        Log::out(LOG_TYPE_WARNING) << "Unable to connect to MySQL server: " << error << endl;
        throw runtime_error("Unable to connect to MySQL server.");
    }
}

MysqlResultPtr SpDatabaseWrapperImpl::ExecuteQueryUnsafe(const string& queryString)
{
    if (mysql_query(&this->mysql, queryString.c_str()) != 0)
    {
        stringstream error;
        error << "An error occurred when storing a result from a MySQL query: " <<
            this->GetLastMysqlError();
        this->queryLock.unlock();
        throw runtime_error(error.str());
    }

    MYSQL_RES* res = mysql_store_result(&this->mysql);

    if (mysql_errno(&this->mysql) != 0)
    {
        stringstream error;
        error << "An error occurred when storing a result from a MySQL query: " <<
            this->GetLastMysqlError();
        this->queryLock.unlock();
        throw runtime_error(error.str());
    }

    return MysqlResultPtr(new MysqlResult(&this->mysql, res));
}

MysqlResultPtr SpDatabaseWrapperImpl::ExecuteQueryUnsafe(const stringstream& queryString)
{
    return this->ExecuteQueryUnsafe(queryString.str());
}

MysqlResultPtr SpDatabaseWrapperImpl::ExecuteQuery(const string& queryString)
{
    this->queryLock.lock();
    MysqlResultPtr res = this->ExecuteQueryUnsafe(queryString);
    this->queryLock.unlock();
    return res;
}

MysqlResultPtr SpDatabaseWrapperImpl::ExecuteQuery(const stringstream& queryString)
{
    return this->ExecuteQuery(queryString.str().c_str());
}

int SpDatabaseWrapper::CreateUser(const string& name, const string& password, bool isMale, const string& creationIp)
{
    return impl->CreateUser(name, password, isMale, creationIp);
}

int SpDatabaseWrapper::GetUserId(const string& userName)
{
    return impl->GetUserId(userName);
}

UserLoginInfo SpDatabaseWrapper::GetUserLoginInfo(int userId)
{
    return impl->GetUserLoginInfo(userId);
}

IpBanInfo SpDatabaseWrapper::GetIpBanInfo(const string& ip)
{
    return impl->GetIpBanInfo(ip);
}

int SpDatabaseWrapper::CreateUserBan(int userId, time_t banExpirationDate)
{
    return impl->CreateUserBan(userId, banExpirationDate);
}

int SpDatabaseWrapper::CreateIpBan(const string& ip, time_t banExpirationDate)
{
    return impl->CreateIpBan(ip, banExpirationDate);
}

void SpDatabaseWrapper::CreateOrUpdateUserIp(int userId, const string& ip)
{
    impl->CreateOrUpdateUserIp(userId, ip);
}

void SpDatabaseWrapper::UpdateUserLastLoginDate(int userId)
{
    impl->UpdateUserLastLoginDate(userId);
}

void SpDatabaseWrapper::UpdateUserLastLoginServerOnlineDate(int userId)
{
    impl->UpdateUserLastLoginServerOnlineDate(userId);
}

void SpDatabaseWrapper::UpdateUserLastGameServerOnlineDate(int userId)
{
    impl->UpdateUserLastGameServerOnlineDate(userId);
}

UserPostLoginInfo SpDatabaseWrapper::GetUserPostLoginInfo(int userId)
{
    return impl->GetUserPostLoginInfo(userId);
}

int SpDatabaseWrapperImpl::CreateUser(const string& name, const string& password, bool isMale, const string& creationIp)
{
    int generatedId;

    stringstream query;
    query << "INSERT INTO user SET "
        "name = \'" << name << "\',"
        "password = \'" << password << "\',"
        "is_male = " << (int)isMale;
    if (creationIp.empty())
        query << ", creation_ip = \'" << creationIp << "\'";

    try
    {
        MysqlResultPtr res = this->ExecuteQuery(query);

        generatedId = res->GetAutoGeneratedId();
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }

    return generatedId;
}

int SpDatabaseWrapperImpl::GetUserId(const string& userName)
{
    int userId;

    stringstream query;
    query << "SELECT id FROM user WHERE name = \'" << userName << "\'";

    try
    {
        MysqlResultPtr res = this->ExecuteQuery(query);

        if (res->GetRowCount() > 0)
            userId = res->GetValue().AsInt();
        else
            userId = 0;
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }

    return userId;
}

UserLoginInfo SpDatabaseWrapperImpl::GetUserLoginInfo(int userId)
{
    string password;
    bool isDeleted;
    int banExpirationDate;

    // get password and isDeleted
    {
        stringstream query;
        query << "SELECT password, is_deleted FROM user WHERE id = " << userId;

        try
        {
            MysqlResultPtr res = this->ExecuteQuery(query);

            password = res->GetValue("user").AsString();
            isDeleted = res->GetValue("is_deleted").AsBool();
        }
        catch (exception ex)
        {
            throw QueryException(query, ex.what());
        }
    }

    // get banExpirationDate
    {
        stringstream query;
        query <<
            "(SELECT NULL as expiration_date_unix "
            "FROM userban "
            "WHERE user_id = " << userId << " AND expiration_date IS NULL) "
            "UNION "
            "(SELECT UNIX_TIMESTAMP(expiration_date) AS expiration_date_unix "
            "FROM userban "
            "WHERE user_id = " << userId << " AND expiration_date IS NOT NULL "
            "ORDER BY expiration_date_unix DESC) "
            "LIMIT 1";

        try
        {
            MysqlResultPtr res = this->ExecuteQuery(query);

            if (res->GetRowCount() == 0)
            {
                banExpirationDate = 0;
            }
            else
            {
                MysqlValue value = res->GetValue();

                if (value.IsNull())
                    banExpirationDate = -1;
                else
                    banExpirationDate = value.AsInt();
            }
        }
        catch (exception ex)
        {
            throw QueryException(query, ex.what());
        }
    }

    return UserLoginInfo(password.c_str(), isDeleted, banExpirationDate);
}

IpBanInfo SpDatabaseWrapperImpl::GetIpBanInfo(const string& ip)
{
    int banExpirationDate;

    stringstream query;
    query <<
        "(SELECT NULL as expiration_date_unix "
        "FROM ipban "
        "WHERE ip = \'" << ip << "\' AND expiration_date IS NULL) "
        "UNION "
        "(SELECT UNIX_TIMESTAMP(expiration_date) AS expiration_date_unix "
        "FROM ipban "
        "WHERE ip = \'" << ip << "\' AND expiration_date IS NOT NULL "
        "ORDER BY expiration_date_unix DESC) "
        "LIMIT 1";

    try
    {
        MysqlResultPtr res = this->ExecuteQuery(query);

        if (res->GetRowCount() == 0)
        {
            banExpirationDate = 0;
        }
        else
        {
            MysqlValue value = res->GetValue();

            if (value.IsNull())
                banExpirationDate = -1;
            else
                banExpirationDate = value.AsInt();
        }
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }

    return IpBanInfo(banExpirationDate);
}

int SpDatabaseWrapperImpl::CreateUserBan(int userId, time_t banExpirationDate)
{
    int generatedId;

    stringstream query;
    query << "INSERT INTO userban SET "
        "user_id = " << userId;
    if (banExpirationDate != -1)
        query << ", expiration_date = FROM_UNIXTIME(" << banExpirationDate << ")";

    try
    {
        MysqlResultPtr res = this->ExecuteQuery(query);

        generatedId = res->GetAutoGeneratedId();
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }

    return generatedId;
}

int SpDatabaseWrapperImpl::CreateIpBan(const string& ip, time_t banExpirationDate)
{
    int generatedId;

    stringstream query;
    query << "INSERT INTO ipban SET "
        "ip = \'" << ip << "\'";
    if (banExpirationDate != -1)
        query << ", expiration_date = FROM_UNIXTIME(" << banExpirationDate << ")";

    try
    {
        MysqlResultPtr res = this->ExecuteQuery(query);

        generatedId = res->GetAutoGeneratedId();
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }

    return generatedId;
}

void SpDatabaseWrapperImpl::CreateOrUpdateUserIp(int userId, const string& ip)
{
    stringstream query;
    query << "INSERT INTO userip SET "
        "user_id = " << userId << ","
        "ip = \'" << ip << "\' "
        "ON DUPLICATE KEY UPDATE last_show_up_date = NOW()";

    try
    {
        this->ExecuteQuery(query);
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }
}

void SpDatabaseWrapperImpl::UpdateUserLastLoginDate(int userId)
{
    stringstream query;
    query << "UPDATE user SET "
        "last_login_date = NOW() "
        "WHERE id = " << userId;

    try
    {
        this->ExecuteQuery(query);
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }
}

void SpDatabaseWrapperImpl::UpdateUserLastLoginServerOnlineDate(int userId)
{
    stringstream query;
    query << "UPDATE user SET "
        "last_loginserver_online_date = NOW() "
        "WHERE id = " << userId;

    try
    {
        this->ExecuteQuery(query);
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }
}

void SpDatabaseWrapperImpl::UpdateUserLastGameServerOnlineDate(int userId)
{
    stringstream query;
    query << "UPDATE user SET "
        "last_gameserver_online_date = NOW() "
        "WHERE id = " << userId;

    try
    {
        this->ExecuteQuery(query);
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }
}

UserPostLoginInfo SpDatabaseWrapperImpl::GetUserPostLoginInfo(int userId)
{
    bool isMale;
    int auth, defaultCharacter, rank, rankRecord, points, code;

    stringstream query;
    query << "SELECT is_male, auth, default_character, rank, rank_record, points, code "
        "FROM user "
        "WHERE user_id = " << userId;

    try
    {
        MysqlResultPtr res = this->ExecuteQuery(query);

        isMale = res->GetValue("is_male").AsBool();
        auth = res->GetValue("auth").AsInt();
        defaultCharacter = res->GetValue("default_character").AsInt();
        rank = res->GetValue("rank").AsInt();
        rankRecord = res->GetValue("rank_record").AsInt();
        points = res->GetValue("points").AsInt();
        code = res->GetValue("code").AsInt();
    }
    catch (exception ex)
    {
        throw QueryException(query, ex.what());
    }

    return UserPostLoginInfo(isMale, auth, defaultCharacter, rank, rankRecord, points, code);
}
