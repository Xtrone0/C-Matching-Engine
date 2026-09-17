# Command runner

`include/command_runner.hpp` supplies typed commands and a runner for an ordinary
`OrderBook`. It does not depend on the test reference model or a file parser.

```cpp
#include "command_runner.hpp"

OrderBook book;
book.submit({10, Side::Sell, 101, 4}); // Optional existing initial state.
std::vector<replay::Command> commands{
    {replay::Action::Market, 20, Side::Buy, 0, 3},
    {replay::Action::Amend, 10, Side::Sell, 102, 2},
    {replay::Action::Cancel, 10, Side::Sell, 0, 0}
};
auto results = replay::run(book, commands);
auto final = book.snapshot();
```

Results correspond to commands in order. Each contains `rejected`, `canceled`,
and ordered `trades`. Ordinary invalid arguments become rejected results; internal
logic/allocation errors propagate. An unknown cancellation has `canceled == false`.

Command fields are action, ID, side, price, and quantity. Market ignores price;
cancel ignores side/price/quantity; amend retains the resting side and interprets
quantity as new remaining quantity. `replay::execute(book, command)` runs one command
without retaining all earlier results and is used by the timing harness.

The caller owns the book and its initial state. File parsing and formatting remain
separate. [Test replay](../tests/README.md#reproduce-and-reduce-failures) compares
against the reference; the runner itself only executes commands.
