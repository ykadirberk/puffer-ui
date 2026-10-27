// pufferui/impl/core_pool.inl - context-owned retained allocation storage.
// Included inside namespace pui before the state declarations.
// Size classes retain their free blocks until the owning context is destroyed.
// Large/over-aligned requests keep the ordinary upstream allocation contract.

class retained_pool_resource final : public std::pmr::memory_resource
{
    struct free_block
    {
        free_block *next;
    };
    struct page
    {
        page *next;
        usize bytes;
    };
    static constexpr usize CLASSES = 13, FIRST_SIZE = 16;
    static constexpr usize ALIGNMENT = alignof(std::max_align_t);
    static constexpr usize HEADER = (sizeof(page) + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
    std::pmr::memory_resource *upstream;
    usize largest;
    page *pages = nullptr;
    free_block *available[CLASSES]{};
    usize next_blocks[CLASSES]{};
    usize size_class(usize bytes, usize &block) const
    {
        block = FIRST_SIZE;
        usize index = 0;
        while (block < bytes)
        {
            block *= 2;
            ++index;
        }
        return index;
    }
    void *do_allocate(usize bytes, usize alignment) override
    {
        if (bytes > largest || alignment > ALIGNMENT) return upstream->allocate(bytes, alignment);
        usize block;
        const usize index = size_class(bytes, block);
        if (!available[index])
        {
            const usize limit = 65536 / block;
            const usize count =
                next_blocks[index] ? next_blocks[index] : std::min(usize(32), limit);
            const usize allocation_bytes = HEADER + block * count;
            auto *memory = static_cast<u8 *>(upstream->allocate(allocation_bytes, ALIGNMENT));
            auto *owned = ::new (memory) page{pages, allocation_bytes};
            pages = owned;
            for (usize i = 0; i < count; ++i)
                available[index] = ::new (memory + HEADER + i * block) free_block{available[index]};
            next_blocks[index] = std::min(count * 2, std::min(usize(512), limit));
        }
        auto *result = available[index];
        available[index] = result->next;
        return result;
    }
    void do_deallocate(void *pointer, usize bytes, usize alignment) override
    {
        if (bytes > largest || alignment > ALIGNMENT)
        {
            upstream->deallocate(pointer, bytes, alignment);
            return;
        }
        usize block;
        const usize index = size_class(bytes, block);
        available[index] = ::new (pointer) free_block{available[index]};
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
    {
        return this == &other;
    }

public:
    explicit retained_pool_resource(usize largest_block = 256, std::pmr::memory_resource *source =
                                                                   std::pmr::get_default_resource())
        : upstream(source), largest(std::min(largest_block, usize(65536)))
    {
    }
    retained_pool_resource(const retained_pool_resource &) = delete;
    retained_pool_resource &operator=(const retained_pool_resource &) = delete;
    ~retained_pool_resource() override
    {
        while (pages)
        {
            auto *next = pages->next;
            upstream->deallocate(pages, pages->bytes, ALIGNMENT);
            pages = next;
        }
    }
};
