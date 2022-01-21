#define QEMU_ALIGN_DOWN(n, m) ((n) / (m) * (m))
#define QEMU_ALIGN_UP(n, m) QEMU_ALIGN_DOWN((n) + (m) - 1, (m))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#define BITS_PER_LONG (sizeof(long) * 8)
#define BIT(nr)                 (1UL << (nr))
#define BIT_ULL(nr)             (1ULL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(x) (DIV_ROUND_UP(x, BITS_PER_LONG))
#define ctpopl ctpop64
#define clzl clz64
#define ctzl ctz64

static inline int clz32(uint32_t val)
{
    return val ? __builtin_clz(val) : 32;
}

static inline int clz64(uint64_t val)
{
    return val ? __builtin_clzll(val) : 64;
}

static inline int ctz64(uint64_t val)
{
    return val ? __builtin_ctzll(val) : 64;
}

static inline int ctpop64(uint64_t val)
{
    return __builtin_popcountll(val);
}

static inline bool is_power_of_2(uint64_t value)
{
    if (!value) {
        return false;
    }

    return !(value & (value - 1));
}

static inline uint64_t pow2ceil(uint64_t value)
{
    int n = clz64(value - 1);

    if (!n) {
        /*
         * @value - 1 has no leading zeroes, thus @value - 1 >= 2^63
         * Therefore, either @value == 0 or @value > 2^63.
         * If it's 0, return 1, else return 0.
         */
        return !value;
    }
    return 0x8000000000000000ull >> (n - 1);
}

static inline void set_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);

    *p  |= mask;
}

unsigned long find_next_bit(const unsigned long *addr,
                            unsigned long size,
                            unsigned long offset);

unsigned long find_next_zero_bit(const unsigned long *addr,
                                 unsigned long size,
                                 unsigned long offset);