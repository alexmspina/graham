#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <dirent.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "sqlite3.h"

// Constraints - add 1 for null terminator \0
#define PORTFOLIO_NAME_MAX_LENGTH_WITH_NULL 21
#define ASSET_SYMBOL_MAX_LENGTH_WITH_NULL 6
#define ASSET_NAME_MAX_LENGTH_WITH_NULL 31
#define TIMESTAMP_MAX_LENGTH_WITH_NULL 25
#define UUID_MAX_LENGTH_WITH_NULL 38
#define CURRENCY_MAX_LENGTH_WITH_NULL 4

// Command Line Interface
typedef enum {
    PARSE_OK = 0,

    /* user errors ------------------------------------ */
    E_NO_COMMAND,       /* nothing after program name           */
    E_NO_SUBCMD,        /* nothing after command name           */
    E_UNKNOWN_COMMAND,  /* "graham frobnicate ..."                */
    E_UNKNOWN_SUBCMD,   /* "graham portfolio frob ..."            */
    E_TOO_MANY_ARGS,    /* "graham portfolio add --help"            */
    E_MISSING_REQUIRED, /* --portfolio NAME not supplied        */
    E_BAD_OPTION,       /* unknown -x / --xyzzy                 */
    E_BAD_OPTION_VALUE, /* --assets=abc                         */

    /* internal / system errors ----------------------- */
    E_INTERNAL,  /* logic bug (should not happen)        */
    E_MEM_ALLOC, /* malloc failed                        */
} ParseStatus;

typedef enum {
    COMMAND_NONE,
    COMMAND_INIT,
    COMMAND_MIGRATE,
    COMMAND_PORTFOLIOS,
    COMMAND_PORTFOLIOS_ADD,
    COMMAND_PORTFOLIOS_LIST,
    COMMAND_PORTFOLIOS_RM,
    COMMAND_ASSETS,
    COMMAND_ASSETS_ADD,
    COMMAND_ASSETS_LIST,
    COMMAND_ASSETS_RM,
    COMMAND_ACCOUNTS,
    COMMAND_ACCOUNTS_ADD,
    COMMAND_ACCOUNTS_LIST,
    COMMAND_ACCOUNTS_RM,
    COMMAND_ACCOUNTS_VIEW,
    COMMAND_TRANSACTIONS,
    COMMAND_TRANSACTIONS_ADD,
    COMMAND_TRANSFERS,
    COMMAND_TRANSFERS_ADD,
} Command;

char *cmd_name[] = {
    [COMMAND_NONE] = "none",
    [COMMAND_INIT] = "init",
    [COMMAND_MIGRATE] = "migrate",
    [COMMAND_PORTFOLIOS] = "portfolios",
    [COMMAND_PORTFOLIOS_ADD] = "portfolios add",
    [COMMAND_PORTFOLIOS_LIST] = "portfolios list",
    [COMMAND_PORTFOLIOS_RM] = "portfolios rm",
    [COMMAND_ASSETS] = "assets",
    [COMMAND_ASSETS_ADD] = "assets add",
    [COMMAND_ASSETS_LIST] = "assets list",
    [COMMAND_ASSETS_RM] = "assets rm",
    [COMMAND_ACCOUNTS] = "accounts",
    [COMMAND_ACCOUNTS_ADD] = "accounts add",
    [COMMAND_ACCOUNTS_LIST] = "accounts list",
    [COMMAND_ACCOUNTS_RM] = "accounts rm",
    [COMMAND_ACCOUNTS_VIEW] = "accounts view",
    [COMMAND_TRANSACTIONS] = "transactions",
    [COMMAND_TRANSACTIONS_ADD] = "transactions add",
    [COMMAND_TRANSFERS] = "transfers",
    [COMMAND_TRANSFERS_ADD] = "transfers add",
};

typedef struct {
    ParseStatus code;
    char msg[128];
} ParseResult;

typedef struct {
    bool help;
    bool verbose;
} GlobalOpts;

typedef struct {
    bool help;
} InitOpts;

typedef struct {
    bool help;
} MigrateOpts;

typedef struct {
    const char *name;
    const char *currency;
    bool help;
} PortfolioAddOpts;

typedef struct {
    bool help;
} PortfolioListOpts;

typedef struct {
    const char *name;
    bool help;
} PortfolioRmOpts;

typedef struct {
    Command cmd;
    bool help;
    union {
        PortfolioAddOpts add;
        PortfolioListOpts list;
        PortfolioRmOpts rm;
    } opts;
} PortfolioOpts;

typedef enum {
    ASSET_TYPE_UNSET,
    ASSET_TYPE_STOCK,
    ASSET_TYPE_BOND,
    ASSET_TYPE_ETF,
    ASSET_TYPE_CASH,
} AssetType;

static const char *asset_type_name[] = {
    [ASSET_TYPE_STOCK] = "stock",
    [ASSET_TYPE_BOND] = "bond",
    [ASSET_TYPE_ETF] = "etf",
    [ASSET_TYPE_CASH] = "cash",
};

typedef struct {
    const char *symbol;
    const char *name;
    AssetType type;
    bool tax_exempt;
    bool help;
} AssetAddOpts;

typedef struct {
    bool help;
} AssetListOpts;

typedef struct {
    const char *symbol;
    bool help;
} AssetRmOpts;

typedef struct {
    Command cmd;
    bool help;
    union {
        AssetAddOpts add;
        AssetListOpts list;
        AssetRmOpts rm;
    } opts;
} AssetOpts;

typedef struct {
    const char *asset;
    const char *portfolio;
    bool help;
} AccountAddOpts;

typedef struct {
    bool help;
} AccountListOpts;

typedef struct {
    const char *asset;
    const char *portfolio;
    bool help;
} AccountRmOpts;

typedef struct {
    const char *portfolio;
    const char *asset;
    bool help;
} AccountViewOpts;

typedef struct {
    Command cmd;
    bool help;
    union {
        AccountAddOpts add;
        AccountListOpts list;
        AccountRmOpts rm;
        AccountViewOpts view;
    } opts;
} AccountOpts;

typedef struct {
    const char *portfolio;
    const char *to_asset;
    const char *to_amount;
    const char *from_asset;
    const char *from_amount;
    const char *date;
    bool help;
} TransactionAddOpts;

typedef struct {
    Command cmd;
    bool help;
    union {
        TransactionAddOpts add;
    } opts;
} TransactionOpts;

typedef struct {
    const char *portfolio_name;
    const char *asset_symbol;
    const char *amount;
    bool withdrawal;
    bool deposit;
    const char *date;
    bool help;
} TransferAddOpts;

typedef struct {
    Command cmd;
    bool help;
    union {
        TransferAddOpts add;
    } opts;
} TransferOpts;

typedef struct {
    Command cmd;
    GlobalOpts global_opts;
    union {
        InitOpts init_opts;
        MigrateOpts migrate_opts;
        PortfolioOpts portfolio_opts;
        AssetOpts asset_opts;
        AccountOpts account_opts;
        TransactionOpts transaction_opts;
        TransferOpts transfer_opts;
    } opts;
} Args;

static ParseResult parse_args(int *argc, char ***argv, Args *out);
static void graham_print_help(FILE *stream);

#define RETURN_PARSE_ERR(err_code, fmt, ...)             \
    do {                                                 \
        ParseResult r = {.code = (err_code)};            \
        snprintf(r.msg, sizeof r.msg, fmt, __VA_ARGS__); \
        return r;                                        \
    } while (0)

typedef enum {
    CMD_OK = 0,
    CMD_UNKNOWN_ERR,
    CMD_DB_ERR,
    CMD_PORTFOLIO_MAX_COUNT_ERR,
    CMD_PORTFOLIO_NOT_FOUND_ERR,
    CMD_MULTIPLE_PORTFOLIOS_FOUND_ERR,
    CMD_ASSET_ALREADY_EXISTS_ERR,
    CMD_ASSET_NOT_FOUND_ERR,
    CMD_MULTIPLE_ASSETS_FOUND_ERR,
    CMD_ACCOUNT_EXISTS_ERR,
    CMD_ACCOUNT_NOT_FOUND_ERR,
    CMD_TO_ACCOUNT_NOT_FOUND_ERR,
    CMD_FROM_ACCOUNT_NOT_FOUND_ERR,
    CMD_INSUFFICIENT_FUNDS_ERR,
} CmdResultCode;

static char *cmd_result_msg(CmdResultCode code, Command cmd) {
    switch (code) {
        case CMD_OK:
            return "OK";
        case CMD_PORTFOLIO_MAX_COUNT_ERR:
            return "You can only have 3 portfolios.";
        case CMD_ASSET_ALREADY_EXISTS_ERR:
            return "Asset already exists.";
        case CMD_ASSET_NOT_FOUND_ERR:
            return "Asset not found.";
        case CMD_MULTIPLE_ASSETS_FOUND_ERR:
            return "Multiple assets found.";
        case CMD_MULTIPLE_PORTFOLIOS_FOUND_ERR:
            return "Multiple portfolios found.";
        case CMD_ACCOUNT_EXISTS_ERR:
            return "Account already exists.";
        case CMD_ACCOUNT_NOT_FOUND_ERR:
            return "Account not found.";
        case CMD_TO_ACCOUNT_NOT_FOUND_ERR:
            return "To account not found.";
        case CMD_FROM_ACCOUNT_NOT_FOUND_ERR:
            return "From account not found.";
        case CMD_INSUFFICIENT_FUNDS_ERR:
            return "Insufficient funds.";
        default:
            return "Oops graham failed for some reason...";
    }
}

// Init command
static CmdResultCode cmd_init(Args args);
static void init_print_help(FILE *stream);
static ParseResult parse_init_cmd(int *argc, char ***argv, Args *out);

// Migrate command
static CmdResultCode cmd_migrate(Args args);
static void migrate_print_help(FILE *stream);
static ParseResult parse_migrate_cmd(int *argc, char ***argv, Args *out);

// Portfolio command
static void portfolio_print_help(FILE *stream);
static ParseResult parse_portfolio_cmd(int *argc, char ***argv, Args *out);

// Portfolio add command
static ParseResult parse_portfolio_add_cmd(int *argc, char ***argv, Args *out);
static void portfolio_add_print_help(FILE *stream);
static CmdResultCode cmd_portfolio_add(Args args);

// Portfolio list command
static ParseResult parse_portfolio_list_cmd(int *argc, char ***argv, Args *out);
static void portfolio_list_print_help(FILE *stream);
static CmdResultCode cmd_portfolio_list(Args args);

// Portfolio rm command
static ParseResult parse_portfolio_rm_cmd(int *argc, char ***argv, Args *out);
static void portfolio_rm_print_help(FILE *stream);
static CmdResultCode cmd_portfolio_rm(Args args);

typedef struct {
    char id[UUID_MAX_LENGTH_WITH_NULL];
    char name[PORTFOLIO_NAME_MAX_LENGTH_WITH_NULL];
    char currency[CURRENCY_MAX_LENGTH_WITH_NULL];
    char created_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
    char updated_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
} Portfolio;

// Asset command
static ParseResult parse_asset_cmd(int *argc, char ***argv, Args *out);
static void asset_print_help(FILE *stream);

typedef struct {
    const char *symbol;
    const char *name;
    AssetType type;
    bool tax_exempt;
    bool help;
} Asset;

// Asset add command
static ParseResult parse_asset_add_cmd(int *argc, char ***argv, Args *out);
static void asset_add_print_help(FILE *stream);
static CmdResultCode cmd_asset_add(Args args);

// Asset list command
static ParseResult parse_asset_list_cmd(int *argc, char ***argv, Args *out);
static void asset_list_print_help(FILE *stream);
static CmdResultCode cmd_asset_list(Args args);

// Asset rm command
static ParseResult parse_asset_rm_cmd(int *argc, char ***argv, Args *out);
static void asset_rm_print_help(FILE *stream);
static CmdResultCode cmd_asset_rm(Args args);

// Account command
static ParseResult parse_account_cmd(int *argc, char ***argv, Args *out);
static void account_print_help(FILE *stream);

// Account add command
static ParseResult parse_account_add_cmd(int *argc, char ***argv, Args *out);
static void account_add_print_help(FILE *stream);
static CmdResultCode cmd_account_add(Args args);

// Account list command
static ParseResult parse_account_list_cmd(int *argc, char ***argv, Args *out);
static void account_list_print_help(FILE *stream);
static CmdResultCode cmd_account_list(Args args);

// Account rm command
static ParseResult parse_account_rm_cmd(int *argc, char ***argv, Args *out);
static void account_rm_print_help(FILE *stream);
static CmdResultCode cmd_account_rm(Args args);

// Account view command
static ParseResult parse_account_view_cmd(int *argc, char ***argv, Args *out);
static void account_view_print_help(FILE *stream);
static CmdResultCode cmd_account_view(Args args);

// Transaction command
static ParseResult parse_transaction_cmd(int *argc, char ***argv, Args *out);
static void transaction_print_help(FILE *stream);

// Transaction add command
static ParseResult parse_transaction_add_cmd(int *argc, char ***argv, Args *out);
static void transaction_add_print_help(FILE *stream);
static CmdResultCode cmd_transaction_add(Args args);

// Transfer command
static ParseResult parse_transfer_cmd(int *argc, char ***argv, Args *out);
static void transfer_print_help(FILE *stream);

// Transfer add command
static ParseResult parse_transfer_add_cmd(int *argc, char ***argv, Args *out);
static void transfer_add_print_help(FILE *stream);
static CmdResultCode cmd_transfer_add(Args args);

typedef struct {
    size_t app_memory;
    size_t sqlite_page_cache;
    size_t sqlite_heap;
    size_t sqlite_lookaside;
} AppMemory;

static AppMemory bytes_for_command(Args args);
static void *allocate_memory(size_t size);
static void setup_sqlite(uint8_t **p, AppMemory app_memory);
static void setup_app_pool(uint8_t **p, size_t app_memory);

#define APP_MEMORY_OBJECT_SIZE 16
#define PORTFOLIO_MAX_COUNT 3
#define KB 1024
#define PAGE_SIZE 4096
#define LOOKASIDE_SLOT_SIZE 256
#define LOOKASIDE_SLOT_COUNT 64

// Database
typedef struct {
    int version;
    const char *sql;
} Migration;

static int exec_sql(sqlite3 *db, const char *sql);
static int migrate(sqlite3 *db, const char *dir);
static uint64_t fnv1a64(const void *data, size_t len);
static uint64_t checksum_file(const char *path);
static bool file_exists(const char *path);

// UUIDv7
static void generate_uuid_v7(char *uuid_str);

// Timestamp
static uint64_t get_current_timestamp_ms();
static void epoch_ms_to_iso8601(uint64_t ms, char iso[TIMESTAMP_MAX_LENGTH_WITH_NULL]);

int main(int argc, char **argv) {
    Args args = {0};
    ParseResult result = parse_args(&argc, &argv, &args);
    if (result.code != PARSE_OK) {
        fprintf(stderr, "%s\n", result.msg);

        if (result.code == E_NO_COMMAND || result.code == E_UNKNOWN_COMMAND) {
            graham_print_help(stderr);
            return EXIT_SUCCESS;
        }

        if (result.code == E_NO_SUBCMD) {
            if (args.cmd == COMMAND_PORTFOLIOS) {
                portfolio_print_help(stderr);
            }
            if (args.cmd == COMMAND_ASSETS) {
                asset_print_help(stderr);
            }
            if (args.cmd == COMMAND_ACCOUNTS) {
                account_print_help(stderr);
            }
            if (args.cmd == COMMAND_TRANSACTIONS) {
                transaction_print_help(stderr);
            }
            if (args.cmd == COMMAND_TRANSFERS) {
                transfer_print_help(stderr);
            }
            return EXIT_SUCCESS;
        }

        return EXIT_FAILURE;
    }

    if (args.global_opts.help) {
        graham_print_help(stdout);
        return EXIT_SUCCESS;
    }

    AppMemory app_need = bytes_for_command(args);

    size_t arena_sz = app_need.app_memory + app_need.sqlite_page_cache + app_need.sqlite_lookaside +
                      app_need.sqlite_heap;

    void *arena = allocate_memory(arena_sz);
    if (!arena) {
        fprintf(stderr, "memory allocation failed");
        return EXIT_FAILURE;
    }

    uint8_t *p = arena;
    setup_sqlite(&p, app_need);
    setup_app_pool(&p, app_need.app_memory);

    if (args.cmd == COMMAND_INIT) {
        if (args.opts.init_opts.help) {
            init_print_help(stdout);
            return EXIT_SUCCESS;
        }

        CmdResultCode err = cmd_init(args);
        if (err != CMD_OK) {
            fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
            return EXIT_FAILURE;
        }

        fprintf(stdout, "Graham is initialized successfully.\n");
        return EXIT_SUCCESS;
    }

    if (!file_exists("graham.db")) {
        fprintf(stderr, "error: graham is not initialized\n");
        fprintf(stderr, "Run `graham init` to initialize graham.\n");
        return EXIT_FAILURE;
    }

    if (args.cmd == COMMAND_MIGRATE) {
        if (args.opts.migrate_opts.help) {
            migrate_print_help(stdout);
            return EXIT_SUCCESS;
        }

        CmdResultCode err = cmd_migrate(args);
        if (err != CMD_OK) {
            fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
            return EXIT_FAILURE;
        }

        fprintf(stdout, "Database migrated successfully.\n");

        return EXIT_SUCCESS;
    }

    if (args.cmd == COMMAND_PORTFOLIOS) {
        if (args.opts.portfolio_opts.help) {
            portfolio_print_help(stdout);
            return EXIT_SUCCESS;
        }

        if (args.opts.portfolio_opts.cmd == COMMAND_PORTFOLIOS_ADD) {
            if (args.opts.portfolio_opts.opts.add.help) {
                portfolio_add_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_portfolio_add(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Created portfolio %s with currency %s",
                    args.opts.portfolio_opts.opts.add.name,
                    args.opts.portfolio_opts.opts.add.currency);

            return EXIT_SUCCESS;
        }

        if (args.opts.portfolio_opts.cmd == COMMAND_PORTFOLIOS_LIST) {
            if (args.opts.portfolio_opts.opts.list.help) {
                portfolio_list_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_portfolio_list(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        if (args.opts.portfolio_opts.cmd == COMMAND_PORTFOLIOS_RM) {
            if (args.opts.portfolio_opts.opts.rm.help) {
                portfolio_rm_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_portfolio_rm(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Removed portfolio %s\n", args.opts.portfolio_opts.opts.rm.name);

            return EXIT_SUCCESS;
        }
    }

    if (args.cmd == COMMAND_ASSETS) {
        if (args.opts.asset_opts.help) {
            asset_print_help(stdout);
            return EXIT_SUCCESS;
        }

        if (args.opts.asset_opts.cmd == COMMAND_ASSETS_ADD) {
            if (args.opts.asset_opts.opts.add.help) {
                asset_add_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_asset_add(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Added asset %s %s\n", args.opts.asset_opts.opts.add.symbol,
                    args.opts.asset_opts.opts.add.name);

            return EXIT_SUCCESS;
        }

        if (args.opts.asset_opts.cmd == COMMAND_ASSETS_LIST) {
            if (args.opts.asset_opts.opts.list.help) {
                asset_list_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_asset_list(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        if (args.opts.asset_opts.cmd == COMMAND_ASSETS_RM) {
            if (args.opts.asset_opts.opts.rm.help) {
                asset_rm_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_asset_rm(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Removed asset %s\n", args.opts.asset_opts.opts.rm.symbol);

            return EXIT_SUCCESS;
        }
    }

    if (args.cmd == COMMAND_ACCOUNTS) {
        if (args.opts.account_opts.help) {
            account_print_help(stdout);
            return EXIT_SUCCESS;
        }

        if (args.opts.account_opts.cmd == COMMAND_ACCOUNTS_ADD) {
            if (args.opts.account_opts.opts.add.help) {
                account_add_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_account_add(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Added account %s to portfolio %s\n",
                    args.opts.account_opts.opts.add.asset,
                    args.opts.account_opts.opts.add.portfolio);

            return EXIT_SUCCESS;
        }

        if (args.opts.account_opts.cmd == COMMAND_ACCOUNTS_LIST) {
            if (args.opts.account_opts.opts.list.help) {
                account_list_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_account_list(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }

        if (args.opts.account_opts.cmd == COMMAND_ACCOUNTS_RM) {
            if (args.opts.account_opts.opts.rm.help) {
                account_rm_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_account_rm(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Removed account %s from portfolio %s\n",
                    args.opts.account_opts.opts.rm.asset, args.opts.account_opts.opts.rm.portfolio);

            return EXIT_SUCCESS;
        }

        if (args.opts.account_opts.cmd == COMMAND_ACCOUNTS_VIEW) {
            if (args.opts.account_opts.opts.view.help) {
                account_view_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_account_view(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }
    }

    if (args.cmd == COMMAND_TRANSACTIONS) {
        if (args.opts.transaction_opts.help) {
            transaction_print_help(stdout);
            return EXIT_SUCCESS;
        }

        if (args.opts.transaction_opts.cmd == COMMAND_TRANSACTIONS_ADD) {
            if (args.opts.transaction_opts.opts.add.help) {
                transaction_add_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_transaction_add(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            fprintf(stdout, "Added transaction %s\n", args.opts.transaction_opts.opts.add.date);

            return EXIT_SUCCESS;
        }
    }

    if (args.cmd == COMMAND_TRANSFERS) {
        if (args.opts.transfer_opts.help) {
            transfer_print_help(stdout);
            return EXIT_SUCCESS;
        }

        if (args.opts.transfer_opts.cmd == COMMAND_TRANSFERS_ADD) {
            if (args.opts.transfer_opts.opts.add.help) {
                transfer_add_print_help(stdout);
                return EXIT_SUCCESS;
            }

            CmdResultCode err = cmd_transfer_add(args);
            if (err != CMD_OK) {
                fprintf(stderr, "error: %s\n", cmd_result_msg(err, args.cmd));
                return EXIT_FAILURE;
            }

            return EXIT_SUCCESS;
        }
    }

    if (args.global_opts.verbose) {
        int cur = 0, hi = 0;
        sqlite3_status(SQLITE_STATUS_MEMORY_USED, &cur, &hi, 0);
        fprintf(stdout, "\n%s\n", "Details");
        fprintf(stdout, "%-25s\n", "------------------------------------");
        fprintf(stdout, "%-25s %s\n", "Command", cmd_name[args.cmd]);
        fprintf(stdout, "%-25s %s\n", "Database", "graham.db");
        fprintf(stdout, "%-25s %ld bytes\n", "SQLite memory allocated",
                app_need.sqlite_page_cache + app_need.sqlite_heap + app_need.sqlite_lookaside);
        fprintf(stdout, "%-25s %d bytes\n", "SQLite peak memory used", hi);
    }

    return EXIT_SUCCESS;
}

void graham_print_help(FILE *stream) {
    fprintf(stream,
            "graham – Investment Portfolio Record Accounting CLI\n"
            "\n"
            "Usage: graham [--GLOBAL-OPTIONS] [COMMAND] [--OPTIONS]\n"
            "\n"
            "Commands:\n"
            "  init          Initialize graham\n"
            "  portfolios     Add, list or remove portfolios\n"
            "  assets         Add, list or remove assets\n"
            "  accounts       Add, list, remove or view accounts\n"
            "  transactions   Add transactions\n"
            "  transfers      Add transfers\n"
            "\n"
            "GLOBAL OPTIONS:\n"
            "  -h, --help    Show help information and exit\n"
            "  -v, --verbose Show detailed logs and memory consumption."
            "\n"
            "Run \"graham COMMAND --help\" for help on a specific command.\n"
            "\n");
}

static ParseResult parse_args(int *argc, char ***argv, Args *out) {
    static struct option global_long[] = {
        {"help", no_argument, 0, 'h'}, {"verbose", no_argument, 0, 'v'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "+hv", global_long, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->global_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'v':
                out->global_opts.verbose = true;
                continue;
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "Unknown option: %c", opt);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc == 0) {
        return (ParseResult){E_NO_COMMAND, "Missing command"};
    }

    const char *cmd = (*argv)[0];

    if (strcmp(cmd, "init") == 0) {
        return parse_init_cmd(argc, argv, out);
    } else if (strcmp(cmd, "migrate") == 0) {
        return parse_migrate_cmd(argc, argv, out);
    } else if (strcmp(cmd, "portfolios") == 0) {
        return parse_portfolio_cmd(argc, argv, out);
    } else if (strcmp(cmd, "assets") == 0) {
        return parse_asset_cmd(argc, argv, out);
    } else if (strcmp(cmd, "accounts") == 0) {
        return parse_account_cmd(argc, argv, out);
    } else if (strcmp(cmd, "transactions") == 0) {
        return parse_transaction_cmd(argc, argv, out);
    } else if (strcmp(cmd, "transfers") == 0) {
        return parse_transfer_cmd(argc, argv, out);
    }

    RETURN_PARSE_ERR(E_UNKNOWN_COMMAND, "Unknown command: %s", cmd);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void init_print_help(FILE *stream) {
    fprintf(stream,
            "graham init – Initialize command\n"
            "Usage: graham init [--OPTIONS]\n"
            "\n"
            "Initializes graham and its dependencies.\n"
            "\n"
            "OPTIONS:\n"
            "  -h, --help   Show help for graham init\n"
            "\n");
}

static ParseResult parse_init_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_INIT;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.init_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "Unknown option: %c", opt);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_init(Args args) {
    if (file_exists("graham.db")) {
        return CMD_OK;
    }

    sqlite3 *db;
    sqlite3_open("graham.db", &db);
    if (migrate(db, "./migrations") != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    return CMD_OK;
}

static void migrate_print_help(FILE *stream) {
    fprintf(stream,
            "graham migrate – Migrate command\n"
            "Usage: graham migrate [--OPTIONS]\n"
            "\n"
            "Migrates the database to the latest version.\n"
            "\n"
            "OPTIONS:\n"
            "  -h, --help   Show help for graham migrate\n"
            "\n");
}

static ParseResult parse_migrate_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_MIGRATE;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.migrate_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "Unknown option: %c", opt);
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_migrate(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);
    if (migrate(db, "./migrations") != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    return CMD_OK;
}

static void portfolio_print_help(FILE *stream) {
    fprintf(stream,
            "graham portfolios – Portfolio command\n"
            "Usage: graham portfolios COMMAND [--OPTIONS]\n"
            "\n"
            "Commands:\n"
            "  add           Add a portfolio\n"
            "  list          List all portfolios\n"
            "  view          View a portfolio\n"
            "  rm            Remove a portfolio\n"
            "\n"
            "Options (add):\n"
            "  -h, --help Show help for graham portfolio\n"
            "\n");
}

static ParseResult parse_portfolio_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_PORTFOLIOS;

    static struct option global_long[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", global_long, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.portfolio_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "Unknown option: %c", opt);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc == 0) {
        RETURN_PARSE_ERR(E_NO_SUBCMD, "Missing subcommand for command %s", cmd_name[out->cmd]);
    }

    const char *cmd = (*argv)[0];

    if (strcmp(cmd, "add") == 0) {
        return parse_portfolio_add_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "list") == 0) {
        return parse_portfolio_list_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "rm") == 0) {
        return parse_portfolio_rm_cmd(argc, argv, out);
    }

    RETURN_PARSE_ERR(E_UNKNOWN_SUBCMD, "Unknown command: %s", cmd);
}

static void portfolio_add_print_help(FILE *stream) {
    fprintf(stream,
            "graham portfolios add – Add command\n"
            "Usage: graham portfolios add [--OPTIONS]\n"
            "\n"
            "Add portfolio:\n"
            "  graham portfolios add --name PORTFOLIO_NAME --currency CURRENCY\n"
            "  PORTFOLIO_NAME:   1 < characters ≤ 30\n"
            "  CURRENCY:         1 < characters ≤ 3 (USD, EUR, GBP, etc.)\n"
            "\n"
            "Options (add):\n"
            "  -n, --name PORTFOLIO_NAME   Portfolio name\n"
            "  -c, --currency CURRENCY     Currency\n"
            "  -h, --help Show help for graham portfolio add\n"
            "\n");
}

static ParseResult parse_portfolio_add_cmd(int *argc, char ***argv, Args *out) {
    out->opts.portfolio_opts.cmd = COMMAND_PORTFOLIOS_ADD;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'},
                                        {"name", required_argument, 0, 'n'},
                                        {"currency", required_argument, 0, 'c'},
                                        {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hn:c:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.portfolio_opts.opts.add.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'n':
                if (strlen(optarg) > PORTFOLIO_NAME_MAX_LENGTH_WITH_NULL - 1) {
                    return (ParseResult){E_BAD_OPTION_VALUE, "portfolio name too long"};
                }
                out->opts.portfolio_opts.opts.add.name = optarg;
                continue;
            case 'c':
                if (strlen(optarg) > CURRENCY_MAX_LENGTH_WITH_NULL - 1) {
                    return (ParseResult){E_BAD_OPTION_VALUE, "currency too long"};
                }
                out->opts.portfolio_opts.opts.add.currency = optarg;
                continue;
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "unknown option: %c", opt);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.portfolio_opts.opts.add.name == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Portfolio name is required"};
    }

    if (out->opts.portfolio_opts.opts.add.currency == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Portfolio currency is required"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_portfolio_add(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[256];
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM portfolios;");

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_UNKNOWN_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        if (count > 2) {
            return CMD_PORTFOLIO_MAX_COUNT_ERR;
        }
    }

    sqlite3_finalize(stmt);

    char uuid[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(uuid);

    char created_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
    epoch_ms_to_iso8601(get_current_timestamp_ms(), created_at);

    Portfolio portfolio = {0};
    snprintf(portfolio.name, sizeof portfolio.name, "%s", args.opts.portfolio_opts.opts.add.name);
    snprintf(portfolio.currency, sizeof portfolio.currency, "%s",
             args.opts.portfolio_opts.opts.add.currency);

    snprintf(portfolio.id, sizeof portfolio.id, "%s", uuid);
    snprintf(portfolio.created_at, sizeof portfolio.created_at, "%s", created_at);
    snprintf(portfolio.updated_at, sizeof portfolio.updated_at, "%s", created_at);

    snprintf(sql, sizeof sql,
             "INSERT INTO portfolios (id, name, currency, created_at, updated_at) VALUES ('%s', "
             "'%s', '%s', '%s', '%s');",
             portfolio.id, portfolio.name, portfolio.currency, portfolio.created_at,
             portfolio.updated_at);

    if (exec_sql(db, sql) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    sqlite3_close(db);

    return CMD_OK;
}

static void portfolio_list_print_help(FILE *stream) {
    fprintf(stream,
            "graham portfolios list – List command\n"
            "Usage: graham portfolios list [--OPTIONS]\n"
            "\n"
            "List portfolios:\n"
            "  graham portfolios list\n"
            "\n"
            "Options (list):\n"
            "  -h, --help Show help for graham portfolios list\n"
            "\n");
}

static ParseResult parse_portfolio_list_cmd(int *argc, char ***argv, Args *out) {
    out->opts.portfolio_opts.cmd = COMMAND_PORTFOLIOS_LIST;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.portfolio_opts.opts.list.help = true;
                return (ParseResult){PARSE_OK, ""};
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "unknown option: %c", opt);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_portfolio_list(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[256];
    snprintf(sql, sizeof sql,
             "SELECT id, name, currency, created_at, updated_at FROM portfolios ORDER BY "
             "created_at DESC;");

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    fprintf(stdout, "%-20s %-10s %-25s %-25s\n", "Name", "Currency", "Created At", "Updated At");
    fprintf(stdout, "%-20s %-10s %-25s %-25s\n", "--------------------", "----------",
            "-------------------------", "-------------------------");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        const unsigned char *currency = sqlite3_column_text(stmt, 2);
        const unsigned char *created_at = sqlite3_column_text(stmt, 3);
        const unsigned char *updated_at = sqlite3_column_text(stmt, 4);

        fprintf(stdout, "%-20s %-10s %-25s %-25s\n", name, currency, created_at, updated_at);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return CMD_OK;
}

static void portfolio_rm_print_help(FILE *stream) {
    fprintf(stream,
            "graham portfolios rm – Remove command\n"
            "Usage: graham portfolios rm [--OPTIONS]\n"
            "\n"
            "Remove portfolio:\n"
            "  graham portfolios rm --name NAME\n"
            "\n"
            "Options (list):\n"
            "  -n, --name PORTFOLIO_NAME   Portfolio name\n"
            "  -h, --help Show help for graham portfolios rm\n"
            "\n");
}

static ParseResult parse_portfolio_rm_cmd(int *argc, char ***argv, Args *out) {
    out->opts.portfolio_opts.cmd = COMMAND_PORTFOLIOS_RM;

    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'}, {"name", required_argument, 0, 'n'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hn:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.portfolio_opts.opts.rm.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'n':
                out->opts.portfolio_opts.opts.rm.name = optarg;
                continue;
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "unknown option: %c", opt);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.portfolio_opts.opts.rm.name == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "portfolio name is required"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_portfolio_rm(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[256];
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM portfolios WHERE name = '%s';",
             args.opts.portfolio_opts.opts.rm.name);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        if (count == 0) {
            return CMD_PORTFOLIO_NOT_FOUND_ERR;
        }
    }

    sqlite3_finalize(stmt);

    snprintf(sql, sizeof sql, "DELETE FROM portfolios WHERE name = '%s';",
             args.opts.portfolio_opts.opts.rm.name);

    if (exec_sql(db, sql) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    sqlite3_close(db);

    return CMD_OK;
}

static void asset_print_help(FILE *stream) {
    fprintf(stream,
            "graham assets – Asset command\n"
            "Usage: graham assets COMMAND [--OPTIONS]\n"
            "\n"
            "Commands:\n"
            "  add           Add an asset\n"
            "  list          List all assets\n"
            "  rm            Remove an asset\n"
            "\n"
            "Options (add):\n"
            "  -h, --help Show help for graham assets\n"
            "\n");
}

static ParseResult parse_asset_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_ASSETS;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.asset_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "unknown option: %c", opt);
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc == 0) {
        RETURN_PARSE_ERR(E_NO_SUBCMD, "missing subcommand for command %s", cmd_name[out->cmd]);
    }

    char *cmd = (*argv)[0];

    if (strcmp(cmd, "add") == 0) {
        return parse_asset_add_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "list") == 0) {
        return parse_asset_list_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "rm") == 0) {
        return parse_asset_rm_cmd(argc, argv, out);
    }

    RETURN_PARSE_ERR(E_UNKNOWN_SUBCMD, "Unknown command: %s", cmd);
}

static void asset_add_print_help(FILE *stream) {
    fprintf(stream,
            "graham assets add – Add command\n"
            "Usage: graham assets add [--OPTIONS]\n"
            "\n"
            "Add an asset:\n"
            "  graham assets add --symbol SYMBOL --name NAME --type TYPE --tax-exempt\n"
            "\n"
            "Options (add):\n"
            "  -s, --symbol ASSET_SYMBOL   Asset symbol\n"
            "  -n, --name ASSET_NAME   Asset name\n"
            "  -t, --type ASSET_TYPE   Asset type\n"
            "  -x, --tax-exempt       Asset is tax-exempt\n"
            "  -h, --help Show help for graham assets\n"
            "\n"
            "Asset types:\n"
            "  stock   Stock\n"
            "  bond    Bond\n"
            "  etf     ETF\n"
            "  cash    Cash\n"
            "\n");
}

static ParseResult parse_asset_add_cmd(int *argc, char ***argv, Args *out) {
    out->opts.asset_opts.cmd = COMMAND_ASSETS_ADD;
    out->opts.asset_opts.opts.add.type = ASSET_TYPE_UNSET;

    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},       {"symbol", required_argument, 0, 's'},
        {"name", required_argument, 0, 'n'}, {"type", required_argument, 0, 't'},
        {"tax-exempt", no_argument, 0, 'x'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hs:n:t:x", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.asset_opts.opts.add.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 's':
                if (strlen(optarg) > ASSET_SYMBOL_MAX_LENGTH_WITH_NULL - 1) {
                    RETURN_PARSE_ERR(E_BAD_OPTION, "symbol too long: %s", optarg);
                }
                out->opts.asset_opts.opts.add.symbol = optarg;
                continue;
            case 'n':
                if (strlen(optarg) > ASSET_NAME_MAX_LENGTH_WITH_NULL - 1) {
                    RETURN_PARSE_ERR(E_BAD_OPTION, "name too long: %s", optarg);
                }
                out->opts.asset_opts.opts.add.name = optarg;
                continue;
            case 't':
                if (strcmp(optarg, "stock") == 0) {
                    out->opts.asset_opts.opts.add.type = ASSET_TYPE_STOCK;
                } else if (strcmp(optarg, "bond") == 0) {
                    out->opts.asset_opts.opts.add.type = ASSET_TYPE_BOND;
                } else if (strcmp(optarg, "etf") == 0) {
                    out->opts.asset_opts.opts.add.type = ASSET_TYPE_ETF;
                } else if (strcmp(optarg, "cash") == 0) {
                    out->opts.asset_opts.opts.add.type = ASSET_TYPE_CASH;
                } else {
                    RETURN_PARSE_ERR(E_BAD_OPTION, "unknown asset type: %s", optarg);
                }
                continue;
            case 'x':
                out->opts.asset_opts.opts.add.tax_exempt = true;
                continue;
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "unknown option: %c", opt);
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.asset_opts.opts.add.symbol == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Asset symbol is required"};
    }

    if (out->opts.asset_opts.opts.add.name == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Asset name is required"};
    }

    if (out->opts.asset_opts.opts.add.type == ASSET_TYPE_UNSET) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Asset type is required"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_asset_add(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[256];

    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM assets WHERE symbol = '%s' OR name = '%s';",
             args.opts.asset_opts.opts.add.symbol, args.opts.asset_opts.opts.add.name);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        if (count > 0) {
            return CMD_ASSET_ALREADY_EXISTS_ERR;
        }
    }

    char uuid[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(uuid);

    char created_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
    epoch_ms_to_iso8601(get_current_timestamp_ms(), created_at);

    snprintf(sql, sizeof sql,
             "INSERT INTO assets (id, symbol, name, type, tax_exempt, created_at, updated_at) "
             "VALUES "
             "('%s', '%s',"
             "'%s', '%s', %d, '%s', '%s');",
             uuid, args.opts.asset_opts.opts.add.symbol, args.opts.asset_opts.opts.add.name,
             asset_type_name[args.opts.asset_opts.opts.add.type],
             args.opts.asset_opts.opts.add.tax_exempt, created_at, created_at);

    if (exec_sql(db, sql) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    sqlite3_close(db);

    return CMD_OK;
}

static void asset_list_print_help(FILE *stream) {
    fprintf(stream,
            "graham assets list – List command\n"
            "Usage: graham assets list [--OPTIONS]\n"
            "\n"
            "List all assets:\n"
            "  graham assets list\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham assets list\n"
            "\n");
}

static ParseResult parse_asset_list_cmd(int *argc, char ***argv, Args *out) {
    out->opts.asset_opts.cmd = COMMAND_ASSETS_LIST;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.asset_opts.opts.list.help = true;
                return (ParseResult){PARSE_OK, ""};
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "too many arguments: %s", (*argv)[0]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_asset_list(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[256];

    snprintf(sql, sizeof sql, "SELECT * FROM assets;");

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    fprintf(stdout, "%-38s %-6s %-31s %-6s %-12s %-25s %-25s\n", "ID", "Symbol", "Name", "Type",
            "Tax Exempt", "Created At", "Updated At");
    fprintf(stdout, "%-38s %-6s %-31s %-6s %-12s %-25s %-25s\n",
            "--------------------------------------", "------", "-------------------------------",
            "------", "------------", "-------------------------", "-------------------------");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        const char *symbol = (const char *)sqlite3_column_text(stmt, 1);
        const char *name = (const char *)sqlite3_column_text(stmt, 2);
        const char *type = (const char *)sqlite3_column_text(stmt, 3);
        const char *tax_exempt_str = sqlite3_column_int(stmt, 4) ? "true" : "false";
        const char *created_at = (const char *)sqlite3_column_text(stmt, 5);
        const char *updated_at = (const char *)sqlite3_column_text(stmt, 6);

        fprintf(stdout, "%-38s %-6s %-31s %-6s %-12s %-25s %-25s\n", id, symbol, name, type,
                tax_exempt_str, created_at, updated_at);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return CMD_OK;
}

static void asset_rm_print_help(FILE *stream) {
    fprintf(stream,
            "graham assets rm – Remove command\n"
            "Usage: graham assets rm [--OPTIONS]\n"
            "\n"
            "Remove an asset:\n"
            "  graham assets rm --symbol SYMBOL\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham assets rm\n"
            "\n");
}

static ParseResult parse_asset_rm_cmd(int *argc, char ***argv, Args *out) {
    out->opts.asset_opts.cmd = COMMAND_ASSETS_RM;

    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'}, {"symbol", required_argument, 0, 's'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hs:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.asset_opts.opts.rm.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 's':
                if (strlen(optarg) > ASSET_SYMBOL_MAX_LENGTH_WITH_NULL - 1) {
                    RETURN_PARSE_ERR(E_BAD_OPTION, "symbol too long: %s", optarg);
                }
                out->opts.asset_opts.opts.rm.symbol = optarg;
                continue;
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.asset_opts.opts.rm.symbol == NULL) {
        RETURN_PARSE_ERR(E_BAD_OPTION, "Asset symbol is required for command %s",
                         cmd_name[out->cmd]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_asset_rm(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[256];

    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM assets WHERE symbol = '%s';",
             args.opts.asset_opts.opts.rm.symbol);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        if (count == 0) {
            fprintf(stderr, "error: asset with symbol %s does not exist\n",
                    args.opts.asset_opts.opts.rm.symbol);
            return CMD_ASSET_NOT_FOUND_ERR;
        }
    }

    sqlite3_finalize(stmt);

    snprintf(sql, sizeof sql, "DELETE FROM assets WHERE symbol = '%s';",
             args.opts.asset_opts.opts.rm.symbol);

    if (exec_sql(db, sql) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    sqlite3_close(db);

    fprintf(stdout, "Removed asset %s\n", args.opts.asset_opts.opts.rm.symbol);

    return EXIT_SUCCESS;
}

static void account_print_help(FILE *stream) {
    fprintf(stream,
            "graham accounts – Account command\n"
            "Usage: graham accounts [--OPTIONS]\n"
            "\n"
            "Commands:\n"
            "  add           Add an account\n"
            "  list          List all accounts\n"
            "  rm            Remove an account\n"
            "  view          View an account\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham accounts\n"
            "\n");
}

static ParseResult parse_account_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_ACCOUNTS;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.account_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    const char *cmd = (*argv)[0];

    if (*argc == 0) {
        RETURN_PARSE_ERR(E_NO_SUBCMD, "missing subcommand for command %s", cmd_name[out->cmd]);
    }

    if (strcmp(cmd, "add") == 0) {
        return parse_account_add_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "list") == 0) {
        return parse_account_list_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "rm") == 0) {
        return parse_account_rm_cmd(argc, argv, out);
    }

    if (strcmp(cmd, "view") == 0) {
        return parse_account_view_cmd(argc, argv, out);
    }

    RETURN_PARSE_ERR(E_UNKNOWN_SUBCMD, "unknown command: %s", cmd);
}

static void account_add_print_help(FILE *stream) {
    fprintf(stream,
            "graham accounts add – Add command\n"
            "Usage: graham accounts add [--OPTIONS]\n"
            "\n"
            "Add an account:\n"
            "  graham accounts add --symbol SYMBOL --portfolio PORTFOLIO_NAME\n"
            "\n"
            "Options:\n"
            "  -p, --portfolio PORTFOLIO_NAME The portfolio to list accounts for\n"
            "  -s, --symbol SYMBOL The symbol of the account to list\n"
            "  -h, --help Show help for graham accounts add\n"
            "\n");
}

static ParseResult parse_account_add_cmd(int *argc, char ***argv, Args *out) {
    out->opts.account_opts.cmd = COMMAND_ACCOUNTS_ADD;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'},
                                        {"asset", required_argument, 0, 'a'},
                                        {"portfolio", required_argument, 0, 'p'},
                                        {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "ha:p:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.account_opts.opts.add.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'a':
                if (strlen(optarg) > ASSET_SYMBOL_MAX_LENGTH_WITH_NULL - 1) {
                    RETURN_PARSE_ERR(E_BAD_OPTION, "symbol too long: %s", optarg);
                }
                out->opts.account_opts.opts.add.asset = optarg;
                continue;
            case 'p':
                if (strlen(optarg) > PORTFOLIO_NAME_MAX_LENGTH_WITH_NULL - 1) {
                    RETURN_PARSE_ERR(E_BAD_OPTION, "portfolio too long: %s", optarg);
                }
                out->opts.account_opts.opts.add.portfolio = optarg;
                continue;
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.account_opts.opts.add.asset == NULL) {
        RETURN_PARSE_ERR(E_BAD_OPTION, "Account asset is required for command %s",
                         cmd_name[out->cmd]);
    }

    if (out->opts.account_opts.opts.add.portfolio == NULL) {
        RETURN_PARSE_ERR(E_BAD_OPTION, "Account portfolio is required for command %s",
                         cmd_name[out->cmd]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static int count_portfolios(sqlite3 *db, const char *portfolio_name) {
    char sql[80];

    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM portfolios WHERE name = '%s';", portfolio_name);
    sqlite3_stmt *stmt;

    int e = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (e != SQLITE_OK) {
        return -1;
    }

    int count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

static CmdResultCode get_portfolio_id(sqlite3 *db, const char *portfolio_name,
                                      char portfolio_id[UUID_MAX_LENGTH_WITH_NULL]) {
    char sql[80];
    snprintf(sql, sizeof sql, "SELECT id FROM portfolios WHERE name = '%s';", portfolio_name);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(portfolio_id, UUID_MAX_LENGTH_WITH_NULL, "%s",
                 (char *)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return CMD_OK;
}

static int count_assets(sqlite3 *db, const char *asset_symbol) {
    char sql[80];

    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM assets WHERE symbol = '%s';", asset_symbol);
    sqlite3_stmt *stmt;

    int e = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (e != SQLITE_OK) {
        return -1;
    }

    int count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

static CmdResultCode get_asset_id(sqlite3 *db, const char *asset_symbol,
                                  char asset_id[UUID_MAX_LENGTH_WITH_NULL]) {
    char sql[80];
    snprintf(sql, sizeof sql, "SELECT id FROM assets WHERE symbol = '%s';", asset_symbol);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(asset_id, UUID_MAX_LENGTH_WITH_NULL, "%s", (char *)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return CMD_OK;
}

static CmdResultCode get_account_dependencies(sqlite3 *db, const char *portfolio_name,
                                              const char *asset_symbol,
                                              char portfolio_id[UUID_MAX_LENGTH_WITH_NULL],
                                              char asset_id[UUID_MAX_LENGTH_WITH_NULL]) {
    int portfolio_count = count_portfolios(db, portfolio_name);
    if (portfolio_count == -1) {
        return CMD_DB_ERR;
    }

    if (portfolio_count == 0) {
        return CMD_PORTFOLIO_NOT_FOUND_ERR;
    }

    if (portfolio_count != 1) {
        return CMD_MULTIPLE_PORTFOLIOS_FOUND_ERR;
    }

    CmdResultCode err = get_portfolio_id(db, portfolio_name, portfolio_id);
    if (err != CMD_OK) {
        return err;
    }

    int asset_count = count_assets(db, asset_symbol);
    if (asset_count == -1) {
        return CMD_DB_ERR;
    }

    if (asset_count == 0) {
        return CMD_ASSET_NOT_FOUND_ERR;
    }

    if (asset_count != 1) {
        return CMD_MULTIPLE_ASSETS_FOUND_ERR;
    }

    err = get_asset_id(db, asset_symbol, asset_id);
    if (err != CMD_OK) {
        return err;
    }

    return CMD_OK;
}

static CmdResultCode account_exists(sqlite3 *db, const char *portfolio_id, const char *asset_id) {
    char sql[152];
    snprintf(sql, sizeof sql,
             "SELECT COUNT(*) FROM accounts WHERE asset_id = '%s' AND portfolio_id = '%s';",
             asset_id, portfolio_id);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    int count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        if (count > 0) {
            return CMD_ACCOUNT_EXISTS_ERR;
        }
        return CMD_ACCOUNT_NOT_FOUND_ERR;
    }

    sqlite3_finalize(stmt);
    return CMD_DB_ERR;
}

static CmdResultCode cmd_account_add(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char portfolio_id[UUID_MAX_LENGTH_WITH_NULL];
    char asset_id[UUID_MAX_LENGTH_WITH_NULL];

    CmdResultCode err =
        get_account_dependencies(db, args.opts.account_opts.opts.add.portfolio,
                                 args.opts.account_opts.opts.add.asset, portfolio_id, asset_id);
    if (err != CMD_OK) {
        return err;
    }

    err = account_exists(db, portfolio_id, asset_id);
    if (err != CMD_ACCOUNT_NOT_FOUND_ERR) {
        return err;
    }

    char uuid[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(uuid);

    char created_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
    epoch_ms_to_iso8601(get_current_timestamp_ms(), created_at);

    char sql[262];
    snprintf(sql, sizeof sql,
             "INSERT INTO accounts (id, asset_id, portfolio_id, created_at, updated_at) VALUES "
             "('%s', '%s', '%s', '%s', '%s');",
             uuid, asset_id, portfolio_id, created_at, created_at);

    if (exec_sql(db, sql) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    sqlite3_close(db);

    return CMD_OK;
}

static void account_list_print_help(FILE *stream) {
    fprintf(stream,
            "graham accounts list – List command\n"
            "Usage: graham accounts list [--OPTIONS]\n"
            "\n"
            "List all accounts:\n"
            "  graham accounts list\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham account list\n"
            "\n");
}

static ParseResult parse_account_list_cmd(int *argc, char ***argv, Args *out) {
    out->opts.account_opts.cmd = COMMAND_ACCOUNTS_LIST;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.account_opts.opts.list.help = true;
                return (ParseResult){PARSE_OK, ""};
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_account_list(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char sql[275];
    snprintf(sql, sizeof sql,
             "SELECT accounts.id, assets.symbol, portfolios.name, assets.tax_exempt, "
             "accounts.created_at, accounts.updated_at "
             "FROM accounts "
             "INNER JOIN assets ON accounts.asset_id = assets.id "
             "INNER JOIN portfolios ON accounts.portfolio_id = portfolios.id "
             "ORDER BY accounts.created_at DESC;");

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    fprintf(stdout, "%-38s %-6s %-31s %-11s %-25s %-25s\n", "ID", "Symbol", "Portfolio",
            "Tax Exempt", "Created At", "Updated At");
    fprintf(stdout, "%-38s %-6s %-31s %-6s %-25s %-25s\n", "--------------------------------------",
            "------", "-------------------------------", "-----------", "-------------------------",
            "-------------------------");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        const char *symbol = (const char *)sqlite3_column_text(stmt, 1);
        const char *portfolio = (const char *)sqlite3_column_text(stmt, 2);
        const char *tax_exempt_str = sqlite3_column_int(stmt, 3) ? "true" : "false";
        const char *created_at = (const char *)sqlite3_column_text(stmt, 4);
        const char *updated_at = (const char *)sqlite3_column_text(stmt, 5);

        fprintf(stdout, "%-38s %-6s %-31s %-11s %-25s %-25s\n", id, symbol, portfolio,
                tax_exempt_str, created_at, updated_at);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return CMD_OK;
}

static void account_rm_print_help(FILE *stream) {
    fprintf(stream,
            "graham accounts rm – Remove command\n"
            "Usage: graham accounts rm [--OPTIONS]\n"
            "\n"
            "Remove an account:\n"
            "  graham accounts rm --symbol SYMBOL --portfolio PORTFOLIO_NAME\n"
            "\n"
            "Options:\n"
            "  -s, --symbol SYMBOL The symbol of the account to remove\n"
            "  -p, --portfolio PORTFOLIO_NAME The portfolio of the account to remove\n"
            "  -h, --help Show help for graham accounts rm\n"
            "\n");
}

static ParseResult parse_account_rm_cmd(int *argc, char ***argv, Args *out) {
    out->opts.account_opts.cmd = COMMAND_ACCOUNTS_RM;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'},
                                        {"asset", required_argument, 0, 'a'},
                                        {"portfolio", required_argument, 0, 'p'},
                                        {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "ha:p:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.account_opts.opts.rm.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'a':
                out->opts.account_opts.opts.rm.asset = optarg;
                continue;
            case 'p':
                out->opts.account_opts.opts.rm.portfolio = optarg;
                continue;
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.account_opts.opts.rm.asset == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Account asset symbol is required"};
    }

    if (out->opts.account_opts.opts.rm.portfolio == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Account portfolio is required"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_account_rm(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char portfolio_id[UUID_MAX_LENGTH_WITH_NULL];
    char asset_id[UUID_MAX_LENGTH_WITH_NULL];

    CmdResultCode err =
        get_account_dependencies(db, args.opts.account_opts.opts.rm.portfolio,
                                 args.opts.account_opts.opts.rm.asset, portfolio_id, asset_id);
    if (err != CMD_OK) {
        return err;
    }

    err = account_exists(db, portfolio_id, asset_id);
    if (err != CMD_ACCOUNT_EXISTS_ERR) {
        return err;
    }

    char sql[145];

    snprintf(sql, sizeof sql, "DELETE FROM accounts WHERE asset_id = '%s' AND portfolio_id = '%s';",
             asset_id, portfolio_id);

    if (exec_sql(db, sql) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    sqlite3_close(db);

    fprintf(stdout, "%s account in portfolio %s removed successfully\n",
            args.opts.account_opts.opts.rm.asset, args.opts.account_opts.opts.rm.portfolio);

    return CMD_OK;
}

static void account_view_print_help(FILE *stream) {
    fprintf(stream,
            "graham accounts view – View command\n"
            "Usage: graham accounts view [--OPTIONS]\n"
            "\n"
            "View an account:\n"
            "  graham accounts view --portfolio PORTFOLIO_NAME --asset ASSET_SYMBOL\n"
            "\n"
            "Options:\n"
            "  -p, --portfolio PORTFOLIO_NAME The portfolio of the account to view\n"
            "  -a, --asset ASSET_SYMBOL The asset of the account to view\n"
            "  -h, --help Show help for graham accounts view\n"
            "\n");
}

static ParseResult parse_account_view_cmd(int *argc, char ***argv, Args *out) {
    out->opts.account_opts.cmd = COMMAND_ACCOUNTS_VIEW;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'},
                                        {"portfolio", required_argument, 0, 'p'},
                                        {"asset", required_argument, 0, 'a'},
                                        {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hp:a:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.account_opts.opts.view.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'p':
                out->opts.account_opts.opts.view.portfolio = optarg;
                continue;
            case 'a':
                out->opts.account_opts.opts.view.asset = optarg;
                continue;
            default:
                return (ParseResult){E_BAD_OPTION_VALUE, "Invalid option"};
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.account_opts.opts.view.portfolio == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Portfolio is required"};
    }

    if (out->opts.account_opts.opts.view.asset == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Asset is required"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode get_account_id(sqlite3 *db, const char *portfolio_id, const char *asset_id,
                                    char account_id[UUID_MAX_LENGTH_WITH_NULL]) {
    int err = account_exists(db, portfolio_id, asset_id);
    if (err != CMD_ACCOUNT_EXISTS_ERR) {
        return err;
    }

    char sql[80];
    snprintf(sql, sizeof sql,
             "SELECT id FROM accounts WHERE asset_id = '%s' AND portfolio_id = '%s';", asset_id,
             portfolio_id);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(account_id, UUID_MAX_LENGTH_WITH_NULL, "%s", (char *)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return CMD_OK;
}

static int64_t get_account_value(sqlite3 *db, const char *account_id, int64_t *amount) {
    char sql[167];
    snprintf(sql, sizeof sql,
             "SELECT SUM(amount) FROM entries INNER JOIN accounts ON "
             "entries.account_id = accounts.id WHERE accounts.id = '%s';",
             account_id);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        return SQLITE_ERROR;
    }

    *amount = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return CMD_OK;
}

static CmdResultCode cmd_account_view(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char portfolio_id[UUID_MAX_LENGTH_WITH_NULL];
    char asset_id[UUID_MAX_LENGTH_WITH_NULL];

    CmdResultCode err =
        get_account_dependencies(db, args.opts.account_opts.opts.view.portfolio,
                                 args.opts.account_opts.opts.view.asset, portfolio_id, asset_id);
    if (err != CMD_OK) {
        return err;
    }

    err = account_exists(db, portfolio_id, asset_id);
    if (err != CMD_ACCOUNT_EXISTS_ERR) {
        return err;
    }

    char account_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_account_id(db, portfolio_id, asset_id, account_id);
    if (err != CMD_OK) {
        return err;
    }

    int64_t account_value;
    err = get_account_value(db, account_id, &account_value);
    if (err != CMD_OK) {
        return err;
    }

    // Print dashboard header
    fprintf(stdout, "\n┌─────────────────────────────────────────────────────────────┐\n");
    fprintf(stdout, "│                     Account Dashboard                       │\n");
    fprintf(stdout, "├─────────────────────────────────────────────────────────────┤\n");

    // Print account details
    fprintf(stdout, "│ %-15s %-43s │\n", "Asset Symbol:", args.opts.account_opts.opts.view.asset);
    fprintf(stdout, "│ %-15s %-43s │\n", "Portfolio:", args.opts.account_opts.opts.view.portfolio);
    fprintf(stdout, "│ %-15s %-43ld │\n", "Quantity:", account_value);

    // Print footer
    fprintf(stdout, "└─────────────────────────────────────────────────────────────┘\n\n");

    sqlite3_close(db);

    return CMD_OK;
}

static void transaction_print_help(FILE *stream) {
    fprintf(stream,
            "graham transactions – Transactions command\n"
            "Usage: graham transactions COMMAND [--OPTIONS]\n"
            "\n"
            "Commands:\n"
            "  add Adds a transaction that records the movement of value from one account to "
            "another\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham transactions\n"
            "\n");
}

static ParseResult parse_transaction_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_TRANSACTIONS;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.transaction_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc == 0) {
        RETURN_PARSE_ERR(E_NO_SUBCMD, "Missing subcommand for command %s", cmd_name[out->cmd]);
    }

    if (strcmp((*argv)[0], "add") == 0) {
        return parse_transaction_add_cmd(argc, argv, out);
    }

    RETURN_PARSE_ERR(E_UNKNOWN_COMMAND, "Unknown command: %s", (*argv)[0]);
}

static void transaction_add_print_help(FILE *stream) {
    fprintf(stream,
            "graham transactions add – Add a transaction that records the movement of value from "
            "one account to another\n"
            "\n"
            "Usage: graham transactions add --portfolio PORTFOLIO_NAME --to-asset ASSET_SYMBOL "
            "--to-amount TO_AMOUNT --from-asset FROM_ASSET_SYMBOL --from-amount FROM_AMOUNT "
            "--date DATE\n"
            "\n"
            "Options:\n"
            "  -p, --portfolio PORTFOLIO_NAME The portfolio of the transaction\n"
            "  -t, --to-asset ASSET_SYMBOL The asset to move value to\n"
            "  -a, --to-amount TO_AMOUNT The amount of value to move to the to asset\n"
            "  -f, --from-asset FROM_ASSET_SYMBOL The asset to move value from\n"
            "  -b, --from-amount FROM_AMOUNT The amount of value to move from the from asset\n"
            "  -d, --date DATE The date of the transaction\n"
            "\n");
}

static ParseResult parse_transaction_add_cmd(int *argc, char ***argv, Args *out) {
    out->opts.transaction_opts.cmd = COMMAND_TRANSACTIONS_ADD;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'},
                                        {"portfolio", required_argument, 0, 'p'},
                                        {"to-asset", required_argument, 0, 't'},
                                        {"to-amount", required_argument, 0, 'a'},
                                        {"from-asset", required_argument, 0, 'f'},
                                        {"from-amount", required_argument, 0, 'b'},
                                        {"date", required_argument, 0, 'd'},
                                        {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hp:t:a:f:b:d:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.transaction_opts.opts.add.help = true;
                return (ParseResult){PARSE_OK, ""};
            case 'p':
                out->opts.transaction_opts.opts.add.portfolio = optarg;
                continue;
            case 't':
                out->opts.transaction_opts.opts.add.to_asset = optarg;
                continue;
            case 'a':
                out->opts.transaction_opts.opts.add.to_amount = optarg;
                continue;
            case 'f':
                out->opts.transaction_opts.opts.add.from_asset = optarg;
                continue;
            case 'b':
                out->opts.transaction_opts.opts.add.from_amount = optarg;
                continue;
            case 'd':
                out->opts.transaction_opts.opts.add.date = optarg;
                continue;
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION_VALUE, "Unknown option: %s", (*argv)[optind - 1]);
        }
    }
    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments: %s", (*argv)[0]);
    }

    if (out->opts.transaction_opts.opts.add.portfolio == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Portfolio is required"};
    }

    if (out->opts.transaction_opts.opts.add.to_asset == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "To asset is required"};
    }

    if (out->opts.transaction_opts.opts.add.from_asset == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "From asset is required"};
    }

    if (out->opts.transaction_opts.opts.add.from_amount == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "From amount is required"};
    }

    if (out->opts.transaction_opts.opts.add.date == NULL) {
        return (ParseResult){E_BAD_OPTION_VALUE, "Date is required"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_transaction_add(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    char portfolio_id[UUID_MAX_LENGTH_WITH_NULL];
    int err = get_portfolio_id(db, args.opts.transaction_opts.opts.add.portfolio, portfolio_id);
    if (err != CMD_OK) {
        return err;
    }

    char from_asset_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_asset_id(db, args.opts.transaction_opts.opts.add.from_asset, from_asset_id);
    if (err != CMD_OK) {
        return err;
    }

    char to_asset_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_asset_id(db, args.opts.transaction_opts.opts.add.to_asset, to_asset_id);
    if (err != CMD_OK) {
        return err;
    }

    char from_account_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_account_id(db, portfolio_id, from_asset_id, from_account_id);
    if (err != CMD_OK) {
        if (err == CMD_ACCOUNT_NOT_FOUND_ERR) {
            return CMD_FROM_ACCOUNT_NOT_FOUND_ERR;
        }
        return err;
    }

    int64_t from_account_value;
    err = get_account_value(db, from_account_id, &from_account_value);
    if (err != CMD_OK) {
        return err;
    }

    if (from_account_value < atoi(args.opts.transaction_opts.opts.add.from_amount)) {
        return CMD_INSUFFICIENT_FUNDS_ERR;
    }

    char to_account_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_account_id(db, portfolio_id, to_asset_id, to_account_id);
    if (err != CMD_OK) {
        if (err == CMD_ACCOUNT_NOT_FOUND_ERR) {
            return CMD_TO_ACCOUNT_NOT_FOUND_ERR;
        }
        return err;
    }

    if (exec_sql(db, "BEGIN;") != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    char transaction_id[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(transaction_id);

    char created_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
    epoch_ms_to_iso8601(get_current_timestamp_ms(), created_at);

    char sql[246];
    snprintf(sql, sizeof sql,
             "INSERT INTO transactions (id, created_at, transaction_date) "
             "VALUES ('%s', '%s', '%s');",
             transaction_id, created_at, args.opts.transaction_opts.opts.add.date);
    if (exec_sql(db, sql) != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    char from_entry_id[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(from_entry_id);
    int64_t from_amount = atoi(args.opts.transaction_opts.opts.add.from_amount) * -1;
    snprintf(sql, sizeof sql,
             "INSERT INTO entries (id, created_at, transaction_id, account_id, amount) "
             "VALUES ('%s', '%s', '%s', '%s', %ld);",
             from_entry_id, created_at, transaction_id, from_account_id, from_amount);
    if (exec_sql(db, sql) != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    char to_entry_id[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(to_entry_id);
    int64_t to_amount = atoi(args.opts.transaction_opts.opts.add.to_amount);
    snprintf(sql, sizeof sql,
             "INSERT INTO entries (id, created_at, transaction_id, account_id, amount) "
             "VALUES ('%s', '%s', '%s', '%s', %ld);",
             to_entry_id, created_at, transaction_id, to_account_id, to_amount);
    if (exec_sql(db, sql) != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    if (exec_sql(db, "COMMIT;") != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    return CMD_OK;
}

static void transfer_print_help(FILE *stream) {
    fprintf(stream,
            "graham transfers – Transfers command\n"
            "Usage: graham transfers COMMAND [--OPTIONS]\n"
            "\n"
            "Commands:\n"
            "  add Adds transactions that record the deposit or withdrawal of value into or from "
            "an account\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham transfers\n"
            "\n");
}

static ParseResult parse_transfer_cmd(int *argc, char ***argv, Args *out) {
    out->cmd = COMMAND_TRANSFERS;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "+h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.transfer_opts.help = true;
                return (ParseResult){PARSE_OK, ""};
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc == 0) {
        RETURN_PARSE_ERR(E_NO_SUBCMD, "Missing subcommand for command %s", cmd_name[out->cmd]);
    }

    if (strcmp((*argv)[0], "add") == 0) {
        return parse_transfer_add_cmd(argc, argv, out);
    }

    RETURN_PARSE_ERR(E_UNKNOWN_COMMAND, "Unknown command: %s", (*argv)[0]);
}

static void transfer_add_print_help(FILE *stream) {
    fprintf(stream,
            "graham transfers add – Adds transactions that record the deposit or withdrawal of "
            "value into or from an account\n"
            "Usage: graham transfers add --portfolio PORTFOLIO_NAME --asset ASSET_SYMBOL "
            "--amount AMOUNT --date DATE [--withdrawal|--deposit] [--OPTIONS]\n"
            "\n"
            "Options:\n"
            "  -h, --help Show help for graham transfers add\n"
            "  -d, --deposit The transfer is a deposit\n"
            "  -w, --withdrawal The transfer is a withdrawal\n"
            "  -a, --amount AMOUNT The amount of the transfer\n"
            "  -p, --portfolio PORTFOLIO_NAME The name of the portfolio to add the transfer to\n"
            "  -s, --asset ASSET_SYMBOL The symbol of the asset to add the transfer to\n"
            "  -t, --date DATE The date of the transfer\n"
            "\n");
}

static ParseResult parse_transfer_add_cmd(int *argc, char ***argv, Args *out) {
    out->opts.transfer_opts.cmd = COMMAND_TRANSFERS_ADD;

    static struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},        {"deposit", no_argument, 0, 'd'},
        {"withdrawal", no_argument, 0, 'w'},  {"portfolio", required_argument, 0, 'p'},
        {"asset", required_argument, 0, 's'}, {"amount", required_argument, 0, 'a'},
        {"date", required_argument, 0, 't'},  {0, 0, 0, 0}};
    int opt;
    while ((opt = getopt_long(*argc, *argv, "hdwp:s:a:t:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
                out->opts.transfer_opts.opts.add.help = true;
                continue;
            case 'd':
                out->opts.transfer_opts.opts.add.deposit = true;
                continue;
            case 'w':
                out->opts.transfer_opts.opts.add.withdrawal = true;
                continue;
            case 'p':
                out->opts.transfer_opts.opts.add.portfolio_name = optarg;
                continue;
            case 's':
                out->opts.transfer_opts.opts.add.asset_symbol = optarg;
                continue;
            case 't':
                out->opts.transfer_opts.opts.add.date = optarg;
                continue;
            case 'a':
                out->opts.transfer_opts.opts.add.amount = optarg;
                continue;
            default:
                RETURN_PARSE_ERR(E_BAD_OPTION, "Invalid option: -%c", opt);
        }
    }

    *argc -= optind;
    *argv += optind;
    optind = 1;

    if (*argc != 0) {
        RETURN_PARSE_ERR(E_TOO_MANY_ARGS, "Too many arguments for command %s", cmd_name[out->cmd]);
    }

    if (!out->opts.transfer_opts.opts.add.deposit && !out->opts.transfer_opts.opts.add.withdrawal) {
        return (ParseResult){E_MISSING_REQUIRED, "Missing --deposit or --withdrawal option"};
    }

    if (out->opts.transfer_opts.opts.add.deposit && out->opts.transfer_opts.opts.add.withdrawal) {
        return (ParseResult){E_BAD_OPTION,
                             "Cannot specify both --deposit and --withdrawal options"};
    }

    if (!out->opts.transfer_opts.opts.add.portfolio_name) {
        return (ParseResult){E_MISSING_REQUIRED, "Missing --portfolio option"};
    }

    if (!out->opts.transfer_opts.opts.add.asset_symbol) {
        return (ParseResult){E_MISSING_REQUIRED, "Missing --asset option"};
    }

    if (!out->opts.transfer_opts.opts.add.amount) {
        return (ParseResult){E_MISSING_REQUIRED, "Missing --amount option"};
    }

    if (!out->opts.transfer_opts.opts.add.date) {
        return (ParseResult){E_MISSING_REQUIRED, "Missing --date option"};
    }

    return (ParseResult){PARSE_OK, ""};
}

static CmdResultCode cmd_transfer_add(Args args) {
    sqlite3 *db;
    sqlite3_open("graham.db", &db);

    int portfolio_count = count_portfolios(db, args.opts.transfer_opts.opts.add.portfolio_name);
    if (portfolio_count == 0) {
        return CMD_PORTFOLIO_NOT_FOUND_ERR;
    }

    if (portfolio_count > 1) {
        return CMD_MULTIPLE_PORTFOLIOS_FOUND_ERR;
    }

    char portfolio_id[UUID_MAX_LENGTH_WITH_NULL];
    int err = get_portfolio_id(db, args.opts.transaction_opts.opts.add.portfolio, portfolio_id);
    if (err != CMD_OK) {
        return err;
    }

    int asset_count = count_assets(db, args.opts.transfer_opts.opts.add.asset_symbol);
    if (asset_count == 0) {
        return CMD_ASSET_NOT_FOUND_ERR;
    }

    if (asset_count > 1) {
        return CMD_MULTIPLE_ASSETS_FOUND_ERR;
    }

    char asset_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_asset_id(db, args.opts.transfer_opts.opts.add.asset_symbol, asset_id);
    if (err != CMD_OK) {
        return err;
    }

    char account_id[UUID_MAX_LENGTH_WITH_NULL];
    err = get_account_id(db, portfolio_id, asset_id, account_id);
    if (err != CMD_OK) {
        if (err == CMD_ACCOUNT_NOT_FOUND_ERR) {
            return CMD_ACCOUNT_NOT_FOUND_ERR;
        }
        return err;
    }

    if (exec_sql(db, "BEGIN;") != SQLITE_OK) {
        return CMD_DB_ERR;
    }

    char transaction_id[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(transaction_id);

    char created_at[TIMESTAMP_MAX_LENGTH_WITH_NULL];
    epoch_ms_to_iso8601(get_current_timestamp_ms(), created_at);

    char sql[246];
    snprintf(sql, sizeof sql,
             "INSERT INTO transactions (id, created_at, transaction_date) "
             "VALUES ('%s', '%s', '%s');",
             transaction_id, created_at, args.opts.transfer_opts.opts.add.date);
    if (exec_sql(db, sql) != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    char entry_id[UUID_MAX_LENGTH_WITH_NULL];
    generate_uuid_v7(entry_id);
    int64_t amount = atoi(args.opts.transfer_opts.opts.add.amount);
    snprintf(sql, sizeof sql,
             "INSERT INTO entries (id, created_at, transaction_id, account_id, amount) "
             "VALUES ('%s', '%s', '%s', '%s', %ld);",
             entry_id, created_at, transaction_id, account_id, amount);
    if (exec_sql(db, sql) != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    if (exec_sql(db, "COMMIT;") != SQLITE_OK) {
        exec_sql(db, "ROLLBACK;");
        return CMD_DB_ERR;
    }

    return CMD_OK;
}

AppMemory bytes_for_command(Args args) {
    AppMemory default_memory = {
        .app_memory = 8 * KB,
        .sqlite_page_cache = 4 * PAGE_SIZE,                            /* 16 KiB */
        .sqlite_heap = 64 * KB,                                        /* 64 KiB */
        .sqlite_lookaside = LOOKASIDE_SLOT_SIZE * LOOKASIDE_SLOT_COUNT /* 16 KiB */
    };

    if (args.cmd == COMMAND_INIT) {
        return default_memory;
    }
    if (args.cmd == COMMAND_MIGRATE) {
        return default_memory;
    }
    if (args.cmd == COMMAND_PORTFOLIOS) {
        return default_memory;
    }
    if (args.cmd == COMMAND_ASSETS) {
        return default_memory;
    }
    if (args.cmd == COMMAND_ACCOUNTS) {
        return default_memory;
    }
    if (args.cmd == COMMAND_TRANSACTIONS) {
        return default_memory;
    }
    if (args.cmd == COMMAND_TRANSFERS) {
        return default_memory;
    }
    return (AppMemory){0, 0, 0, 0};
}

void *allocate_memory(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed: %zu bytes\n", size);
        exit(1);
    }
    return ptr;
}

void setup_sqlite(uint8_t **p, AppMemory mem) {
    if (mem.sqlite_page_cache) {
        const int nPage = (int)(mem.sqlite_page_cache / PAGE_SIZE);
        sqlite3_config(SQLITE_CONFIG_PAGECACHE, *p, PAGE_SIZE, nPage);
        *p += mem.sqlite_page_cache;
    }

    if (mem.sqlite_heap) {
        const int minAlloc = 64;
        sqlite3_config(SQLITE_CONFIG_HEAP, *p, (int)mem.sqlite_heap, minAlloc);
        *p += mem.sqlite_heap;
    }

    if (mem.sqlite_lookaside) {
        const int slotSize = LOOKASIDE_SLOT_SIZE;
        int slotCount = (int)(mem.sqlite_lookaside / slotSize);
        if (slotCount == 0)
            slotCount = 1;
        sqlite3_config(SQLITE_CONFIG_LOOKASIDE, slotSize, slotCount);
    }

    sqlite3_config(SQLITE_CONFIG_SMALL_MALLOC, 1);
}

void setup_app_pool(uint8_t **p, size_t app_memory) { *p += app_memory; }

uint64_t get_current_timestamp_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void generate_uuid_v7(char *uuid_str) {
    uint64_t ts = get_current_timestamp_ms();
    uint64_t rnd = ((uint64_t)rand() << 48) | ((uint64_t)rand() << 32) | ((uint64_t)rand() << 16) |
                   (uint64_t)rand();

    uint32_t time_hi = (uint32_t)(ts >> 16);
    uint16_t time_low_ver = (uint16_t)((ts & 0xFFFF) | 0x7000);
    uint16_t rand_a = (uint16_t)(rnd >> 48);
    uint16_t rand_b = (uint16_t)(rnd >> 32);
    uint64_t rand_c = rnd & 0xFFFFFFFFFFFFULL; /* 48 bits, still uint64_t */

    /* 8-4-4-4-12 format */
    snprintf(uuid_str, UUID_MAX_LENGTH_WITH_NULL,
             "%08" PRIx32 "-%04" PRIx16 "-%04" PRIx16 "-%04" PRIx16 "-%012" PRIx64, time_hi,
             time_low_ver, rand_a, rand_b, rand_c);
}

void epoch_ms_to_iso8601(uint64_t ms, char iso[TIMESTAMP_MAX_LENGTH_WITH_NULL]) {
    time_t s = ms / 1000;    /* whole seconds            */
    int ms_part = ms % 1000; /* 0-999                    */
    struct tm tm;

    /* UTC ("Z") version; use localtime_r for local offset */
    gmtime_r(&s, &tm);

    /* First the YYYY-MM-DDTHH:MM:SS part */
    strftime(iso, TIMESTAMP_MAX_LENGTH_WITH_NULL, "%Y-%m-%dT%H:%M:%S", &tm);

    /* Append .mmmZ  */
    sprintf(iso + 19, ".%03dZ", ms_part); /* 19 chars written earlier */
}

static int migrate(sqlite3 *db, const char *dir) {
    /* 0. Ensure schema_migrations table exists ---------------------- */
    const char *create_sm =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version     INTEGER PRIMARY KEY,"
        "  checksum    TEXT    NOT NULL,"
        "  applied_at  TEXT    DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
        ");";
    if (exec_sql(db, create_sm) != SQLITE_OK)
        return SQLITE_ERROR;

    /* 1. Current version ------------------------------------------- */
    sqlite3_stmt *st = NULL;
    int current = 0;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(version),0) FROM schema_migrations;", -1, &st,
                           NULL) != SQLITE_OK)
        return SQLITE_ERROR;

    if (sqlite3_step(st) == SQLITE_ROW)
        current = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);

    /* 2. Scan directory -------------------------------------------- */
    struct dirent **list;
    int n = scandir(dir, &list, NULL, alphasort);
    if (n < 0) {
        perror("scandir");
        return SQLITE_ERROR;
    }

    for (int i = 0; i < n; ++i) {
        if (list[i]->d_type != DT_REG) {
            free(list[i]);
            continue;
        }

        /* Extract leading integer "001" or "V002" ------------------ */
        int ver = 0;
        if (sscanf(list[i]->d_name, "V%d", &ver) != 1 && sscanf(list[i]->d_name, "%d", &ver) != 1) {
            free(list[i]);
            continue; /* not a migration */
        }
        if (ver <= current) {
            free(list[i]);
            continue;
        }

        /* Build full path, read file ------------------------------- */
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, list[i]->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) {
            perror(path);
            return SQLITE_ERROR;
        }

        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        rewind(f);

        char *sql = malloc(len + 1);
        if (!sql) {
            fprintf(stderr, "error: failed to allocate memory for migration\n");
            fclose(f);
            return SQLITE_ERROR;
        }

        if (fread(sql, 1, len, f) != len) {
            fprintf(stderr, "error: failed to read migration\n");
            fclose(f);
            free(sql);
            return SQLITE_ERROR;
        }

        sql[len] = '\0';
        fclose(f);

        uint64_t cksum = checksum_file(path);

        /* 3. One-file transaction --------------------------------- */
        int rc = exec_sql(db, "BEGIN;");
        if (rc == SQLITE_OK)
            rc = exec_sql(db, sql);
        if (rc == SQLITE_OK) {
            char ins[256];
            snprintf(ins, sizeof ins,
                     "INSERT INTO schema_migrations(version,checksum)"
                     " VALUES(%d,'%016llx');",
                     ver, (unsigned long long)cksum);
            rc = exec_sql(db, ins);
        }
        rc = exec_sql(db, rc == SQLITE_OK ? "COMMIT;" : "ROLLBACK;");
        free(sql);

        if (rc != SQLITE_OK)
            return rc; /* stop on first failure */
        current = ver; /* advance */
        free(list[i]);
    }
    free(list);
    return SQLITE_OK;
}

static uint64_t fnv1a64(const void *data, size_t len) {
    const uint8_t *p = data;
    uint64_t hash = 0xcbf29ce484222325ULL; /* offset basis */
    while (len--) {
        hash ^= *p++;
        hash *= 0x100000001b3ULL; /* FNV prime */
    }
    return hash;
}

static uint64_t checksum_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint8_t buf[4096];
    uint64_t h = 0xcbf29ce484222325ULL;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) h = fnv1a64(buf, n) ^ (h * 0x100000001b3ULL);
    fclose(f);
    return h;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s\n", err);
        sqlite3_free(err);
    }
    return rc;
}