#include "checked_order_book.hpp"
#include "order_book_test_access.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
struct MissingFeature : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct TestCase {
    std::string_view name;
    void (*run)();
};
std::vector<TestCase>& cases() {
    static std::vector<TestCase> result;
    return result;
}
struct Register {
    Register(std::string_view name, void (*run)()) { cases().push_back({name, run}); }
};
#define TEST_CASE(name) \
    void name(); \
    const Register register_##name{#name, name}; \
    void name()
#define CHECK(expression) \
    do { \
        if (!(expression)) \
            throw std::runtime_error(std::string(__FILE__) + ":" + \
                std::to_string(__LINE__) + ": " #expression); \
    } while (false)

constexpr Side sides[] = {Side::Buy, Side::Sell};
// Contract limits are literal test expectations, independent of production validation.
constexpr Price max_price = 1'000'000'000;
constexpr Quantity max_quantity = 1'000'000'000;
Side opposite(Side side) { return side == Side::Buy ? Side::Sell : Side::Buy; }
// Increasing offset walks farther into the opposite book for either aggressor side.
Price price(Side aggressor, Price offset) {
    return aggressor == Side::Buy ? 100 + offset : 100 - offset;
}
Order order(OrderId id, Side side, Price p, Quantity q) { return {id, side, p, q}; }
Trade execution(Side aggressor, OrderId taker, OrderId maker, Price p, Quantity q) {
    return aggressor == Side::Buy ? Trade{taker, maker, p, q} : Trade{maker, taker, p, q};
}
void check_trades(const std::vector<Trade>& actual, const std::vector<Trade>& expected) {
    CHECK(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto& a = actual[i];
        const auto& e = expected[i];
        if (a.buy_id != e.buy_id || a.sell_id != e.sell_id ||
            a.price != e.price || a.quantity != e.quantity) {
            throw std::runtime_error("trade " + std::to_string(i) +
                ": expected buy=" + std::to_string(e.buy_id) +
                " sell=" + std::to_string(e.sell_id) +
                " price=" + std::to_string(e.price) + " qty=" + std::to_string(e.quantity) +
                "; got buy=" + std::to_string(a.buy_id) +
                " sell=" + std::to_string(a.sell_id) +
                " price=" + std::to_string(a.price) + " qty=" + std::to_string(a.quantity));
        }
        CHECK(a.quantity > 0);
    }
}
void check_empty(const CheckedOrderBook& book) {
    CHECK(!book.best_bid());
    CHECK(!book.best_ask());
}
template<class Action>
void expect_rejection(Action action) {
    bool rejected = false;
    try { action(); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

TEST_CASE(resting_sell_price_with_descending_ids) {
    CheckedOrderBook book;
    book.submit(order(100, Side::Sell, 98, 5));
    check_trades(book.submit(order(1, Side::Buy, 100, 5)), {{1, 100, 98, 5}});
    check_empty(book);
}
TEST_CASE(resting_buy_price_with_descending_ids) {
    CheckedOrderBook book;
    book.submit(order(100, Side::Buy, 102, 5));
    check_trades(book.submit(order(1, Side::Sell, 100, 5)), {{100, 1, 102, 5}});
    check_empty(book);
}
TEST_CASE(fifo_uses_arrival_not_id_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        for (OrderId id : {90u, 10u, 70u})
            book.submit(order(id, opposite(side), 100, 2));
        check_trades(book.submit(order(50, side, 100, 5)), {
            execution(side, 50, 90, 100, 2), execution(side, 50, 10, 100, 2),
            execution(side, 50, 70, 100, 1)});
        check_trades(book.submit(order(60, side, 100, 1)), {execution(side, 60, 70, 100, 1)});
        check_empty(book);
    }
}
TEST_CASE(sweep_prices_with_shuffled_ids_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(90, opposite(side), price(side, 2), 3));
        book.submit(order(70, opposite(side), price(side, 0), 2));
        book.submit(order(10, opposite(side), price(side, 1), 4));
        check_trades(book.submit(order(50, side, price(side, 3), 9)), {
            execution(side, 50, 70, price(side, 0), 2),
            execution(side, 50, 10, price(side, 1), 4),
            execution(side, 50, 90, price(side, 2), 3)});
        check_empty(book);
    }
}
TEST_CASE(partial_maker_keeps_fifo_priority_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 5));
        book.submit(order(2, opposite(side), 100, 4));
        check_trades(book.submit(order(3, side, 100, 2)), {execution(side, 3, 1, 100, 2)});
        book.submit(order(4, opposite(side), 100, 1));
        check_trades(book.submit(order(5, side, 100, 8)), {
            execution(side, 5, 1, 100, 3), execution(side, 5, 2, 100, 4),
            execution(side, 5, 4, 100, 1)});
        check_empty(book);
    }
}
TEST_CASE(residual_rests_at_own_limit_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), price(side, 0), 2));
        check_trades(book.submit(order(2, side, price(side, 2), 5)), {
            execution(side, 2, 1, price(side, 0), 2)});
        CHECK((side == Side::Buy ? book.best_bid() : book.best_ask()) == price(side, 2));
        check_trades(book.submit(order(3, opposite(side), price(side, 1), 3)), {
            execution(opposite(side), 3, 2, price(side, 2), 3)});
        check_empty(book);
    }
}
TEST_CASE(later_order_joins_behind_partial_maker_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 3));
        book.submit(order(2, side, 100, 4));
        check_trades(book.submit(order(3, opposite(side), 100, 5)), {
            execution(opposite(side), 3, 1, 100, 3), execution(opposite(side), 3, 2, 100, 2)});
        book.submit(order(4, side, 100, 1));
        check_trades(book.submit(order(5, opposite(side), 100, 3)), {
            execution(opposite(side), 5, 2, 100, 2), execution(opposite(side), 5, 4, 100, 1)});
        check_empty(book);
    }
}
TEST_CASE(sweep_stops_at_limit_with_both_sides_remaining) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), price(side, 0), 2));
        book.submit(order(2, opposite(side), price(side, 2), 7));
        check_trades(book.submit(order(3, side, price(side, 1), 5)), {
            execution(side, 3, 1, price(side, 0), 2)});
        CHECK((side == Side::Buy ? book.best_bid() : book.best_ask()) == price(side, 1));
        CHECK((side == Side::Buy ? book.best_ask() : book.best_bid()) == price(side, 2));
        CHECK(book.best_bid().value() < book.best_ask().value());
        CHECK(book.cancel(3));
        check_trades(book.submit(order(4, side, price(side, 2), 7)), {
            execution(side, 4, 2, price(side, 2), 7)});
        check_empty(book);
    }
}
TEST_CASE(exact_level_exhaustion_leaves_worse_level_untouched) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), price(side, 0), 2));
        book.submit(order(2, opposite(side), price(side, 0), 3));
        book.submit(order(3, opposite(side), price(side, 1), 7));
        check_trades(book.submit(order(4, side, price(side, 2), 5)), {
            execution(side, 4, 1, price(side, 0), 2), execution(side, 4, 2, price(side, 0), 3)});
        check_trades(book.submit(order(5, side, price(side, 1), 7)), {
            execution(side, 5, 3, price(side, 1), 7)});
        check_empty(book);
    }
}
TEST_CASE(roadmap_sweep_example_and_mirror) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), price(side, 1), 4));
        book.submit(order(2, opposite(side), price(side, 1), 6));
        book.submit(order(3, opposite(side), price(side, 2), 10));
        book.submit(order(4, opposite(side), price(side, 3), 20));
        check_trades(book.submit(order(5, side, price(side, 2), 17)), {
            execution(side, 5, 1, price(side, 1), 4), execution(side, 5, 2, price(side, 1), 6),
            execution(side, 5, 3, price(side, 2), 7)});
        CHECK(book.submit(order(6, side, price(side, 1), 1)).empty());
        CHECK(book.cancel(6));
        check_trades(book.submit(order(7, side, price(side, 3), 23)), {
            execution(side, 7, 3, price(side, 2), 3), execution(side, 7, 4, price(side, 3), 20)});
        check_empty(book);
    }
}

void invalid_quantity(Quantity quantity, bool crossing) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 3));
        book.submit(order(2, opposite(side), 100, 4));
        expect_rejection([&] { book.submit(order(3, side, price(side, crossing ? 1 : -1), quantity)); });
        CHECK(!book.cancel(3));
        check_trades(book.submit(order(4, side, 100, 7)), {
            execution(side, 4, 1, 100, 3), execution(side, 4, 2, 100, 4)});
        check_empty(book);
    }
}
TEST_CASE(zero_quantity_passive_rejected_without_mutation) { invalid_quantity(0, false); }
TEST_CASE(zero_quantity_crossing_rejected_without_mutation) { invalid_quantity(0, true); }
TEST_CASE(over_limit_quantity_passive_rejected_without_mutation) { invalid_quantity(max_quantity + 1, false); }
TEST_CASE(over_limit_quantity_crossing_rejected_without_mutation) { invalid_quantity(max_quantity + 1, true); }
TEST_CASE(maximum_unsigned_quantity_rejected_without_mutation) {
    invalid_quantity(std::numeric_limits<Quantity>::max(), false);
    invalid_quantity(std::numeric_limits<Quantity>::max(), true);
}
TEST_CASE(invalid_sides_rejected_without_mutation) {
    for (int raw : {-1, 2, 127}) {
        CheckedOrderBook book;
        book.submit(order(1, Side::Buy, 99, 3));
        book.submit(order(2, Side::Sell, 101, 4));
        expect_rejection([&] { book.submit(order(3, static_cast<Side>(raw), 100, 5)); });
        CHECK(!book.cancel(3));
        CHECK(book.best_bid() == 99);
        CHECK(book.best_ask() == 101);
        check_trades(book.submit(order(4, Side::Sell, 99, 3)), {{1, 4, 99, 3}});
        check_trades(book.submit(order(5, Side::Buy, 101, 4)), {{5, 2, 101, 4}});
        check_empty(book);
    }
}
void duplicate_order(bool other_side, bool other_price, bool partially_filled) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 5));
        if (partially_filled)
            check_trades(book.submit(order(2, opposite(side), 100, 2)), {
                execution(opposite(side), 2, 1, 100, 2)});
        const Side duplicate_side = other_side ? opposite(side) : side;
        expect_rejection([&] { book.submit(order(1, duplicate_side, other_price ? 101 : 100, 9)); });
        check_trades(book.submit(order(3, opposite(side), 100, partially_filled ? 3 : 5)), {
            execution(opposite(side), 3, 1, 100, partially_filled ? 3 : 5)});
        CHECK(!book.cancel(1));
        check_empty(book);
    }
}
TEST_CASE(duplicate_id_same_level_rejected) { duplicate_order(false, false, false); }
TEST_CASE(duplicate_id_different_level_rejected) { duplicate_order(false, true, false); }
TEST_CASE(duplicate_id_opposite_side_rejected_before_matching) { duplicate_order(true, false, false); }
TEST_CASE(duplicate_id_partially_filled_order_rejected) { duplicate_order(true, false, true); }
TEST_CASE(rejected_submission_does_not_reserve_id) {
    for (Side side : sides) {
        CheckedOrderBook book;
        expect_rejection([&] { book.submit(order(1, side, 100, 0)); });
        CHECK(book.submit(order(1, side, 100, 3)).empty());
        check_trades(book.submit(order(2, opposite(side), 100, 3)), {
            execution(opposite(side), 2, 1, 100, 3)});
        check_empty(book);
    }
}
TEST_CASE(invalid_orders_rejected_from_empty_book) {
    for (Side side : sides) {
        for (Quantity q : {Quantity{0}, max_quantity + 1, std::numeric_limits<Quantity>::max()}) {
            CheckedOrderBook book;
            expect_rejection([&] { book.submit(order(1, side, 100, q)); });
            CHECK(!book.cancel(1));
            check_empty(book);
        }
    }
    CheckedOrderBook book;
    expect_rejection([&] { book.submit(order(1, static_cast<Side>(2), 100, 1)); });
    check_empty(book);
}
TEST_CASE(reused_canceled_id_gets_new_fifo_position) {
    // IDs are unique among active orders; reusing one must not recover its old priority.
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 2));
        book.submit(order(2, opposite(side), 100, 3));
        CHECK(book.cancel(1));
        book.submit(order(1, opposite(side), 100, 4));
        check_trades(book.submit(order(3, side, 100, 7)), {
            execution(side, 3, 2, 100, 3), execution(side, 3, 1, 100, 4)});
        check_empty(book);
    }
}
TEST_CASE(fully_filled_maker_and_taker_ids_can_be_reused) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 2));
        check_trades(book.submit(order(2, side, 100, 2)), {execution(side, 2, 1, 100, 2)});
        CHECK(!book.cancel(1));
        CHECK(!book.cancel(2));
        book.submit(order(2, opposite(side), 100, 3));
        check_trades(book.submit(order(1, side, 100, 3)), {execution(side, 1, 2, 100, 3)});
        check_empty(book);
    }
}

TEST_CASE(cancel_empty_unknown_and_repeated) {
    CheckedOrderBook book;
    CHECK(!book.cancel(123));
    for (Side side : sides) {
        book.submit(order(1, side, 100, 2));
        CHECK(!book.cancel(999));
        CHECK(book.cancel(1));
        CHECK(!book.cancel(1));
        check_empty(book);
    }
}
TEST_CASE(cancel_head_middle_tail_preserves_fifo_both_sides) {
    for (Side side : sides) {
        for (OrderId removed : {1u, 2u, 3u}) {
            CheckedOrderBook book;
            for (OrderId id : {1u, 2u, 3u}) book.submit(order(id, opposite(side), 100, 2));
            CHECK(book.cancel(removed));
            CHECK(!book.cancel(removed));
            std::vector<Trade> expected;
            for (OrderId id : {1u, 2u, 3u})
                if (id != removed) expected.push_back(execution(side, 4, id, 100, 2));
            check_trades(book.submit(order(4, side, 100, 4)), expected);
            check_empty(book);
        }
    }
}
TEST_CASE(cancel_partially_filled_maker_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 5));
        book.submit(order(2, opposite(side), 100, 3));
        check_trades(book.submit(order(3, side, 100, 2)), {execution(side, 3, 1, 100, 2)});
        CHECK(book.cancel(1));
        check_trades(book.submit(order(4, side, 100, 3)), {execution(side, 4, 2, 100, 3)});
        check_empty(book);
    }
}
TEST_CASE(cancel_incoming_residual_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 3));
        check_trades(book.submit(order(2, side, 100, 5)), {execution(side, 2, 1, 100, 3)});
        CHECK(!book.cancel(1));
        CHECK(book.cancel(2));
        CHECK(!book.cancel(2));
        check_empty(book);
    }
}
TEST_CASE(cancel_nonbest_level_keeps_best_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), price(side, 0), 2));
        book.submit(order(2, opposite(side), price(side, 1), 3));
        CHECK(book.cancel(2));
        CHECK((side == Side::Buy ? book.best_ask() : book.best_bid()) == price(side, 0));
        check_trades(book.submit(order(3, side, price(side, 1), 2)), {
            execution(side, 3, 1, price(side, 0), 2)});
        check_empty(book);
    }
}
TEST_CASE(cancel_level_then_recreate_level_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 5));
        CHECK(book.cancel(1));
        book.submit(order(2, opposite(side), 100, 2));
        book.submit(order(3, opposite(side), 100, 3));
        check_trades(book.submit(order(4, side, 100, 5)), {
            execution(side, 4, 2, 100, 2), execution(side, 4, 3, 100, 3)});
        check_empty(book);
    }
}
TEST_CASE(independent_books_do_not_share_state) {
    CheckedOrderBook a, b;
    a.submit(order(1, Side::Buy, 100, 2));
    b.submit(order(1, Side::Sell, 101, 3));
    CHECK(a.cancel(1));
    check_empty(a);
    CHECK(b.best_ask() == 101);
    check_trades(b.submit(order(2, Side::Buy, 101, 3)), {{2, 1, 101, 3}});
    check_empty(b);
}
TEST_CASE(maximum_allowed_quantity_partial_fill) {
    const Quantity maximum = max_quantity;
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, maximum));
        check_trades(book.submit(order(2, side, 100, maximum - 1)), {
            execution(side, 2, 1, 100, maximum - 1)});
        check_trades(book.submit(order(3, side, 100, 1)), {execution(side, 3, 1, 100, 1)});
        check_empty(book);
    }
}
TEST_CASE(maximum_allowed_quantity_multi_fill) {
    const Quantity maximum = max_quantity;
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, maximum - 1));
        book.submit(order(2, opposite(side), 100, 1));
        check_trades(book.submit(order(3, side, 100, maximum)), {
            execution(side, 3, 1, 100, maximum - 1), execution(side, 3, 2, 100, 1)});
        check_empty(book);
    }
}
TEST_CASE(maximum_order_id_is_not_arrival_time) {
    for (Side side : sides) {
        CheckedOrderBook book;
        const auto id = std::numeric_limits<OrderId>::max();
        book.submit(order(id, opposite(side), price(side, 0), 1));
        check_trades(book.submit(order(1, side, price(side, 1), 1)), {
            execution(side, 1, id, price(side, 0), 1)});
        check_empty(book);
    }
}
void incoming_large_id_uses_resting_prices(Side side, OrderId incoming_id) {
    CHECK(incoming_id > static_cast<OrderId>(std::numeric_limits<int>::max()));
    CheckedOrderBook book;
    // Both makers arrive first, at prices different from the incoming limit.
    // Two levels also exercise the subsequent execute() call inside the loop.
    const Price first_price = side == Side::Buy ? 98 : 102;
    const Price second_price = side == Side::Buy ? 99 : 101;
    CHECK(book.submit(order(1, opposite(side), first_price, 2)).empty());
    CHECK(book.submit(order(2, opposite(side), second_price, 3)).empty());
    check_trades(book.submit(order(incoming_id, side, 100, 5)), {
        execution(side, incoming_id, 1, first_price, 2),
        execution(side, incoming_id, 2, second_price, 3)});
    check_empty(book);
    CHECK(!book.cancel(incoming_id));
    CHECK(!book.cancel(1));
    CHECK(!book.cancel(2));
}
TEST_CASE(incoming_buy_id_above_int_max_uses_resting_prices) {
    incoming_large_id_uses_resting_prices(Side::Buy,
        static_cast<OrderId>(std::numeric_limits<int>::max()) + 1);
}
TEST_CASE(incoming_sell_id_above_int_max_uses_resting_prices) {
    incoming_large_id_uses_resting_prices(Side::Sell,
        static_cast<OrderId>(std::numeric_limits<int>::max()) + 1);
}
TEST_CASE(incoming_buy_id_maximum_uses_resting_prices) {
    incoming_large_id_uses_resting_prices(Side::Buy, std::numeric_limits<OrderId>::max());
}
TEST_CASE(incoming_sell_id_maximum_uses_resting_prices) {
    incoming_large_id_uses_resting_prices(Side::Sell, std::numeric_limits<OrderId>::max());
}
TEST_CASE(incoming_buy_id_low_bits_match_maker_uses_resting_prices) {
    // On this toolchain, narrowing this ID to int yields maker ID 1.
    incoming_large_id_uses_resting_prices(Side::Buy,
        static_cast<OrderId>(std::numeric_limits<unsigned int>::max()) + 2);
}
TEST_CASE(incoming_sell_id_low_bits_match_maker_uses_resting_prices) {
    // A narrowed sell ID must not be mistaken for the resting buy's identity.
    incoming_large_id_uses_resting_prices(Side::Sell,
        static_cast<OrderId>(std::numeric_limits<unsigned int>::max()) + 2);
}
TEST_CASE(adjacent_prices_near_contract_maximum_do_not_cross) {
    CheckedOrderBook book;
    const Price p = max_price;
    CHECK(book.submit(order(1, Side::Buy, p - 1, 1)).empty());
    CHECK(book.submit(order(2, Side::Sell, p, 1)).empty());
    CHECK(book.best_bid() == p - 1);
    CHECK(book.best_ask() == p);
    check_trades(book.submit(order(3, Side::Buy, p, 1)), {{3, 2, p, 1}});
    CHECK(book.cancel(1));
    check_empty(book);
}

void check_snapshot(const Snapshot& actual, const Snapshot& expected);

// Independent reference: flat arrival-ordered vector; scan to choose the best maker.
class ReferenceBook {
    Priority next_priority = 0;
public:
    std::vector<Order> resting;
    std::vector<Trade> submit(Order incoming) {
        return process(incoming, false);
    }
    std::vector<Trade> submit_market(OrderId id, Side side, Quantity quantity) {
        return process(order(id, side, 0, quantity), true);
    }
    std::vector<Trade> process(Order incoming, bool is_market) {
        if (incoming.id == 0 || (incoming.side != Side::Buy && incoming.side != Side::Sell)
            || incoming.quantity == 0 || incoming.quantity > max_quantity
            || (!is_market && (incoming.price < 1 || incoming.price > max_price)))
            throw std::invalid_argument("Invalid reference input");
        if (std::any_of(resting.begin(), resting.end(), [&](const auto& item) {
                return item.id == incoming.id;
            }))
            throw std::invalid_argument("Duplicate reference ID");
        std::vector<Trade> result;
        while (incoming.quantity > 0) {
            auto best = resting.end();
            for (auto it = resting.begin(); it != resting.end(); ++it) {
                if (it->side == incoming.side) continue;
                const bool crosses = is_market || (incoming.side == Side::Buy ?
                    incoming.price >= it->price : incoming.price <= it->price);
                if (!crosses) continue;
                if (best == resting.end() || (incoming.side == Side::Buy ?
                    it->price < best->price : it->price > best->price)) best = it;
            }
            if (best == resting.end()) break;
            const Quantity q = std::min(incoming.quantity, best->quantity);
            result.push_back(execution(incoming.side, incoming.id, best->id, best->price, q));
            incoming.quantity -= q;
            best->quantity -= q;
            if (best->quantity == 0) resting.erase(best);
        }
        if (!is_market && incoming.quantity > 0) {
            incoming.priority = ++next_priority;
            resting.push_back(incoming);
        }
        return result;
    }
    Snapshot snapshot() const {
        auto sorted = resting;
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            if (a.side != b.side) return a.side == Side::Buy;
            return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
        });
        Snapshot result;
        for (const auto& item : sorted) {
            auto& levels = item.side == Side::Buy ? result.bids : result.asks;
            if (levels.empty() || levels.back().price != item.price)
                levels.push_back({item.price, 0, {}});
            levels.back().quantity += item.quantity; // Bounded test workloads.
            levels.back().orders.push_back(item);
        }
        return result;
    }
    Quantity cancel(OrderId id) {
        const auto it = std::find_if(resting.begin(), resting.end(),
            [id](const Order& item) { return item.id == id; });
        if (it == resting.end()) return 0;
        const Quantity q = it->quantity;
        resting.erase(it);
        return q;
    }
    std::optional<Price> best(Side side) const {
        std::optional<Price> result;
        for (const auto& item : resting) {
            if (item.side != side) continue;
            if (!result || (side == Side::Buy ? item.price > *result : item.price < *result))
                result = item.price;
        }
        return result;
    }
};
void check_observable_state(const CheckedOrderBook& actual, const ReferenceBook& expected) {
    CHECK(actual.best_bid() == expected.best(Side::Buy));
    CHECK(actual.best_ask() == expected.best(Side::Sell));
    // Compare complete state without copying the production book, so indexed
    // storage can later disable copying without weakening this oracle.
    actual.assert_invariants();
    check_snapshot(actual.snapshot(), expected.snapshot());
}
void differential(std::uint64_t seed, bool shuffled) {
    constexpr int events = 400;
    std::mt19937_64 rng(seed);
    std::vector<OrderId> ids(events);
    std::iota(ids.begin(), ids.end(), OrderId{1});
    if (shuffled) std::shuffle(ids.begin(), ids.end(), rng);
    std::vector<OrderId> submitted;
    CheckedOrderBook actual;
    ReferenceBook reference;
    Quantity accepted = 0, executed = 0, canceled = 0;
    std::size_t next = 0;
    for (int event = 0; event < events; ++event) {
        try {
            if (!submitted.empty() && rng() % 100 < 35) {
                const OrderId id = rng() % 5 == 0 ? 999'999 : submitted[rng() % submitted.size()];
                const Quantity q = reference.cancel(id);
                CHECK(actual.cancel(id) == (q > 0));
                canceled += q;
            } else {
                const auto incoming = order(ids[next++], rng() % 2 ? Side::Buy : Side::Sell,
                    95 + static_cast<Price>(rng() % 11), 1 + static_cast<Quantity>(rng() % 20));
                submitted.push_back(incoming.id);
                const auto expected = reference.submit(incoming);
                check_trades(actual.submit(incoming), expected);
                accepted += incoming.quantity;
                for (const auto& trade : expected) executed += trade.quantity;
            }
            Quantity resting = 0;
            for (const auto& item : reference.resting) {
                CHECK(item.quantity > 0);
                resting += item.quantity;
            }
            CHECK(accepted == 2 * executed + canceled + resting);
            check_observable_state(actual, reference);
        } catch (const std::exception& e) {
            throw std::runtime_error("seed=" + std::to_string(seed) +
                " event=" + std::to_string(event) + ": " + e.what());
        }
    }
}
TEST_CASE(differential_monotonic_ids_seed_42) { differential(42, false); }
TEST_CASE(differential_monotonic_ids_seed_598) { differential(598, false); }
TEST_CASE(differential_shuffled_ids_seed_42) { differential(42, true); }
TEST_CASE(differential_shuffled_ids_seed_598) { differential(598, true); }
TEST_CASE(differential_shuffled_ids_seed_2026) { differential(2026, true); }

// Future API adapters: these throw a CTest skip (77), never report a missing feature as passing.
// Only this adapter needs changing if the eventual public market API has a different shape.
template<class Book>
std::vector<Trade> market(Book& book, OrderId id, Side side, Quantity quantity) {
    if constexpr (requires { { book.submit_market(id, side, quantity) } -> std::same_as<std::vector<Trade>>; })
        return book.submit_market(id, side, quantity);
    else
        throw MissingFeature("requires submit_market(OrderId, Side, Quantity) -> vector<Trade>");
}
TEST_CASE(market_empty_book_expires_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        CHECK(market(book, 1, side, 5).empty());
        CHECK(!book.cancel(1));
        check_empty(book);
        CHECK(book.submit(order(2, opposite(side), 100, 5)).empty());
    }
}
TEST_CASE(market_does_not_match_same_side_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 3));
        CHECK(market(book, 2, side, 5).empty());
        CHECK(!book.cancel(2));
        check_trades(book.submit(order(3, opposite(side), 100, 3)), {
            execution(opposite(side), 3, 1, 100, 3)});
        check_empty(book);
    }
}
TEST_CASE(market_partial_maker_keeps_remaining_fifo) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(90, opposite(side), 100, 5));
        book.submit(order(10, opposite(side), 100, 3));
        check_trades(market(book, 1, side, 2), {execution(side, 1, 90, 100, 2)});
        CHECK(!book.cancel(1));
        check_trades(market(book, 2, side, 6), {
            execution(side, 2, 90, 100, 3), execution(side, 2, 10, 100, 3)});
        check_empty(book);
    }
}
TEST_CASE(market_exact_liquidity_removes_all_levels) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(90, opposite(side), price(side, 0), 2));
        book.submit(order(70, opposite(side), price(side, 2), 3));
        check_trades(market(book, 1, side, 5), {
            execution(side, 1, 90, price(side, 0), 2), execution(side, 1, 70, price(side, 2), 3)});
        CHECK(!book.cancel(1));
        CHECK(!book.cancel(90));
        CHECK(!book.cancel(70));
        check_empty(book);
    }
}
TEST_CASE(market_sweeps_fifo_and_prices_then_expires_residual) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(90, opposite(side), price(side, 2), 4));
        book.submit(order(70, opposite(side), price(side, 0), 2));
        book.submit(order(10, opposite(side), price(side, 0), 3));
        check_trades(market(book, 1, side, 12), {
            execution(side, 1, 70, price(side, 0), 2), execution(side, 1, 10, price(side, 0), 3),
            execution(side, 1, 90, price(side, 2), 4)});
        // 12 = 9 executed + 3 expired; later liquidity cannot fill the expired residual.
        CHECK(!book.cancel(1));
        check_empty(book);
        CHECK(book.submit(order(2, opposite(side), 100, 3)).empty());
        CHECK(book.cancel(2));
        check_empty(book);
    }
}
TEST_CASE(market_invalid_quantity_rejected_without_mutation) {
    for (Side side : sides) {
        for (Quantity q : {Quantity{0}, max_quantity + 1, std::numeric_limits<Quantity>::max()}) {
            CheckedOrderBook book;
            book.submit(order(1, opposite(side), 100, 5));
            expect_rejection([&] { market(book, 2, side, q); });
            check_trades(market(book, 3, side, 5), {execution(side, 3, 1, 100, 5)});
            check_empty(book);
        }
    }
}
TEST_CASE(market_duplicate_active_id_rejected_before_matching) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 5));
        expect_rejection([&] { market(book, 1, side, 3); });
        check_trades(market(book, 2, side, 5), {execution(side, 2, 1, 100, 5)});
        check_empty(book);
    }
}
TEST_CASE(market_invalid_side_rejected_without_mutation) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Sell, 100, 5));
    expect_rejection([&] { market(book, 2, static_cast<Side>(2), 3); });
    check_trades(market(book, 3, Side::Buy, 5), {{3, 1, 100, 5}});
    check_empty(book);
}
TEST_CASE(market_expired_id_can_be_reused) {
    for (Side side : sides) {
        CheckedOrderBook book;
        CHECK(market(book, 1, side, 5).empty());
        CHECK(book.submit(order(1, side, 100, 3)).empty());
        check_trades(market(book, 2, opposite(side), 3), {
            execution(opposite(side), 2, 1, 100, 3)});
        check_empty(book);
    }
}

using Levels = std::vector<std::pair<Price, Quantity>>;
using Depth = std::pair<Levels, Levels>;

void check_depth(const Depth& actual, const Depth& expected) {
    const auto check_levels = [](const Levels& a, const Levels& e) {
        CHECK(a.size() == e.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a.at(i).first == e.at(i).first);
            CHECK(a.at(i).second == e.at(i).second);
        }
    };
    check_levels(actual.first, expected.first);
    check_levels(actual.second, expected.second);
}

// Compare complete owned snapshots, including priorities, without adding operators
// or inspection hooks to production types just for the tests.
void check_snapshot(const Snapshot& actual, const Snapshot& expected) {
    const auto check_levels = [](const auto& actual_levels, const auto& expected_levels) {
        CHECK(actual_levels.size() == expected_levels.size());
        for (std::size_t i = 0; i < actual_levels.size(); ++i) {
            const auto& a = actual_levels[i];
            const auto& e = expected_levels[i];
            CHECK(a.price == e.price);
            CHECK(a.quantity == e.quantity);
            CHECK(a.orders.size() == e.orders.size());
            for (std::size_t j = 0; j < a.orders.size(); ++j) {
                CHECK(a.orders[j].id == e.orders[j].id);
                CHECK(a.orders[j].side == e.orders[j].side);
                CHECK(a.orders[j].price == e.orders[j].price);
                CHECK(a.orders[j].quantity == e.orders[j].quantity);
                CHECK(a.orders[j].priority == e.orders[j].priority);
            }
        }
    };
    check_levels(actual.bids, expected.bids);
    check_levels(actual.asks, expected.asks);
}

TEST_CASE(cancel_unknown_id_preserves_complete_snapshot) {
    CheckedOrderBook book;
    CHECK(!book.cancel(999));
    check_snapshot(book.snapshot(), Snapshot{});

    // Both sides, multiple levels, and FIFO order different from numeric ID order.
    book.submit(order(90, Side::Buy, 100, 4));
    book.submit(order(10, Side::Buy, 100, 6));
    book.submit(order(70, Side::Buy, 99, 8));
    book.submit(order(80, Side::Sell, 102, 3));
    book.submit(order(20, Side::Sell, 102, 5));
    book.submit(order(60, Side::Sell, 103, 7));
    const auto before = book.snapshot();

    for (OrderId missing : {OrderId{0}, OrderId{999}, std::numeric_limits<OrderId>::max()}) {
        CHECK(!book.cancel(missing));
        check_snapshot(book.snapshot(), before);
        CHECK(book.best_bid() == 100);
        CHECK(book.best_ask() == 102);
    }
}

TEST_CASE(cancel_preserves_survivor_priorities_and_fifo) {
    for (Side side : sides) {
        const Side taker = opposite(side);
        struct Scenario {
            OrderId canceled;
            Quantity remaining;
            std::vector<Order> survivors;
            std::vector<Trade> expected_trades;
        };
        const std::vector<Scenario> scenarios = {
            {90, 14, {{10, side, 100, 6, 2}, {70, side, 100, 8, 3}},
                {execution(taker, 1000, 10, 100, 6), execution(taker, 1000, 70, 100, 8)}},
            {10, 12, {{90, side, 100, 4, 1}, {70, side, 100, 8, 3}},
                {execution(taker, 1000, 90, 100, 4), execution(taker, 1000, 70, 100, 8)}},
            {70, 10, {{90, side, 100, 4, 1}, {10, side, 100, 6, 2}},
                {execution(taker, 1000, 90, 100, 4), execution(taker, 1000, 10, 100, 6)}}};

        for (const auto& scenario : scenarios) {
            CheckedOrderBook book;
            book.submit(order(90, side, 100, 4));
            book.submit(order(10, side, 100, 6));
            book.submit(order(70, side, 100, 8));
            const Price worse = side == Side::Buy ? 99 : 101;
            const Price other = side == Side::Buy ? 102 : 98;
            book.submit(order(40, side, worse, 9));
            book.submit(order(50, taker, other, 11));

            // Literal survivor quantities/priorities: cancellation must not
            // renumber priorities, reorder IDs, or touch any other level.
            Snapshot expected;
            (side == Side::Buy ? expected.bids : expected.asks) = {
                {100, scenario.remaining, scenario.survivors},
                {worse, 9, {{40, side, worse, 9, 4}}}};
            (side == Side::Buy ? expected.asks : expected.bids) = {
                {other, 11, {{50, taker, other, 11, 5}}}};

            CHECK(book.cancel(scenario.canceled));
            check_snapshot(book.snapshot(), expected);
            CHECK(!book.cancel(scenario.canceled));
            check_snapshot(book.snapshot(), expected);

            // Verify actual execution order too, not just snapshot presentation.
            check_trades(book.submit(order(1000, taker, 100, scenario.remaining)),
                scenario.expected_trades);
            (side == Side::Buy ? expected.bids : expected.asks) = {
                {worse, 9, {{40, side, worse, 9, 4}}}};
            check_snapshot(book.snapshot(), expected);
        }
    }
}

TEST_CASE(lesson3_workbook_passive_snapshot) {
    CheckedOrderBook book;
    check_snapshot(book.snapshot(), Snapshot{});
    check_empty(book);
    CHECK(book.submit(order(1, Side::Buy, 100, 10)).empty());
    CHECK(book.submit(order(2, Side::Buy, 100, 20)).empty());
    CHECK(book.submit(order(3, Side::Buy, 99, 40)).empty());
    CHECK(book.submit(order(4, Side::Sell, 102, 10)).empty());
    Snapshot expected;
    expected.bids = {
        {100, 30, {{1, Side::Buy, 100, 10, 1}, {2, Side::Buy, 100, 20, 2}}},
        {99, 40, {{3, Side::Buy, 99, 40, 3}}}};
    expected.asks = {{102, 10, {{4, Side::Sell, 102, 10, 4}}}};
    CHECK(book.best_bid() == 100);
    CHECK(book.best_ask() == 102);
    check_snapshot(book.snapshot(), expected);
    check_snapshot(book.snapshot(), expected);
}

TEST_CASE(lesson3_snapshot_price_order_fifo_and_assigned_priority) {
    for (Side side : sides) {
        CheckedOrderBook book;
        // User-supplied priorities must not choose arrival order.
        CHECK(book.submit({90, side, 100, 2, 999}).empty());
        CHECK(book.submit({10, side, 100, 3, 1}).empty());
        const Price better = side == Side::Buy ? 101 : 99;
        CHECK(book.submit({70, side, better, 4, 0}).empty());
        Snapshot expected;
        (side == Side::Buy ? expected.bids : expected.asks) = {
            {better, 4, {{70, side, better, 4, 3}}},
            {100, 5, {{90, side, 100, 2, 1}, {10, side, 100, 3, 2}}}};
        check_snapshot(book.snapshot(), expected);
    }
}

TEST_CASE(lesson3_snapshot_owns_independent_copies) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Buy, 100, 5));
    book.submit(order(2, Side::Sell, 102, 7));
    Snapshot expected;
    expected.bids = {{100, 5, {{1, Side::Buy, 100, 5, 1}}}};
    expected.asks = {{102, 7, {{2, Side::Sell, 102, 7, 2}}}};
    const auto saved = book.snapshot();
    auto edited = book.snapshot();
    edited.bids[0].orders[0].quantity = 999;
    edited.asks.clear();
    check_snapshot(book.snapshot(), expected);
    CHECK(book.cancel(1));
    book.submit(order(3, Side::Buy, 99, 2));
    check_snapshot(saved, expected);
    expected.bids = {{99, 2, {{3, Side::Buy, 99, 2, 3}}}};
    check_snapshot(book.snapshot(), expected);
}

TEST_CASE(lesson3_rejections_preserve_orders_totals_and_priority) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, Side::Buy, 99, 3));
        book.submit(order(2, Side::Sell, 101, 4));
        const auto before = book.snapshot();
        const Price crossing = side == Side::Buy ? 102 : 98;
        const std::vector<Order> invalid = {
            order(0, side, crossing, 1),
            order(3, static_cast<Side>(-1), 100, 1),
            order(3, static_cast<Side>(2), 100, 1),
            order(3, side, -1, 1),
            order(3, side, 0, 1),
            order(3, side, max_price + 1, 1),
            order(3, side, std::numeric_limits<Price>::lowest(), 1),
            order(3, side, std::numeric_limits<Price>::max(), 1),
            order(3, side, crossing, 0),
            order(3, side, crossing, max_quantity + 1),
            order(3, side, crossing, std::numeric_limits<Quantity>::max()),
            order(1, side, crossing, 1),
            order(2, side, crossing, 1)};
        for (const auto& item : invalid) {
            expect_rejection([&] { book.submit(item); });
            check_snapshot(book.snapshot(), before);
        }
        // A rejected command must not reserve ID 3 or consume arrival priority.
        CHECK(book.submit(order(3, Side::Buy, 98, 2)).empty());
        auto expected = before;
        expected.bids.push_back({98, 2, {{3, Side::Buy, 98, 2, 3}}});
        check_snapshot(book.snapshot(), expected);
    }
}

TEST_CASE(lesson3_valid_price_and_quantity_boundaries) {
    for (Side side : sides) {
        for (Price p : {Price{1}, max_price}) {
            for (Quantity q : {Quantity{1}, max_quantity}) {
                CheckedOrderBook book;
                CHECK(book.submit(order(1, side, p, q)).empty());
                Snapshot expected;
                (side == Side::Buy ? expected.bids : expected.asks) = {
                    {p, q, {{1, side, p, q, 1}}}};
                check_snapshot(book.snapshot(), expected);
                check_trades(book.submit(order(2, opposite(side), p, q)), {
                    execution(opposite(side), 2, 1, p, q)});
                check_snapshot(book.snapshot(), Snapshot{});
            }
        }
    }
}

TEST_CASE(lesson3_depth_can_exceed_individual_quantity_limit) {
    for (Side side : sides) {
        CheckedOrderBook book;
        for (OrderId id = 1; id <= 5; ++id)
            CHECK(book.submit(order(id, side, 100, max_quantity)).empty());
        const auto snapshot = book.snapshot();
        const auto& levels = side == Side::Buy ? snapshot.bids : snapshot.asks;
        CHECK(levels.size() == 1);
        CHECK(levels[0].quantity == 5'000'000'000ULL);
        CHECK(levels[0].orders.size() == 5);
    }
}

TEST_CASE(lesson3_partial_fill_and_reuse_preserve_arrival_order) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(90, side, 100, 5));
        book.submit(order(10, side, 100, 3));
        check_trades(book.submit(order(20, opposite(side), 100, 2)), {
            execution(opposite(side), 20, 90, 100, 2)});
        auto snapshot = book.snapshot();
        const auto& levels = side == Side::Buy ? snapshot.bids : snapshot.asks;
        CHECK(levels.size() == 1);
        CHECK(levels[0].quantity == 6);
        CHECK(levels[0].orders.size() == 2);
        CHECK(levels[0].orders[0].id == 90);
        CHECK(levels[0].orders[0].quantity == 3);
        CHECK(levels[0].orders[0].priority == 1);
        CHECK(levels[0].orders[1].id == 10);
        CHECK(levels[0].orders[1].priority == 2);
        CHECK(book.cancel(90));
        CHECK(book.submit(order(90, side, 100, 1)).empty());
        snapshot = book.snapshot();
        const auto& remaining = side == Side::Buy ? snapshot.bids : snapshot.asks;
        CHECK(remaining.size() == 1);
        CHECK(remaining[0].orders.size() == 2);
        CHECK(remaining[0].orders[0].id == 10);
        CHECK(remaining[0].orders[0].priority == 2);
        CHECK(remaining[0].orders[1].id == 90);
        CHECK(remaining[0].orders[1].priority > 2);
    }
}

template<class Book>
Depth depth(const Book& book) {
    if constexpr (requires { book.snapshot().bids; book.snapshot().asks; }) {
        const auto snapshot = book.snapshot();
        Depth result;
        for (const auto& level : snapshot.bids) result.first.emplace_back(level.price, level.quantity);
        for (const auto& level : snapshot.asks) result.second.emplace_back(level.price, level.quantity);
        return result;
    } else {
        throw MissingFeature("requires const snapshot() with bids/asks levels containing price/quantity");
    }
}
TEST_CASE(snapshot_empty_book) {
    const CheckedOrderBook book;
    check_depth(depth(book), Depth{});
}
TEST_CASE(snapshot_aggregates_and_sorts_both_sides) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Buy, 98, 2));
    book.submit(order(2, Side::Buy, 99, 3));
    book.submit(order(3, Side::Buy, 99, 4));
    book.submit(order(4, Side::Sell, 102, 5));
    book.submit(order(5, Side::Sell, 101, 6));
    book.submit(order(6, Side::Sell, 101, 7));
    const Depth expected{{{99, 7}, {98, 2}}, {{101, 13}, {102, 5}}};
    check_depth(depth(book), expected);
    check_depth(depth(book), expected); // Repeated queries must not consume or reorder orders.
    check_trades(book.submit(order(7, Side::Buy, 101, 13)), {{7, 5, 101, 6}, {7, 6, 101, 7}});
    check_trades(book.submit(order(8, Side::Sell, 99, 7)), {{2, 8, 99, 3}, {3, 8, 99, 4}});
}
TEST_CASE(snapshot_updates_after_partial_fill_cancel_and_removal) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Sell, 100, 5));
    book.submit(order(2, Side::Sell, 100, 4));
    book.submit(order(3, Side::Sell, 101, 2));
    const Depth original{{}, {{100, 9}, {101, 2}}};
    check_depth(depth(book), original);
    book.submit(order(4, Side::Buy, 100, 3));
    const Depth partial{{}, {{100, 6}, {101, 2}}};
    check_depth(depth(book), partial);
    CHECK(book.cancel(2));
    const Depth canceled{{}, {{100, 2}, {101, 2}}};
    check_depth(depth(book), canceled);
    book.submit(order(5, Side::Buy, 100, 2));
    const Depth removed{{}, {{101, 2}}};
    check_depth(depth(book), removed);
    CHECK(book.cancel(3));
    check_depth(depth(book), Depth{});
}
TEST_CASE(snapshot_reports_incoming_limit_residual_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 2));
        book.submit(order(2, side, price(side, 1), 5));
        Depth expected;
        (side == Side::Buy ? expected.first : expected.second).emplace_back(price(side, 1), 3);
        check_depth(depth(book), expected);
    }
}
TEST_CASE(snapshot_updates_bid_depth_after_fill_and_cancel) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Buy, 100, 5));
    book.submit(order(2, Side::Buy, 100, 4));
    book.submit(order(3, Side::Buy, 99, 2));
    const Depth initial{{{100, 9}, {99, 2}}, {}};
    check_depth(depth(book), initial);
    book.submit(order(4, Side::Sell, 100, 3));
    const Depth partial{{{100, 6}, {99, 2}}, {}};
    check_depth(depth(book), partial);
    CHECK(book.cancel(2));
    const Depth canceled{{{100, 2}, {99, 2}}, {}};
    check_depth(depth(book), canceled);
    book.submit(order(5, Side::Sell, 100, 2));
    const Depth removed{{{99, 2}}, {}};
    check_depth(depth(book), removed);
    CHECK(book.cancel(3));
    check_depth(depth(book), Depth{});
}
TEST_CASE(snapshot_market_residual_never_appears) {
    for (Side side : sides) {
        CheckedOrderBook book;
        check_depth(depth(book), Depth{});
        book.submit(order(1, opposite(side), 100, 3));
        check_trades(market(book, 2, side, 5), {execution(side, 2, 1, 100, 3)});
        check_depth(depth(book), Depth{});
        CHECK(!book.cancel(2));
    }
}
TEST_CASE(snapshot_matches_reference_after_every_command) {
    CheckedOrderBook book;
    ReferenceBook reference;
    check_depth(depth(book), Depth{}); // Detect missing API before any other test failure.
    std::mt19937_64 rng(491);
    for (OrderId id = 1; id <= 300; ++id) {
        if (id % 3 == 0) {
            const OrderId target = 1 + rng() % id;
            CHECK(book.cancel(target) == (reference.cancel(target) > 0));
        } else {
            auto item = order(id, rng() % 2 ? Side::Buy : Side::Sell,
                95 + static_cast<Price>(rng() % 11), 1 + static_cast<Quantity>(rng() % 10));
            check_trades(book.submit(item), reference.submit(item));
        }
        Depth expected;
        for (const auto& item : reference.resting) {
            auto& levels = item.side == Side::Buy ? expected.first : expected.second;
            auto it = std::find_if(levels.begin(), levels.end(),
                [&](const auto& level) { return level.first == item.price; });
            if (it == levels.end()) levels.emplace_back(item.price, item.quantity);
            else it->second += item.quantity;
        }
        std::sort(expected.first.begin(), expected.first.end(), std::greater<>{});
        std::sort(expected.second.begin(), expected.second.end());
        check_depth(depth(book), expected);
    }
}

enum class Action { Limit, Market, Cancel };
struct Command {
    Action action;
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};
struct CommandResult {
    bool rejected = false;
    bool canceled = false;
    std::vector<Trade> trades;
};
template<class Book>
CommandResult run_command(Book& book, const Command& command) {
    CommandResult result;
    try {
        switch (command.action) {
        case Action::Limit:
            result.trades = book.submit(order(command.id, command.side, command.price, command.quantity));
            break;
        case Action::Market:
            result.trades = book.submit_market(command.id, command.side, command.quantity);
            break;
        case Action::Cancel:
            result.canceled = book.cancel(command.id) != 0;
            break;
        }
    } catch (const std::invalid_argument&) {
        result.rejected = true;
    }
    return result;
}
CommandResult compare_command(CheckedOrderBook& book, ReferenceBook& reference,
    const Command& command, std::size_t step) {
    try {
        const auto before = book.snapshot();
        const auto expected = run_command(reference, command);
        const auto actual = run_command(book, command);
        CHECK(actual.rejected == expected.rejected);
        CHECK(actual.canceled == expected.canceled);
        check_trades(actual.trades, expected.trades);
        check_observable_state(book, reference);
        if (actual.rejected || (command.action == Action::Cancel && !actual.canceled))
            check_snapshot(book.snapshot(), before);
        return actual;
    } catch (const std::exception& error) {
        throw std::runtime_error("step=" + std::to_string(step)
            + " action=" + std::to_string(static_cast<int>(command.action))
            + " id=" + std::to_string(command.id)
            + " side=" + std::to_string(static_cast<int>(command.side))
            + " price=" + std::to_string(command.price)
            + " qty=" + std::to_string(command.quantity) + ": " + error.what());
    }
}

TEST_CASE(lesson6_saved_mixed_command_fixture) {
    // Retain these concrete inputs for L11 replay. Cancel ignores side/price/
    // quantity; Market ignores price. Expected results are checked below.
    constexpr std::array<Command, 20> commands{{
        {Action::Limit, 1, Side::Buy, 99, 4},
        {Action::Limit, 2, Side::Buy, 99, 6},
        {Action::Limit, 3, Side::Buy, 98, 8},
        {Action::Limit, 4, Side::Sell, 101, 3},
        {Action::Limit, 5, Side::Sell, 102, 5},
        {Action::Limit, 2, Side::Sell, 98, 2}, // Reject duplicate before crossing.
        {Action::Limit, 6, Side::Buy, 102, 5}, // Sweep leaving a partial maker.
        {Action::Cancel, 1, Side::Buy, 0, 0},
        {Action::Market, 7, Side::Sell, 0, 5},
        {Action::Cancel, 2, Side::Buy, 0, 0},
        {Action::Limit, 2, Side::Buy, 98, 2}, // Reused ID joins behind ID 3.
        {Action::Market, 8, Side::Sell, 0, 20}, // Ten units expire.
        {Action::Cancel, 8, Side::Buy, 0, 0}, // Expired order never rested.
        {Action::Limit, 8, Side::Sell, 103, 1},
        {Action::Cancel, 999, Side::Buy, 0, 0},
        {Action::Limit, 9, Side::Buy, 100, 0},
        {Action::Market, 0, Side::Buy, 0, 1},
        {Action::Market, 10, Side::Buy, 0, 10}, // Six expire; book empties.
        {Action::Limit, 1, Side::Buy, 100, 1},
        {Action::Cancel, 1, Side::Buy, 0, 0}
    }};
    CheckedOrderBook book;
    ReferenceBook reference;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto result = compare_command(book, reference, commands[i], i);
        CHECK(result.rejected == (i == 5 || i == 15 || i == 16));
        CHECK(result.canceled == (i == 7 || i == 9 || i == 19));
        std::vector<Trade> expected;
        if (i == 6) expected = {{6, 4, 101, 3}, {6, 5, 102, 2}};
        if (i == 8) expected = {{2, 7, 99, 5}};
        if (i == 11) expected = {{3, 8, 98, 8}, {2, 8, 98, 2}};
        if (i == 17) expected = {{10, 5, 102, 3}, {10, 8, 103, 1}};
        check_trades(result.trades, expected);
    }
    check_snapshot(book.snapshot(), Snapshot{});
}

TEST_CASE(lesson6_workbook_cancel_then_partial_fill) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 4));
        book.submit(order(2, side, 100, 6));
        book.submit(order(3, side, 100, 8));
        CHECK(book.cancel(2));
        check_trades(book.submit(order(4, opposite(side), 100, 5)), {
            execution(opposite(side), 4, 1, 100, 4), execution(opposite(side), 4, 3, 100, 1)});
        Snapshot expected;
        (side == Side::Buy ? expected.bids : expected.asks) = {
            {100, 7, {{3, side, 100, 7, 3}}}};
        check_snapshot(book.snapshot(), expected);
        CHECK(!book.cancel(2));
        check_snapshot(book.snapshot(), expected);
    }
}

TEST_CASE(lesson6_market_zero_id_rejects_before_matching) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, opposite(side), 100, 4));
        const auto before = book.snapshot();
        expect_rejection([&] { book.submit_market(0, side, 4); });
        check_snapshot(book.snapshot(), before);
    }
}

TEST_CASE(lesson6_fills_and_expiration_do_not_consume_arrival_priority) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 2));
        book.submit(order(2, opposite(side), 100, 2)); // Full execution, never rests.
        book.submit_market(3, side, 4); // Empty-book expiration.
        CHECK(!book.cancel(999));
        expect_rejection([&] { book.submit(order(4, side, 100, 0)); });
        book.submit(order(4, side, 100, 3));
        Snapshot expected;
        (side == Side::Buy ? expected.bids : expected.asks) = {
            {100, 3, {{4, side, 100, 3, 2}}}};
        check_snapshot(book.snapshot(), expected);
    }
}

TEST_CASE(lesson6_repeated_level_recreation_and_id_reuse) {
    for (Side side : sides) {
        CheckedOrderBook book;
        for (int cycle = 0; cycle < 100; ++cycle) {
            book.submit(order(1, side, 100, 3));
            book.submit(order(2, side, 100, 4));
            if (cycle % 2 == 0) {
                CHECK(book.cancel(1));
                CHECK(book.cancel(2));
            } else {
                check_trades(book.submit_market(3, opposite(side), 10), {
                    execution(opposite(side), 3, 1, 100, 3),
                    execution(opposite(side), 3, 2, 100, 4)});
            }
            CHECK(!book.cancel(1));
            CHECK(!book.cancel(2));
            CHECK(!book.cancel(3));
            check_snapshot(book.snapshot(), Snapshot{});
        }
    }
}

void mixed_differential(std::uint64_t seed) {
    CheckedOrderBook book;
    ReferenceBook reference;
    std::mt19937_64 rng(seed);
    for (std::size_t step = 0; step < 1000; ++step) {
        Command command{static_cast<Action>(rng() % 3), 1 + rng() % 20,
            rng() % 2 ? Side::Buy : Side::Sell,
            97 + static_cast<Price>(rng() % 7), 1 + rng() % 12};
        switch (rng() % 16) {
        case 0: command.id = 0; break;
        case 1: command.id = std::numeric_limits<OrderId>::max(); break;
        case 2: command.side = static_cast<Side>(2); break;
        case 3: command.quantity = 0; break;
        case 4: command.quantity = max_quantity + 1; break;
        case 5: command.price = 0; break;
        case 6: command.price = max_price + 1; break;
        default: break;
        }
        try { compare_command(book, reference, command, step); }
        catch (const std::exception& error) {
            throw std::runtime_error("seed=" + std::to_string(seed) + " " + error.what());
        }
    }
}
TEST_CASE(lesson6_mixed_differential_seed_6) { mixed_differential(6); }
TEST_CASE(lesson6_mixed_differential_seed_491) { mixed_differential(491); }
TEST_CASE(lesson6_mixed_differential_seed_2026) { mixed_differential(2026); }

TEST_CASE(lesson6_exhaustive_short_command_sequences) {
    // Enumerate every length-1 through length-4 sequence over this bounded
    // alphabet: 11,110 traces, including duplicate/reused IDs and invalid inputs.
    const std::vector<Command> alphabet = {
        {Action::Limit, 1, Side::Buy, 99, 2},
        {Action::Limit, 2, Side::Buy, 99, 1},
        {Action::Limit, 1, Side::Sell, 99, 1},
        {Action::Limit, 2, Side::Sell, 100, 2},
        {Action::Limit, 3, Side::Buy, 100, 3},
        {Action::Market, 3, Side::Buy, 0, 4},
        {Action::Market, 3, Side::Sell, 0, 4},
        {Action::Cancel, 1, Side::Buy, 0, 0},
        {Action::Cancel, 2, Side::Buy, 0, 0},
        {Action::Limit, 3, Side::Buy, 100, 0}};
    std::size_t count = 1;
    for (std::size_t length = 1; length <= 4; ++length) {
        count *= alphabet.size();
        for (std::size_t trace = 0; trace < count; ++trace) {
            CheckedOrderBook book;
            ReferenceBook reference;
            auto digits = trace;
            for (std::size_t step = 0; step < length; ++step) {
                const auto& command = alphabet[digits % alphabet.size()];
                digits /= alphabet.size();
                try { compare_command(book, reference, command, step); }
                catch (const std::exception& error) {
                    throw std::runtime_error("length=" + std::to_string(length)
                        + " trace=" + std::to_string(trace) + " " + error.what());
                }
            }
        }
    }
}

template<class Action>
void expect_logic_error(Action action, std::string_view message) {
    bool caught = false;
    try { action(); }
    catch (const std::logic_error& error) {
        caught = true;
        CHECK(std::string_view(error.what()) == message);
    }
    CHECK(caught);
}

template<class Corrupt>
void invalid_book_both_sides(Corrupt corrupt, std::string_view message) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 5));
        book.submit(order(2, side, 100, 7));
        corrupt(book, side);
        expect_logic_error([&] { book.assert_invariants(); }, message);
    }
}

TEST_CASE(invariants_reject_empty_levels) {
    invalid_book_both_sides([](auto& book, Side side) {
        OrderBookTestAccess::orders(book, side, 100).clear();
    }, "Empty price level");
}
TEST_CASE(invariants_reject_zero_resting_id) {
    invalid_book_both_sides([](auto& book, Side side) {
        OrderBookTestAccess::order(book, side, 100).id = 0;
    }, "Zero resting ID");
}
TEST_CASE(invariants_reject_invalid_resting_prices) {
    for (Price p : {Price{-1}, Price{0}, max_price + 1,
            std::numeric_limits<Price>::lowest(), std::numeric_limits<Price>::max()}) {
        invalid_book_both_sides([p](auto& book, Side side) {
            OrderBookTestAccess::order(book, side, 100).price = p;
        }, "Resting price out of range");
    }
}
TEST_CASE(invariants_reject_invalid_remaining_quantities) {
    for (Quantity q : {Quantity{0}, max_quantity + 1, std::numeric_limits<Quantity>::max()}) {
        invalid_book_both_sides([q](auto& book, Side side) {
            OrderBookTestAccess::order(book, side, 100).quantity = q;
        }, "Resting quantity out of range");
    }
}
TEST_CASE(invariants_reject_wrong_side_and_invalid_enum) {
    for (bool invalid_enum : {false, true}) {
        invalid_book_both_sides([invalid_enum](auto& book, Side side) {
            OrderBookTestAccess::order(book, side, 100).side =
                invalid_enum ? static_cast<Side>(9) : opposite(side);
        }, "Resting order on wrong side");
    }
}
TEST_CASE(invariants_reject_order_at_wrong_level) {
    invalid_book_both_sides([](auto& book, Side side) {
        OrderBookTestAccess::order(book, side, 100).price = 99;
    }, "Resting order at wrong price level");
}
TEST_CASE(invariants_reject_duplicate_ids_within_level) {
    invalid_book_both_sides([](auto& book, Side side) {
        OrderBookTestAccess::order(book, side, 100, 1).id = 1;
    }, "Duplicate resting ID");
}
TEST_CASE(invariants_reject_duplicate_ids_across_levels) {
    invalid_book_both_sides([](auto& book, Side side) {
        book.submit(order(3, side, 99, 2));
        OrderBookTestAccess::order(book, side, 99).id = 1;
    }, "Duplicate resting ID");
}
TEST_CASE(invariants_reject_duplicate_ids_across_sides) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Buy, 99, 3));
    book.submit(order(2, Side::Sell, 101, 4));
    OrderBookTestAccess::order(book, Side::Sell, 101).id = 1;
    expect_logic_error([&] { book.assert_invariants(); }, "Duplicate resting ID");
}
TEST_CASE(invariants_reject_missing_active_ids) {
    invalid_book_both_sides([](auto& book, Side) {
        OrderBookTestAccess::ids(book).erase(1);
    }, "Resting ID missing from active index");
}
TEST_CASE(invariants_reject_stale_active_ids) {
    CheckedOrderBook empty;
    // No order owns this extra key. The checker rejects the key-set mismatch
    // without dereferencing the placeholder location.
    OrderBookTestAccess::ids(empty).emplace(999, Location{});
    expect_logic_error([&] { empty.assert_invariants(); }, "Active ID index disagrees with book");
    invalid_book_both_sides([](auto& book, Side) {
        auto& active = OrderBookTestAccess::ids(book);
        // Valid iterators, but an extra key with no corresponding resting ID.
        const Location existing = active.at(1);
        active.emplace(999, existing);
    }, "Active ID index disagrees with book");
}
TEST_CASE(invariants_reject_zero_priority) {
    invalid_book_both_sides([](auto& book, Side side) {
        OrderBookTestAccess::order(book, side, 100).priority = 0;
    }, "Zero arrival priority");
}
TEST_CASE(invariants_reject_equal_or_reversed_fifo_priorities) {
    for (bool reversed : {false, true}) {
        invalid_book_both_sides([reversed](auto& book, Side side) {
            OrderBookTestAccess::order(book, side, 100).priority = reversed ? 2 : 1;
            OrderBookTestAccess::order(book, side, 100, 1).priority = 1;
        }, "FIFO priorities not increasing");
    }
}
TEST_CASE(invariants_reject_priority_beyond_counter) {
    invalid_book_both_sides([](auto& book, Side side) {
        OrderBookTestAccess::order(book, side, 100, 1).priority = 3;
    }, "Arrival priority exceeds counter");
}
TEST_CASE(invariants_reject_locked_and_crossed_books) {
    for (Price ask_price : {Price{99}, Price{100}}) {
        CheckedOrderBook book;
        book.submit(order(1, Side::Buy, 100, 2));
        book.submit(order(2, Side::Sell, 101, 3));
        OrderBookTestAccess::move_ask_level(book, 101, ask_price);
        expect_logic_error([&] { book.assert_invariants(); }, "Crossed resting book");
    }
}
TEST_CASE(invariants_checked_add_accepts_exact_boundary) {
    const Quantity maximum = std::numeric_limits<Quantity>::max();
    CHECK(OrderBookTestAccess::add(0, 0) == 0);
    CHECK(OrderBookTestAccess::add(0, maximum) == maximum);
    CHECK(OrderBookTestAccess::add(maximum, 0) == maximum);
    CHECK(OrderBookTestAccess::add(maximum - 1, 1) == maximum);
    CHECK(OrderBookTestAccess::add(maximum - max_quantity, max_quantity) == maximum);
    CHECK(OrderBookTestAccess::add(4'000'000'000ULL, max_quantity) == 5'000'000'000ULL);
}
TEST_CASE(invariants_checked_add_rejects_overflow) {
    // Legal billion-unit orders would require billions of nodes to overflow.
    // Exercise the exact checked-add helper used by the invariant checker.
    const Quantity maximum = std::numeric_limits<Quantity>::max();
    for (const auto& values : std::vector<std::pair<Quantity, Quantity>>{
            {maximum, 1}, {maximum - 1, 2}, {maximum, maximum},
            {maximum - max_quantity + 1, max_quantity}}) {
        expect_logic_error([&] { OrderBookTestAccess::add(values.first, values.second); },
            "Price level quantity overflow");
    }
}
TEST_CASE(invariants_accept_priority_gaps_and_are_read_only) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(90, side, 100, 2));
        book.submit(order(10, side, 100, 3));
        book.submit(order(70, side, 100, 4));
        CHECK(book.cancel(10));
        const auto before = book.snapshot();
        for (int i = 0; i < 3; ++i) book.assert_invariants();
        check_snapshot(book.snapshot(), before);
        book.submit(order(50, side, 100, 1));
        const auto after = book.snapshot();
        const auto& levels = side == Side::Buy ? after.bids : after.asks;
        CHECK(levels[0].orders.back().priority == 4);
    }
}
TEST_CASE(invariants_accept_latest_and_maximum_priority) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 1));
        const auto maximum = std::numeric_limits<Priority>::max();
        OrderBookTestAccess::order(book, side, 100).priority = maximum;
        OrderBookTestAccess::counter(book) = maximum;
        book.assert_invariants();
    }
}

template<class Book>
void invariants(const Book& book) {
    if constexpr (requires { book.assert_invariants(); }) book.assert_invariants();
    else throw MissingFeature("requires public const assert_invariants(); current method is private");
}
TEST_CASE(invariants_accept_empty_book) { invariants(CheckedOrderBook{}); }
TEST_CASE(invariants_accept_buy_only_book) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Buy, 100, 2));
    invariants(book);
}
TEST_CASE(invariants_accept_sell_only_book) {
    CheckedOrderBook book;
    book.submit(order(1, Side::Sell, 100, 2));
    invariants(book);
}
TEST_CASE(invariants_accept_valid_lifecycle_after_every_command) {
    CheckedOrderBook book;
    invariants(book);
    book.submit(order(1, Side::Buy, 99, 3)); invariants(book);
    book.submit(order(2, Side::Buy, 99, 2)); invariants(book);
    book.submit(order(3, Side::Sell, 101, 4)); invariants(book);
    book.submit(order(4, Side::Sell, 99, 1)); invariants(book);
    CHECK(book.cancel(2)); invariants(book);
    CHECK(!book.cancel(999)); invariants(book);
    book.submit(order(5, Side::Buy, 101, 6)); invariants(book);
    CHECK(book.cancel(1)); invariants(book);
    CHECK(book.cancel(5)); invariants(book);
    check_empty(book);
}

// Compare node addresses, not iterators from potentially different containers.
// All deliberately substituted iterators below refer to live nodes.
void check_location(OrderBook& book, OrderId id, Side side, Price p,
                    std::size_t offset = 0) {
    const auto& location = OrderBookTestAccess::ids(book).at(id);
    CHECK(location.side == side);
    CHECK(location.level->first == p);
    CHECK(std::addressof(location.level->second) ==
          std::addressof(OrderBookTestAccess::orders(book, side, p)));
    CHECK(std::addressof(*location.order) ==
          std::addressof(OrderBookTestAccess::order(book, side, p, offset)));
    CHECK(location.order->id == id);
}

TEST_CASE(locations_register_exact_nodes_on_both_sides) {
    CheckedOrderBook book;
    for (Side side : sides) {
        const Price p = side == Side::Buy ? 90 : 110;
        const OrderId base = side == Side::Buy ? 10 : 20;
        book.submit(order(base, side, p, 5));
        book.submit(order(base + 1, side, p, 7));
        book.submit(order(base + 2, side, p + 1, 9));
        check_location(book, base, side, p);
        check_location(book, base + 1, side, p, 1);
        check_location(book, base + 2, side, p + 1);
    }
    CHECK(OrderBookTestAccess::ids(book).size() == 6);
}

TEST_CASE(locations_cancel_preserves_survivor_nodes_both_sides) {
    for (Side side : sides) {
        for (OrderId removed : {OrderId{1}, OrderId{2}, OrderId{3}}) {
            CheckedOrderBook book;
            for (OrderId id = 1; id <= 4; ++id)
                book.submit(order(id, side, id == 4 ? 101 : 100, id));
            const auto before = book.snapshot();
            const auto saved = OrderBookTestAccess::ids(book);
            CHECK(!book.cancel(999));
            check_snapshot(book.snapshot(), before);
            CHECK(book.cancel(removed));
            CHECK(!OrderBookTestAccess::ids(book).contains(removed));
            std::size_t offset = 0;
            for (OrderId id = 1; id <= 4; ++id) {
                if (id == removed) continue;
                check_location(book, id, side, id == 4 ? 101 : 100,
                               id == 4 ? 0 : offset++);
                const auto& now = OrderBookTestAccess::ids(book).at(id);
                CHECK(std::addressof(*now.level) == std::addressof(*saved.at(id).level));
                CHECK(std::addressof(*now.order) == std::addressof(*saved.at(id).order));
                CHECK(now.order->priority == id);
            }
            // Erase through every surviving locator, including the other level.
            for (OrderId id = 1; id <= 4; ++id)
                if (id != removed) CHECK(book.cancel(id));
            CHECK(OrderBookTestAccess::ids(book).empty());
            check_empty(book);
        }
    }
}

TEST_CASE(locations_partial_fill_keeps_maker_node_both_sides) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 5));
        book.submit(order(2, side, 100, 7));
        const Location saved = OrderBookTestAccess::ids(book).at(1);
        check_trades(book.submit(order(3, opposite(side), 100, 2)),
                     {execution(opposite(side), 3, 1, 100, 2)});
        check_location(book, 1, side, 100);
        CHECK(std::addressof(*OrderBookTestAccess::ids(book).at(1).order) ==
              std::addressof(*saved.order));
        CHECK(saved.order->quantity == 3);
        CHECK(saved.order->priority == 1);
        CHECK(!OrderBookTestAccess::ids(book).contains(3));
        CHECK(book.cancel(1));
        check_location(book, 2, side, 100);
        CHECK(book.cancel(2));
        check_empty(book);
    }
}

TEST_CASE(locations_sweep_removes_makers_and_registers_only_residual) {
    for (Side taker : sides) {
        for (bool market : {false, true}) {
            CheckedOrderBook book;
            book.submit(order(1, opposite(taker), price(taker, 0), 2));
            book.submit(order(2, opposite(taker), price(taker, 1), 3));
            const auto trades = market ? book.submit_market(3, taker, 7)
                : book.submit(order(3, taker, price(taker, 2), 7));
            check_trades(trades, {execution(taker, 3, 1, price(taker, 0), 2),
                                  execution(taker, 3, 2, price(taker, 1), 3)});
            CHECK(!OrderBookTestAccess::ids(book).contains(1));
            CHECK(!OrderBookTestAccess::ids(book).contains(2));
            CHECK(!book.cancel(1));
            CHECK(!book.cancel(2));
            if (market) {
                CHECK(OrderBookTestAccess::ids(book).empty());
                CHECK(!book.cancel(3));
            } else {
                CHECK(OrderBookTestAccess::ids(book).size() == 1);
                check_location(book, 3, taker, price(taker, 2));
                CHECK(OrderBookTestAccess::ids(book).at(3).order->quantity == 2);
                CHECK(book.cancel(3));
            }
            check_empty(book);
        }
    }
}

TEST_CASE(locations_recreate_level_and_reuse_id_on_opposite_side) {
    for (Side side : sides) {
        CheckedOrderBook book;
        for (int repeat = 0; repeat < 10; ++repeat) {
            book.submit(order(1, side, 100, 2));
            check_location(book, 1, side, 100);
            CHECK(book.cancel(1));
            CHECK(OrderBookTestAccess::ids(book).empty());
            book.submit(order(1, opposite(side), 100, 3));
            check_location(book, 1, opposite(side), 100);
            book.submit_market(2, side, 3);
            CHECK(OrderBookTestAccess::ids(book).empty());
            check_empty(book);
        }
    }
}

TEST_CASE(locations_survive_index_rehash_and_map_insertions) {
    for (Side side : sides) {
        CheckedOrderBook book;
        book.submit(order(1, side, 100, 5));
        const Location saved = OrderBookTestAccess::ids(book).at(1);
        for (OrderId id = 2; id <= 128; ++id)
            book.submit(order(id, side, 100 + static_cast<Price>(id % 8), 1));
        auto& active = OrderBookTestAccess::ids(book);
        const auto old_buckets = active.bucket_count();
        active.rehash(old_buckets * 4);
        CHECK(active.bucket_count() > old_buckets);
        book.assert_invariants();
        check_location(book, 1, side, 100);
        CHECK(std::addressof(*active.at(1).level) == std::addressof(*saved.level));
        CHECK(std::addressof(*active.at(1).order) == std::addressof(*saved.order));
        // Cancel all nodes in a reproducible shuffled order after rehash.
        std::vector<OrderId> ids(128);
        std::iota(ids.begin(), ids.end(), OrderId{1});
        std::mt19937 random(7);
        std::shuffle(ids.begin(), ids.end(), random);
        for (OrderId id : ids) CHECK(book.cancel(id));
        CHECK(active.empty());
        check_empty(book);
    }
}

// These tests require number two: checking stored Location values. Only a
// location is corrupted; the orders, active keys, and snapshot stay valid.
// Exception wording is deliberately not part of this internal contract.
template<class Corrupt>
void reject_bad_location(Corrupt corrupt) {
    for (Side side : sides) {
        CheckedOrderBook book;
        const Price p = side == Side::Buy ? 90 : 110;
        book.submit(order(1, side, p, 5));
        book.submit(order(2, side, p, 7));
        book.submit(order(3, side, p + 1, 9));
        book.submit(order(4, opposite(side), side == Side::Buy ? 120 : 80, 11));
        const auto before = book.snapshot();
        corrupt(OrderBookTestAccess::ids(book), side);
        bool rejected = false;
        try { book.assert_invariants(); }
        catch (const std::logic_error&) { rejected = true; }
        check_snapshot(book.snapshot(), before);
        if (!rejected)
            throw std::runtime_error("Invariant checker accepted a corrupted Location");
    }
}

TEST_CASE(locations_checker_rejects_wrong_side) {
    reject_bad_location([](auto& active, Side side) { active.at(1).side = opposite(side); });
}
TEST_CASE(locations_checker_rejects_invalid_side) {
    reject_bad_location([](auto& active, Side) { active.at(1).side = static_cast<Side>(99); });
}
TEST_CASE(locations_checker_rejects_other_level_same_side) {
    reject_bad_location([](auto& active, Side) { active.at(1).level = active.at(3).level; });
}
TEST_CASE(locations_checker_rejects_level_on_opposite_side) {
    reject_bad_location([](auto& active, Side) { active.at(1).level = active.at(4).level; });
}
TEST_CASE(locations_checker_rejects_other_order_same_level) {
    reject_bad_location([](auto& active, Side) { active.at(1).order = active.at(2).order; });
}
TEST_CASE(locations_checker_rejects_order_at_other_level) {
    reject_bad_location([](auto& active, Side) { active.at(1).order = active.at(3).order; });
}
TEST_CASE(locations_checker_rejects_order_on_opposite_side) {
    reject_bad_location([](auto& active, Side) { active.at(1).order = active.at(4).order; });
}
TEST_CASE(locations_checker_rejects_swapped_complete_locations) {
    reject_bad_location([](auto& active, Side) { std::swap(active.at(1), active.at(2)); });
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--list") {
        for (const auto& item : cases()) std::cout << item.name << '\n';
        return 0;
    }
    if (argc > 2) {
        std::cerr << "Usage: order_book_edge_cases [test_name|--list]\n";
        return 2;
    }
    int passed = 0, failed = 0, skipped = 0;
    for (const auto& item : cases()) {
        if (argc == 2 && item.name != argv[1]) continue;
        try {
            item.run();
            ++passed;
            std::cout << "PASS " << item.name << '\n';
        } catch (const MissingFeature& e) {
            ++skipped;
            std::cout << "SKIP " << item.name << ": " << e.what() << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "FAIL " << item.name << ": " << e.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "FAIL " << item.name << ": unknown exception\n";
        }
    }
    if (passed + failed + skipped == 0) {
        std::cerr << "Unknown test name\n";
        return 2;
    }
    std::cout << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    if (failed) return 1;
    return skipped ? 77 : 0;
}
