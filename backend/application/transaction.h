#pragma once
#include "domain/repositories.h"
namespace nekotune {
/// Rolls back on every early return, including a failed commit; does not nest transactions.
class Transaction final {
  public:
    explicit Transaction(ITransaction &session) : m_session(session), m_active(session.begin()) {}
    ~Transaction() {
        if (m_active)
            m_session.rollback();
    }
    explicit operator bool() const { return m_active; }
    bool commit() {
        if (!m_active || !m_session.commit())
            return false;
        m_active = false;
        return true;
    }
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

  private:
    ITransaction &m_session;
    bool m_active;
};
} // namespace nekotune
