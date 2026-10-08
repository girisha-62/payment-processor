#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <future>
#include <sstream>

#include "ThreadPool.hpp"
#include "AccountService.hpp"
#include "Transaction.hpp"
#include "Logger.hpp"

// Orchestrates the pipeline: validate -> duplicate check -> authenticate ->
// route to AccountService -> log -> record in history.
// Each incoming request is handed to the thread pool, so many transactions
// are validated/processed concurrently while account state stays consistent
// (all real mutation happens inside AccountService's per-account locks).
class TransactionProcessor {
public:
    TransactionProcessor(AccountService& accountService, size_t numWorkerThreads,
                          const std::string& logFilePath)
        : accountService_(accountService),
          pool_(numWorkerThreads),
          logger_(logFilePath) {}

    std::future<TransactionResult> submit(TransactionRequest req) {
        return pool_.submit([this, req]() { return process(req); });
    }

    std::vector<TransactionResult> history() {
        std::lock_guard<std::mutex> lock(historyMutex_);
        return history_;
    }

    struct Counters {
        int success = 0, failed = 0, duplicate = 0, total = 0;
    };

    Counters counters() {
        std::lock_guard<std::mutex> lock(historyMutex_);
        Counters c;
        c.total = static_cast<int>(history_.size());
        for (auto& r : history_) {
            if (r.status == TransactionStatus::SUCCESS) c.success++;
            else if (r.status == TransactionStatus::FAILED) c.failed++;
            else if (r.status == TransactionStatus::DUPLICATE) c.duplicate++;
        }
        return c;
    }

private:
    TransactionResult process(const TransactionRequest& req) {
        // 1. Validate
        if (req.amountCents <= 0) {
            return finalize(req, TransactionStatus::FAILED, "invalid amount");
        }

        // 2. Duplicate detection (idempotency key), guarded by its own mutex
        //    so this check-and-insert is atomic across threads.
        {
            std::lock_guard<std::mutex> lock(dedupeMutex_);
            if (!seenKeys_.insert(req.idempotencyKey).second) {
                return finalize(req, TransactionStatus::DUPLICATE,
                                 "duplicate idempotency key: " + req.idempotencyKey);
            }
        }

        // 3. "Authenticate" -- in a real system this would check an auth
        //    token / signature. Kept simple here since accounts are trusted.
        auto acc = accountService_.getAccount(req.fromAccount);
        if (!acc) {
            return finalize(req, TransactionStatus::FAILED, "unknown account " +
                             std::to_string(req.fromAccount));
        }

        // 4. Route by type
        AccountOpResult opResult;
        switch (req.type) {
            case TransactionType::DEBIT:
            case TransactionType::AUTHORIZATION:
                opResult = accountService_.debit(req.fromAccount, req.amountCents);
                break;
            case TransactionType::CREDIT:
            case TransactionType::REFUND:
                opResult = accountService_.credit(req.fromAccount, req.amountCents);
                break;
            case TransactionType::REVERSAL:
                // Reverse a prior debit by crediting back.
                opResult = accountService_.credit(req.fromAccount, req.amountCents);
                break;
            case TransactionType::TRANSFER:
                opResult = accountService_.transfer(req.fromAccount, req.toAccount, req.amountCents);
                break;
            default:
                return finalize(req, TransactionStatus::FAILED, "unsupported type");
        }

        if (opResult == AccountOpResult::SUCCESS) {
            return finalize(req, TransactionStatus::SUCCESS, "ok");
        } else if (opResult == AccountOpResult::INSUFFICIENT_FUNDS) {
            return finalize(req, TransactionStatus::FAILED, "insufficient funds");
        } else {
            return finalize(req, TransactionStatus::FAILED, "account not found");
        }
    }

    TransactionResult finalize(const TransactionRequest& req, TransactionStatus status,
                                const std::string& message) {
        TransactionResult result{req.txnId, status, message, std::chrono::system_clock::now()};

        std::ostringstream oss;
        oss << "txn=" << req.txnId
            << " type=" << toString(req.type)
            << " from=" << req.fromAccount
            << (req.toAccount >= 0 ? " to=" + std::to_string(req.toAccount) : "")
            << " amount=" << req.amountCents
            << " status=" << toString(status)
            << " (" << message << ")"
            << " thread=" << std::this_thread::get_id();
        logger_.log(oss.str());

        {
            std::lock_guard<std::mutex> lock(historyMutex_);
            history_.push_back(result);
        }
        return result;
    }

    AccountService& accountService_;
    ThreadPool pool_;
    Logger logger_;

    std::unordered_set<std::string> seenKeys_;
    std::mutex dedupeMutex_;

    std::vector<TransactionResult> history_;
    std::mutex historyMutex_;
};
