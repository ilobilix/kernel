// Copyright (C) 2024-2026  ilobilo

module;

#include <lwip/err.h>

export module lwip;

import drivers.dev.net;
import lib;
import std;

export namespace lwip
{
    constexpr err_t to_err(lib::err err)
    {
        using enum lib::err;
        switch (err)
        {
            case out_of_memory: return ERR_MEM;
            case no_buffer_space: return ERR_BUF;
            case timed_out: return ERR_TIMEOUT;
            case host_unreachable: return ERR_RTE;
            case operation_in_progress: return ERR_INPROGRESS;
            // case invalid_argument: return ERR_VAL;
            case would_block: return ERR_WOULDBLOCK;
            case address_in_use: return ERR_USE;
            case already_in_progress: return ERR_ALREADY;
            case already_connected: return ERR_ISCONN;
            case not_connected: return ERR_CONN;
            case io_error: return ERR_IF;
            case connection_aborted: return ERR_ABRT;
            case connection_reset: return ERR_RST;
            // case not_connected: return ERR_CLSD;
            case invalid_argument: return ERR_ARG;
            default: return ERR_IF;
        }
    }

    constexpr lib::err from_err(err_t err)
    {
        using enum lib::err;
        switch (err)
        {
            case ERR_MEM: return out_of_memory;
            case ERR_BUF: return no_buffer_space;
            case ERR_TIMEOUT: return timed_out;
            case ERR_RTE: return host_unreachable;
            case ERR_INPROGRESS: return operation_in_progress;
            case ERR_VAL: return invalid_argument;
            case ERR_WOULDBLOCK: return would_block;
            case ERR_USE: return address_in_use;
            case ERR_ALREADY: return already_in_progress;
            case ERR_ISCONN: return already_connected;
            case ERR_CONN: return not_connected;
            case ERR_ABRT: return connection_aborted;
            case ERR_RST: return connection_reset;
            case ERR_CLSD: return not_connected;
            case ERR_ARG: return invalid_argument;
            default: return io_error;
        }
    }

    constexpr lib::expect<void> check_err(err_t err)
    {
        if (err == ERR_OK)
            return { };
        return std::unexpected { from_err(err) };
    }

    lib::expect<void> attach(const std::shared_ptr<dev::net::nic_t> &nic);
    void deattach(const std::shared_ptr<dev::net::nic_t> &nic);

    lib::initgraph::stage *initialised_stage();
} // export namespace lwip
