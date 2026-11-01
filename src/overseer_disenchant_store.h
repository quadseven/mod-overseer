#ifndef OVERSEER_DISENCHANT_STORE_H
#define OVERSEER_DISENCHANT_STORE_H

#include "ModuleDatabasePool.h"
#include "MySQLPreparedStatement.h"
#include "QueryCallback.h"
#include <string>

namespace OverseerDisenchant
{
enum Statement : uint32
{
    Pending,
    Claim,
    Owned,
    Complete,
    Acknowledge,
    Defer,
    SayHeld,
    StatementCount
};

class Connection final : public MySQLConnection
{
public:
    explicit Connection(MySQLConnectionInfo& info) : MySQLConnection(info) {}
    Connection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& info)
        : MySQLConnection(queue, info) {}

    void DoPrepareStatements() override
    {
        if (!m_reconnecting)
            m_stmts.resize(StatementCount);
        PrepareStatement(Pending,
            "SELECT c.id, c.target_name FROM overseer_command c "
            "JOIN overseer_roster r ON r.name = c.target_name "
            "WHERE c.kind = 'disenchant' AND c.status = 'pending' "
            "AND r.enabled = 1 AND r.job = 'craft' "
            "ORDER BY c.updated_at, c.id LIMIT 32", CONNECTION_ASYNC);
        PrepareStatement(Claim,
            "UPDATE overseer_command SET status = 'claimed', claimed_by = ? "
            "WHERE id = ? AND kind = 'disenchant' AND status = 'pending'",
            CONNECTION_ASYNC);
        PrepareStatement(Owned,
            // A valid empty snapshot returns id 0; a failed query still returns no result.
            "SELECT COALESCE(c.id, 0), COALESCE(c.target_name, ''), "
            "COALESCE(r.job, ''), COALESCE(r.enabled, 0) "
            "FROM (SELECT 1) AS owned_snapshot LEFT JOIN overseer_command c "
            "ON c.kind = 'disenchant' AND c.status = 'claimed' AND c.claimed_by = ? "
            "LEFT JOIN overseer_roster r ON r.name = c.target_name "
            "ORDER BY c.id", CONNECTION_ASYNC);
        PrepareStatement(Complete,
            "UPDATE overseer_command SET status = 'delivered', "
            "result = 'disenchanted: reagent inventory verified and source consumed' "
            "WHERE id = ? AND claimed_by = ? AND kind = 'disenchant' "
            "AND status = 'claimed'", CONNECTION_ASYNC);
        PrepareStatement(Acknowledge,
            "SELECT status FROM overseer_command WHERE id = ? AND claimed_by = ? "
            "AND kind = 'disenchant'", CONNECTION_ASYNC);
        PrepareStatement(Defer,
            "UPDATE overseer_command SET updated_at = NOW() "
            "WHERE id = ? AND kind = 'disenchant' AND status = 'pending'",
            CONNECTION_ASYNC);
        PrepareStatement(SayHeld,
            "UPDATE overseer_command SET result = ? WHERE id = ? "
            "AND claimed_by = ? AND kind = 'disenchant' AND status = 'claimed'",
            CONNECTION_ASYNC);
    }
};

// Reuses the configured character-database identity, with one queued worker.
// A separate module pool is required to register module-owned prepared SQL;
// it creates no database, schema, account, grant or new credential.
class Store final : public ModuleDatabasePool
{
public:
    bool Start(std::string const& connectionInfo)
    {
        SetConnectionInfo(connectionInfo, 1, 1);
        if (Open() != 0 || !PrepareStatements())
        {
            Close();
            return false;
        }
        _ready = true;
        return true;
    }

    void Stop()
    {
        if (_ready)
            Close();
        _ready = false;
    }

    bool Ready() const { return _ready; }

    PreparedStatementBase* Bound(Statement index)
    {
        return new PreparedStatementBase(index, GetPreparedStatementParamCount(index));
    }

protected:
    MySQLConnection* CreateConnection(MySQLConnectionInfo& info) override
    {
        return new Connection(info);
    }

    MySQLConnection* CreateConnection(ProducerConsumerQueue<SQLOperation*>* queue,
                                      MySQLConnectionInfo& info) override
    {
        return new Connection(queue, info);
    }

private:
    bool _ready = false;
};
}

#endif
