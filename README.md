# Graham 
An command line program that helps users track their investment portfolio assets.

## Table of Contents
- [Graham](#graham)
  - [Table of Contents](#table-of-contents)
  - [Command Line Interface](#command-line-interface)
  - [Coding Style Guide](#coding-style-guide)
    - [Style Guide Checklist](#style-guide-checklist)
    - [Data Types and Size Constraints](#data-types-and-size-constraints)
      - [⚠️ Portability caveat](#️portability-caveat)
      - [Fundamental scalar types](#fundamental-scalar-types)
      - [Safety‑critical guidelines](#safetycritical-guidelines)

## Command Line Interface

```shell
# Main help
graham – Investment Portfolio Record Accounting CLI

Usage: graham [--GLOBAL-OPTIONS] [COMMAND] [--OPTIONS]

Commands:
  init          Initialize graham
  portfolios     Add, list or remove portfolios
  assets         Add, list or remove assets
  accounts       Add, list, remove or view accounts
  transactions   Add transactions
  transfers      Add transfers

GLOBAL OPTIONS:
  -h, --help    Show help information and exit
  -v, --verbose Show detailed logs and memory consumption.

Run "graham COMMAND --help" for help on a specific command.

# Init command
graham init – Initialize command
Usage: graham init [--OPTIONS]

Initializes graham and its dependencies.

OPTIONS:
  -h, --help   Show help for graham init

# Migrate command
graham migrate – Migrate command
Usage: graham migrate [--OPTIONS]

Migrates the database to the latest version.

OPTIONS:
  -h, --help   Show help for graham migrate

# Portfolio command
graham portfolios – Portfolio command
Usage: graham portfolios COMMAND [--OPTIONS]

Commands:
  add           Add a portfolio
  list          List all portfolios
  view          View a portfolio
  rm            Remove a portfolio

Options (add):
  -h, --help Show help for graham portfolio

# Portfolio add command
graham portfolios add – Add command
Usage: graham portfolios add [--OPTIONS]

Add portfolio:
  graham portfolios add --name PORTFOLIO_NAME --currency CURRENCY
  PORTFOLIO_NAME:   1 < characters ≤ 30
  CURRENCY:         1 < characters ≤ 3 (USD, EUR, GBP, etc.)

Options (add):
  -n, --name PORTFOLIO_NAME   Portfolio name
  -c, --currency CURRENCY     Currency
  -h, --help Show help for graham portfolio add

# Portfolio list command
graham portfolios list – List command
Usage: graham portfolios list [--OPTIONS]

List portfolios:
  graham portfolios list

Options (list):
  -h, --help Show help for graham portfolios list

# Portfolio rm command
graham portfolios rm – Remove command
Usage: graham portfolios rm [--OPTIONS]

Remove portfolio:
  graham portfolios rm --name NAME

Options (list):
  -n, --name PORTFOLIO_NAME   Portfolio name
  -h, --help Show help for graham portfolios rm

# Asset command
graham assets – Asset command
Usage: graham assets COMMAND [--OPTIONS]

Commands:
  add           Add an asset
  list          List all assets
  rm            Remove an asset

Options (add):
  -h, --help Show help for graham assets

# Asset add command
graham assets add – Add command
Usage: graham assets add [--OPTIONS]

Add an asset:
  graham assets add --symbol SYMBOL --name NAME --type TYPE --tax-exempt

Options (add):
  -s, --symbol ASSET_SYMBOL   Asset symbol
  -n, --name ASSET_NAME   Asset name
  -t, --type ASSET_TYPE   Asset type
  -x, --tax-exempt       Asset is tax-exempt
  -h, --help Show help for graham assets

Asset types:
  stock   Stock
  bond    Bond
  etf     ETF
  cash    Cash

# Asset list command
graham assets list – List command
Usage: graham assets list [--OPTIONS]

List all assets:
  graham assets list

Options:
  -h, --help Show help for graham assets list

# Asset rm command
graham assets rm – Remove command
Usage: graham assets rm [--OPTIONS]

Remove an asset:
  graham assets rm --symbol SYMBOL

Options:
  -h, --help Show help for graham assets rm

# Account command
graham accounts – Account command
Usage: graham accounts [--OPTIONS]

Commands:
  add           Add an account
  list          List all accounts
  rm            Remove an account
  view          View an account

Options:
  -h, --help Show help for graham accounts

# Account add command
graham accounts add – Add command
Usage: graham accounts add [--OPTIONS]

Add an account:
  graham accounts add --symbol SYMBOL --portfolio PORTFOLIO_NAME

Options:
  -p, --portfolio PORTFOLIO_NAME The portfolio to list accounts for
  -s, --symbol SYMBOL The symbol of the account to list
  -h, --help Show help for graham accounts add

# Account list command
graham accounts list – List command
Usage: graham accounts list [--OPTIONS]

List all accounts:
  graham accounts list

Options:
  -h, --help Show help for graham account list

# Account rm command
graham accounts rm – Remove command
Usage: graham accounts rm [--OPTIONS]

Remove an account:
  graham accounts rm --symbol SYMBOL --portfolio PORTFOLIO_NAME

Options:
  -s, --symbol SYMBOL The symbol of the account to remove
  -p, --portfolio PORTFOLIO_NAME The portfolio of the account to remove
  -h, --help Show help for graham accounts rm

# Account view command
graham accounts view – View command
Usage: graham accounts view [--OPTIONS]

View an account:
  graham accounts view --portfolio PORTFOLIO_NAME --asset ASSET_SYMBOL

Options:
  -p, --portfolio PORTFOLIO_NAME The portfolio of the account to view
  -a, --asset ASSET_SYMBOL The asset of the account to view
  -h, --help Show help for graham accounts view

# Transaction command
graham transactions – Transactions command
Usage: graham transactions COMMAND [--OPTIONS]

Commands:
  add Adds a transaction that records the movement of value from one account to another

Options:
  -h, --help Show help for graham transactions

# Transaction add command
graham transactions add – Add a transaction that records the movement of value from one account to another

Usage: graham transactions add --portfolio PORTFOLIO_NAME --to-asset ASSET_SYMBOL --to-amount TO_AMOUNT --from-asset FROM_ASSET_SYMBOL --from-amount FROM_AMOUNT --date DATE

Options:
  -p, --portfolio PORTFOLIO_NAME The portfolio of the transaction
  -t, --to-asset ASSET_SYMBOL The asset to move value to
  -a, --to-amount TO_AMOUNT The amount of value to move to the to asset
  -f, --from-asset FROM_ASSET_SYMBOL The asset to move value from
  -b, --from-amount FROM_AMOUNT The amount of value to move from the from asset
  -d, --date DATE The date of the transaction

# Transfer command
graham transfers – Transfers command
Usage: graham transfers COMMAND [--OPTIONS]

Commands:
  add Adds transactions that record the deposit or withdrawal of value into or from an account

Options:
  -h, --help Show help for graham transfers

# Transfer add command
graham transfers add – Adds transactions that record the deposit or withdrawal of value into or from an account
Usage: graham transfers add --portfolio PORTFOLIO_NAME --asset ASSET_SYMBOL --amount AMOUNT --date DATE [--withdrawal|--deposit] [--OPTIONS]

Options:
  -h, --help Show help for graham transfers add
  -d, --deposit The transfer is a deposit
  -w, --withdrawal The transfer is a withdrawal
  -a, --amount AMOUNT The amount of the transfer
  -p, --portfolio PORTFOLIO_NAME The name of the portfolio to add the transfer to
  -s, --asset ASSET_SYMBOL The symbol of the asset to add the transfer to
  -t, --date DATE The date of the transfer
```


## Coding Style Guide
### Style Guide Checklist
| ✓ | Rule |
|--------|------|
| ✓ | Prefer **C99** (or newer) but restrict to a "safe subset" (no `setjmp`, VLAs, K\&R style). |
| ☐ | **No dynamic allocation after `main`** starts; pre‑allocate or use memory pools. |
| ☐ | **No recursion or `goto`;** loops must have fixed bounds established at compile‑time. |
| ☐ | Limit functions to **≤ 40 logical lines**; maximum one level of pointer indirection. |
| ☐ | **All warnings are errors** (`-Wall -Wextra -Werror -pedantic`); zero undefined behaviour. |
| ☐ | Each function contains **≥ 2 assert‑style runtime checks** (disabled only in release). |
| ☐ | **Check every non‑void return value** (or `(void)expr` to signal "intentionally ignored"). |
| ☐ | Only header‑guarded `#include`s and **macro‑like constants;** no macro functions. |
| ☐ | Minimise global state; mark file‑local objects `static`. |
| ☐ | Continuous **static analysis** (clang‑tidy, cppcheck) + MISRA / cert‑C gates. ([Wikipedia][1]) |

[1]: https://en.wikipedia.org/wiki/The_Power_of_10%3A_Rules_for_Developing_Safety-Critical_Code?utm_source=chatgpt.com "The Power of 10: Rules for Developing Safety-Critical Code"

### Data Types and Size Constraints
#### ⚠️ Portability caveat

ISO C guarantees only *minimum* widths and certain ordering relations (e.g., `sizeof(short) ≤ sizeof(int)`). Actual byte sizes depend on the compiler/CPU **data model**:

| Data model | `char` | `short` | `int` | `long` | `long long` | `void*` | Typical targets             |
| ---------- | :----: | :-----: | :---: | :----: | :---------: | :-----: | --------------------------- |
| **ILP32**  |    1   |    2    |   4   |    4   |      8      |    4    | 32‑bit ARM, x86             |
| **LP64**   |    1   |    2    |   4   |    8   |      8      |    8    | Linux/macOS x86‑64, AArch64 |
| **LLP64**  |    1   |    2    |   4   |    4   |      8      |    8    | Windows x86‑64              |

---

#### Fundamental scalar types

| Category                     | C type                                 | **Minimum bits**<br>(C17 §5.2.4.2.1) | **Max digits/chars** |  ILP32<br>(bytes)  | LP64<br>(bytes) | LLP64<br>(bytes) |
| ---------------------------- | -------------------------------------- | ------------------------------------ | ------------------- | :----------------: | :-------------: | :--------------: |
| **Character**                | `char`, `signed char`, `unsigned char` | 8                                    | 1                   |        **1**       |      **1**      |       **1**      |
|                              | `wchar_t` *(impl‑defined)*             | ≥ 8                                  | 1                   |     2 (UTF‑16)     |    4 (UTF‑32)   |    2 (UTF‑16)    |
| **Boolean**                  | `_Bool` / `bool`                       | 1                                    | —                   |          1         |        1        |         1        |
| **Integer**                  | `short` / `unsigned short`             | ≥ 16                                 | 5 / 6               |          2         |        2        |         2        |
|                              | `int` / `unsigned int`                 | ≥ 16                                 | 10 / 11             |          4         |        4        |         4        |
|                              | `long` / `unsigned long`               | ≥ 32                                 | 10 / 11             |          4         |        8        |         4        |
|                              | `long long` / `unsigned long long`     | ≥ 64                                 | 19 / 20             |          8         |        8        |         8        |
| **Fixed‑width `<stdint.h>`** | `int8_t`, `uint8_t`                    | 8                                    | 3 / 4               |          1         |        1        |         1        |
|                              | `int16_t`, `uint16_t`                  | 16                                   | 5 / 6               |          2         |        2        |         2        |
|                              | `int32_t`, `uint32_t`                  | 32                                   | 10 / 11             |          4         |        4        |         4        |
|                              | `int64_t`, `uint64_t`                  | 64                                   | 19 / 20             |          8         |        8        |         8        |
| **Floating point**           | `float`                                | IEEE binary32                        | ~7                  |          4         |        4        |         4        |
|                              | `double`                               | IEEE binary64                        | ~15                 |          8         |        8        |         8        |
|                              | `long double`\*                        | ≥ `double`                           | ~19                 | 8 (SSE) / 12 (x87) |        16       |        16        |
| **Pointer‑sized**            | `void*`, any object pointer            | —                                    | —                   |          4         |        8        |         8        |
| **Size / diff**              | `size_t`, `ptrdiff_t`                  | —                                    | —                   |          4         |        8        |         8        |

\* `long double` varies by compiler:
\* GCC/Clang x86‑64 Linux → 16 B (binary128) • x86‑32 → 12 B • MSVC → 8 B\*

---

#### Safety‑critical guidelines

1. **Prefer fixed‑width types**

   ```c
   #include <stdint.h>
   uint32_t counter;
   ```

2. **Verify assumptions at compile‑time**

   ```c
   #include <stdint.h>
   #include <assert.h>
   static_assert(sizeof(uint32_t) == 4, "Expect 32‑bit uint32_t");
   ```

3. **Cast addresses with `uintptr_t`/`intptr_t`**, *not* plain `unsigned long`.

4. **Serialise explicitly**—write bytes in a defined order instead of dumping whole structs.

5. **Use `<inttypes.h>` macros** for portable `printf`/`scanf`:

   ```c
   #include <inttypes.h>
   printf("value = %" PRIu32 "\n", counter);
   ```

Follow these rules to keep your code portable from tiny microcontrollers to 64‑bit servers while knowing *exactly* how many bytes every variable occupies.
