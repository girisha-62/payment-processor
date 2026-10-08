#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <optional>

struct Account {
    int id;
    std::string ownerName;
    long long balanceCents; // integer cents avoids floating point money bugs
    mutable std::mutex mtx; // per-account lock: fine-grained, not a single global lock

    Account(int id_, std::string owner, long long balance)
        : id(id_), ownerName(std::move(owner)), balanceCents(balance) {}

    // Accounts hold a mutex -> not copyable/movable. We store them via shared_ptr.
    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;
};

enum class AccountOpResult { SUCCESS, INSUFFICIENT_FUNDS, ACCOUNT_NOT_FOUND };

// Owns all accounts and is the *only* place balances are mutated.
// Per-account locking + a fixed lock ordering (lower account id first) for any
// operation that touches two accounts at once, so concurrent transfers can
// never deadlock against each other.
class AccountService {
public:
    void createAccount(int id, const std::string& owner, long long openingBalanceCents) {
        std::unique_lock<std::mutex> lock(mapMutex_);
        accounts_.emplace(id, std::make_shared<Account>(id, owner, openingBalanceCents));
    }

    std::shared_ptr<Account> getAccount(int id) {
        std::unique_lock<std::mutex> lock(mapMutex_);
        auto it = accounts_.find(id);
        if (it == accounts_.end()) return nullptr;
        return it->second;
    }

    long long getBalance(int id) {
        auto acc = getAccount(id);
        if (!acc) throw std::runtime_error("account not found: " + std::to_string(id));
        std::lock_guard<std::mutex> lock(acc->mtx);
        return acc->balanceCents;
    }

    // Credit: money into an account (e.g. successful deposit, refund target).
    AccountOpResult credit(int accountId, long long amountCents) {
        auto acc = getAccount(accountId);
        if (!acc) return AccountOpResult::ACCOUNT_NOT_FOUND;
        std::lock_guard<std::mutex> lock(acc->mtx);
        acc->balanceCents += amountCents;
        return AccountOpResult::SUCCESS;
    }

    // Debit: money out of an account. Fails cleanly on insufficient funds
    // instead of allowing an overdraft.
    AccountOpResult debit(int accountId, long long amountCents) {
        auto acc = getAccount(accountId);
        if (!acc) return AccountOpResult::ACCOUNT_NOT_FOUND;
        std::lock_guard<std::mutex> lock(acc->mtx);
        if (acc->balanceCents < amountCents) return AccountOpResult::INSUFFICIENT_FUNDS;
        acc->balanceCents -= amountCents;
        return AccountOpResult::SUCCESS;
    }

    // Transfer: debit `from`, credit `to`, atomically w.r.t. other transfers.
    // Locks both accounts at once using std::lock (deadlock-avoiding) rather
    // than locking them one at a time.
    AccountOpResult transfer(int fromId, int toId, long long amountCents) {
        auto from = getAccount(fromId);
        auto to = getAccount(toId);
        if (!from || !to) return AccountOpResult::ACCOUNT_NOT_FOUND;

        // Self-transfer guard: if fromId == toId, `from` and `to` are the
        // SAME Account, so locking "both" mutexes would lock one non-recursive
        // std::mutex twice on the same thread -> instant deadlock. This is a
        // classic concurrency bug that only shows up under load once a
        // self-transfer happens to be generated, which is exactly what a
        // stress test with random account ids will eventually hit.
        if (from->id == to->id) {
            std::lock_guard<std::mutex> lock(from->mtx);
            if (from->balanceCents < amountCents) return AccountOpResult::INSUFFICIENT_FUNDS;
            return AccountOpResult::SUCCESS; // no-op: money leaves and returns to the same place
        }

        // Always acquire in a consistent order (lower account id first) to
        // avoid the classic "A locks 1-then-2 while B locks 2-then-1" deadlock.
        Account* first = from->id < to->id ? from.get() : to.get();
        Account* second = from->id < to->id ? to.get() : from.get();

        std::unique_lock<std::mutex> lockA(first->mtx, std::defer_lock);
        std::unique_lock<std::mutex> lockB(second->mtx, std::defer_lock);
        std::lock(lockA, lockB);

        if (from->balanceCents < amountCents) return AccountOpResult::INSUFFICIENT_FUNDS;
        from->balanceCents -= amountCents;
        to->balanceCents += amountCents;
        return AccountOpResult::SUCCESS;
    }

private:
    std::unordered_map<int, std::shared_ptr<Account>> accounts_;
    std::mutex mapMutex_; // protects the map structure only, not balances
};
