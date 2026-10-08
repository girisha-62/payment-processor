#pragma once
#include <string>
#include <chrono>

enum class TransactionType {
    AUTHORIZATION,
    DEBIT,
    CREDIT,
    REFUND,
    REVERSAL,
    TRANSFER
};

enum class TransactionStatus {
    PENDING,
    SUCCESS,
    FAILED,
    DUPLICATE
};

inline std::string toString(TransactionType t) {
    switch (t) {
        case TransactionType::AUTHORIZATION: return "AUTHORIZATION";
        case TransactionType::DEBIT: return "DEBIT";
        case TransactionType::CREDIT: return "CREDIT";
        case TransactionType::REFUND: return "REFUND";
        case TransactionType::REVERSAL: return "REVERSAL";
        case TransactionType::TRANSFER: return "TRANSFER";
    }
    return "UNKNOWN";
}

inline std::string toString(TransactionStatus s) {
    switch (s) {
        case TransactionStatus::PENDING: return "PENDING";
        case TransactionStatus::SUCCESS: return "SUCCESS";
        case TransactionStatus::FAILED: return "FAILED";
        case TransactionStatus::DUPLICATE: return "DUPLICATE";
    }
    return "UNKNOWN";
}

struct TransactionRequest {
    std::string txnId;          // unique id for this request
    std::string idempotencyKey; // client-supplied key; same key => same logical txn
    TransactionType type;
    int fromAccount;
    int toAccount = -1;         // only used for TRANSFER
    long long amountCents;
};

struct TransactionResult {
    std::string txnId;
    TransactionStatus status;
    std::string message;
    std::chrono::system_clock::time_point processedAt;
};
