#include <iostream>
#include <vector>
#include <thread>
#include <future>
#include <random>
#include <atomic>
#include <iomanip>

#include "AccountService.hpp"
#include "TransactionProcessor.hpp"

// Simulates `numClients` concurrent clients, each firing `txnsPerClient`
// transactions at the processor. Deliberately includes: an insufficient-funds
// case, a duplicate idempotency key, and interleaved transfers, so a single
// run demonstrates every status path.
void runLoadTest(TransactionProcessor& processor, AccountService& /*accounts*/,
                  int numClients, int txnsPerClient) {
    std::vector<std::thread> clients;
    std::vector<std::future<TransactionResult>> futures;
    std::mutex futuresMutex;

    std::atomic<int> txnCounter{0};

    for (int c = 0; c < numClients; ++c) {
        clients.emplace_back([&, c]() {
            std::mt19937 rng(std::random_device{}() + c);
            std::uniform_int_distribution<int> accountPick(1, 5);
            std::uniform_int_distribution<int> amountPick(500, 5000); // cents
            std::uniform_int_distribution<int> typePick(0, 3);

            for (int i = 0; i < txnsPerClient; ++i) {
                int n = txnCounter.fetch_add(1);
                TransactionRequest req;
                req.txnId = "TXN-" + std::to_string(n);
                req.idempotencyKey = "KEY-" + std::to_string(n);
                req.amountCents = amountPick(rng);
                req.fromAccount = accountPick(rng);

                switch (typePick(rng)) {
                    case 0: req.type = TransactionType::DEBIT; break;
                    case 1: req.type = TransactionType::CREDIT; break;
                    case 2: req.type = TransactionType::REFUND; break;
                    default:
                        req.type = TransactionType::TRANSFER;
                        req.toAccount = accountPick(rng);
                        break;
                }

                auto fut = processor.submit(req);
                std::lock_guard<std::mutex> lock(futuresMutex);
                futures.push_back(std::move(fut));
            }
        });
    }

    // Deliberate insufficient-funds case: account 5 starts with a small balance.
    {
        TransactionRequest req{"TXN-OVERDRAFT", "KEY-OVERDRAFT", TransactionType::DEBIT, 5, -1, 999999};
        std::lock_guard<std::mutex> lock(futuresMutex);
        futures.push_back(processor.submit(req));
    }

    // Deliberate duplicate: submit the exact same idempotency key twice.
    {
        TransactionRequest req1{"TXN-DUP-A", "KEY-DUPLICATE", TransactionType::CREDIT, 1, -1, 1000};
        TransactionRequest req2{"TXN-DUP-B", "KEY-DUPLICATE", TransactionType::CREDIT, 1, -1, 1000};
        std::lock_guard<std::mutex> lock(futuresMutex);
        futures.push_back(processor.submit(req1));
        futures.push_back(processor.submit(req2));
    }

    for (auto& t : clients) t.join();

    // Resolve every future so exceptions (if any) surface, and so we know
    // all submitted work has actually finished processing.
    for (auto& f : futures) f.get();
}

void printBalances(AccountService& accounts) {
    std::cout << "\n=== Final Account Balances ===\n";
    for (int id = 1; id <= 5; ++id) {
        long long cents = accounts.getBalance(id);
        std::cout << "  Account " << id << ": $"
                   << std::fixed << std::setprecision(2) << (cents / 100.0) << "\n";
    }
}

void printSummary(TransactionProcessor& processor) {
    auto c = processor.counters();
    std::cout << "\n=== Transaction Summary ===\n"
              << "  Total:     " << c.total << "\n"
              << "  Success:   " << c.success << "\n"
              << "  Failed:    " << c.failed << "\n"
              << "  Duplicate: " << c.duplicate << "\n";
}

int main(int argc, char** argv) {
    int numWorkerThreads = 4;
    int numClients = 6;
    int txnsPerClient = 20;

    if (argc >= 2) numWorkerThreads = std::stoi(argv[1]);
    if (argc >= 3) numClients = std::stoi(argv[2]);
    if (argc >= 4) txnsPerClient = std::stoi(argv[3]);

    std::cout << "Starting payment processor: "
              << numWorkerThreads << " worker threads, "
              << numClients << " clients x " << txnsPerClient << " txns each\n\n";

    AccountService accounts;
    accounts.createAccount(1, "Alice",   50000); // $500.00
    accounts.createAccount(2, "Bob",     30000);
    accounts.createAccount(3, "Charlie", 100000);
    accounts.createAccount(4, "Diana",   20000);
    accounts.createAccount(5, "Evan",    1000);  // intentionally low, for overdraft test

    TransactionProcessor processor(accounts, numWorkerThreads, "transactions.log");

    auto start = std::chrono::steady_clock::now();
    runLoadTest(processor, accounts, numClients, txnsPerClient);
    auto end = std::chrono::steady_clock::now();

    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    printBalances(accounts);
    printSummary(processor);
    std::cout << "\nProcessed in " << elapsedMs << " ms using " << numWorkerThreads
              << " worker threads.\n";
    std::cout << "Full log written to transactions.log\n";

    return 0;
}
