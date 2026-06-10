// Copyright (C) 2024-2026  ilobilo

export module nvme:spec;

import libarch;
import std;

export namespace nvme
{
    constexpr std::uint32_t max_queue_depth = 1024;
    constexpr std::size_t admin_timeout_ms = 500;
    constexpr std::size_t io_timeout_ms = 30'000;

    namespace regs
    {
        constexpr arch::bit_register<std::uint64_t> cap { 0x00 };
        constexpr arch::scalar_register<std::uint32_t> vs { 0x08 };
        constexpr arch::scalar_register<std::uint32_t> intms { 0x0C };
        constexpr arch::scalar_register<std::uint32_t> intmc { 0x10 };
        constexpr arch::bit_register<std::uint32_t> cc { 0x14 };
        constexpr arch::scalar_register<std::uint32_t> csts { 0x1C };
        constexpr arch::scalar_register<std::uint32_t> aqa { 0x24 };
        constexpr arch::scalar_register<std::uint64_t> asq { 0x28 };
        constexpr arch::scalar_register<std::uint64_t> acq { 0x30 };
    } // namespace regs

    namespace flags
    {
        namespace cap
        {
            constexpr arch::field<std::uint64_t, std::uint8_t> mpsmin { 48, 4 };
            constexpr arch::field<std::uint64_t, std::uint8_t> dstrd { 32, 4 };
            constexpr arch::field<std::uint64_t, std::uint8_t> to { 24, 8 };
            constexpr arch::field<std::uint64_t, std::uint16_t> mqes { 0, 16 };
        } // namespace cap

        namespace cc
        {
            constexpr arch::field<std::uint32_t, std::uint8_t> iocqes { 20, 4 };
            constexpr arch::field<std::uint32_t, std::uint8_t> iosqes { 16, 4 };
            constexpr arch::field<std::uint32_t, std::uint8_t> ams { 11, 3 };
            constexpr arch::field<std::uint32_t, std::uint8_t> mps { 7, 4 };
            constexpr arch::field<std::uint32_t, std::uint8_t> shn { 14, 2 };
            constexpr arch::field<std::uint32_t, std::uint8_t> css { 4, 3 };
            constexpr arch::field<std::uint32_t, bool> enable { 0, 1 };
        } // namespace cc

        namespace csts
        {
            constexpr std::uint32_t rdy = (1 << 0);
            constexpr std::uint32_t cfs = (1 << 1);
            constexpr std::uint32_t shst_mask = (0b11 << 2);
            constexpr std::uint32_t shst_complete = (0b10 << 2);
        } // namespace csts
    } // namespace flags

    namespace spec
    {
        enum command_opcode
        {
            flush = 0x00,
            write = 0x01,
            read = 0x02
        };

        enum class admin_opcode
        {
            delete_sq = 0x0,
            create_sq = 0x1,
            delete_cq = 0x4,
            create_cq = 0x5,
            identify = 0x6,
            set_features = 0x9
        };

        enum command_flags
        {
            queue_phys_contig = (1 << 0),
            cq_irq_enabled = (1 << 1)
        };

        enum identify_cns
        {
            identify_namespace = 0x00,
            identify_controller = 0x01,
            identify_active_list = 0x02
        };

        enum feature_id
        {
            volatile_write_cache = 0x06,
            number_of_queues = 0x07,
            interrupt_coalescing = 0x08
        };

        struct data_pointer_t
        {
            std::uint64_t prp1;
            std::uint64_t prp2;
        };
        static_assert(sizeof(data_pointer_t) == 16);

        namespace cmd
        {
            struct common_t
            {
                std::uint8_t opcode;
                std::uint8_t flags;
                std::uint16_t command_id;
                std::uint32_t namespace_id;
                std::uint32_t cdw2[2];
                std::uint64_t metadata;
                data_pointer_t data_ptr;
                std::uint32_t cdw10;
                std::uint32_t cdw11;
                std::uint32_t cdw12;
                std::uint32_t cdw13;
                std::uint32_t cdw14;
                std::uint32_t cdw15;
            };

            struct rw_t
            {
                std::uint8_t opcode;
                std::uint8_t flags;
                std::uint16_t command_id;
                std::uint32_t nsid;
                std::uint64_t __reserved2;
                std::uint64_t metadata;
                data_pointer_t data_ptr;
                std::uint64_t start_lba;
                std::uint16_t length;
                std::uint16_t control;
                std::uint32_t ds_mgmt;
                std::uint32_t ref_tag;
                std::uint16_t app_tag;
                std::uint16_t app_mask;
            };

            struct create_cq_t
            {
                std::uint8_t opcode;
                std::uint8_t flags;
                std::uint16_t command_id;
                std::uint32_t __reserved1[5];
                std::uint64_t prp1;
                std::uint64_t __prp2;
                std::uint16_t cqid;
                std::uint16_t qsize;
                std::uint16_t cqflags;
                std::uint16_t irq_vector;
                std::uint32_t __reserved2[4];
            };

            struct create_sq_t
            {
                std::uint8_t opcode;
                std::uint8_t flags;
                std::uint16_t command_id;
                std::uint32_t __reserved1[5];
                std::uint64_t prp1;
                std::uint64_t __prp2;
                std::uint16_t sqid;
                std::uint16_t qsize;
                std::uint16_t sqflags;
                std::uint16_t cqid;
                std::uint32_t __reserved2[4];
            };

            struct identify_t
            {
                std::uint8_t opcode;
                std::uint8_t flags;
                std::uint16_t command_id;
                std::uint32_t nsid;
                std::uint64_t __reserved2[2];
                data_pointer_t data_ptr;
                std::uint8_t cns;
                std::uint8_t __reserved3;
                std::uint16_t controller_id;
                std::uint32_t __reserved11[5];
            };

            struct set_features_t
            {
                std::uint8_t opcode;
                std::uint8_t flags;
                std::uint16_t command_id;
                std::uint32_t nsid;
                std::uint64_t __reserved2[2];
                data_pointer_t data_ptr;
                std::uint32_t data[6];
            };
        } // namespace cmd

        union command_t
        {
            cmd::common_t common;
            cmd::rw_t rw;
            cmd::create_cq_t create_cq;
            cmd::create_sq_t create_sq;
            cmd::identify_t identify;
            cmd::set_features_t set_features;
        };
        static_assert(sizeof(command_t) == 64);

        struct completion_status_t
        {
            std::uint16_t status;

            enum class code_type
            {
                generic = 0x00,
                command_specific = 0x01,
                media_and_data_integrity_error = 0x02,
                path_related = 0x03,
                vendor_specific = 0x07
            };

            std::uint8_t code() const { return (status & 0x01FE) >> 1; }

            code_type type() const
            {
                return static_cast<code_type>((status & 0x0E00) >> 9);
            }

            bool successful() const
            {
                return type() == code_type::generic && code() == 0;
            }

            auto operator<=>(const completion_status_t &) const = default;
        };
        static_assert(
            std::is_standard_layout<completion_status_t>() &&
            std::is_trivial<completion_status_t>()
        );
        static_assert(sizeof(completion_status_t) == 2);

        struct completion_entry_t
        {
            union result_t
            {
                std::uint16_t u16;
                std::uint32_t u32;
                std::uint64_t u64;
            } result;

            std::uint16_t sq_head;
            std::uint16_t sq_id;
            std::uint16_t command_id;
            completion_status_t status;
        };
        static_assert(sizeof(completion_entry_t) == 16);

        struct power_state_t
        {
            std::uint16_t max_power;
            std::uint8_t __reserved2;
            std::uint8_t flags;
            std::uint32_t entry_latency;
            std::uint32_t exit_latency;
            std::uint8_t read_throughput;
            std::uint8_t read_latency;
            std::uint8_t write_throughput;
            std::uint8_t write_latency;
            std::uint16_t idle_power;
            std::uint8_t idle_scale;
            std::uint8_t __reserved19;
            std::uint16_t active_power;
            std::uint8_t active_work_scale;
            std::uint8_t __reserved23[9];
        };
        static_assert(sizeof(power_state_t) == 32);

        struct identify_controller_t
        {
            std::uint16_t vid;
            std::uint16_t ssvid;
            char sn[20];
            char mn[40];
            char fr[8];
            std::uint8_t rab;
            std::uint8_t ieee[3];
            std::uint8_t cmic;
            std::uint8_t mdts;
            std::uint16_t cntlid;
            std::uint32_t ver;
            std::uint32_t rtd3r;
            std::uint32_t rtd3e;
            std::uint32_t oaes;
            std::uint32_t ctratt;
            std::uint8_t __reserved100[11];
            std::uint8_t cntrltype;
            std::uint8_t __reserved112[16];
            std::uint16_t crdt1;
            std::uint16_t crdt2;
            std::uint16_t crdt3;
            std::uint8_t __reserved134[122];
            std::uint16_t oacs;
            std::uint8_t acl;
            std::uint8_t aerl;
            std::uint8_t frmw;
            std::uint8_t lpa;
            std::uint8_t elpe;
            std::uint8_t npss;
            std::uint8_t avscc;
            std::uint8_t apsta;
            std::uint16_t wctemp;
            std::uint16_t cctemp;
            std::uint16_t mtfa;
            std::uint32_t hmpre;
            std::uint32_t hmmin;
            std::uint8_t tnvmcap[16];
            std::uint8_t unvmcap[16];
            std::uint32_t rpmbs;
            std::uint16_t edstt;
            std::uint8_t dsto;
            std::uint8_t fwug;
            std::uint16_t kas;
            std::uint16_t hctma;
            std::uint16_t mntmt;
            std::uint16_t mxtmt;
            std::uint32_t sanicap;
            std::uint32_t hmminds;
            std::uint16_t hmmaxd;
            std::uint8_t __reserved338[4];
            std::uint8_t anatt;
            std::uint8_t anacap;
            std::uint32_t anagrpmax;
            std::uint32_t nanagrpid;
            std::uint8_t __reserved352[160];
            std::uint8_t sqes;
            std::uint8_t cqes;
            std::uint16_t maxcmd;
            std::uint32_t nn;
            std::uint16_t oncs;
            std::uint16_t fuses;
            std::uint8_t fna;
            std::uint8_t vwc;
            std::uint16_t awun;
            std::uint16_t awupf;
            std::uint8_t nvscc;
            std::uint8_t nwpc;
            std::uint16_t acwu;
            std::uint8_t __reserved534[2];
            std::uint32_t sgls;
            std::uint32_t mnan;
            std::uint8_t __reserved544[224];
            char subnqn[256];
            std::uint8_t __reserved1024[768];
            std::uint32_t ioccsz;
            std::uint32_t iorcsz;
            std::uint16_t icdoff;
            std::uint8_t ctrattr;
            std::uint8_t msdbd;
            std::uint8_t __reserved1804[244];
            power_state_t psd[32];
            std::uint8_t vs[1024];
        };
        static_assert(sizeof(identify_controller_t) == 0x1000);

        struct lba_format_t
        {
            std::uint16_t ms;
            std::uint8_t ds;
            std::uint8_t rp;
        };
        static_assert(sizeof(lba_format_t) == 4);

        struct identify_namespace_t
        {
            std::uint64_t nsze;
            std::uint64_t ncap;
            std::uint64_t nuse;
            std::uint8_t nsfeat;
            std::uint8_t nlbaf;
            std::uint8_t flbas;
            std::uint8_t mc;
            std::uint8_t dpc;
            std::uint8_t dps;
            std::uint8_t nmic;
            std::uint8_t rescap;
            std::uint8_t fpi;
            std::uint8_t dlfeat;
            std::uint16_t nawun;
            std::uint16_t nawupf;
            std::uint16_t nacwu;
            std::uint16_t nabsn;
            std::uint16_t nabo;
            std::uint16_t nabspf;
            std::uint16_t noiob;
            std::uint8_t nvmcap[16];
            std::uint16_t npwg;
            std::uint16_t npwa;
            std::uint16_t npdg;
            std::uint16_t npda;
            std::uint16_t nows;
            std::uint8_t __reserved74[18];
            std::uint32_t anagrpid;
            std::uint8_t __reserved96[3];
            std::uint8_t nsattr;
            std::uint16_t nvmsetid;
            std::uint16_t endgid;
            std::uint8_t nguid[16];
            std::uint8_t eui64[8];
            lba_format_t lbaf[16];
            std::uint8_t __reserved192[192];
            std::uint8_t vs[3712];
        };
        static_assert(sizeof(identify_namespace_t) == 0x1000);
    } // namespace spec
} // export namespace nvme
