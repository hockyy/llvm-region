// Default inline element count (~64B object)  — llvm/ADT/SmallVector.h

static constexpr size_t kPreferredSmallVectorSizeof = 64;
static constexpr size_t PreferredInlineBytes =
    kPreferredSmallVectorSizeof - sizeof(SmallVector<T, 0>);
static constexpr size_t NumFit = PreferredInlineBytes / sizeof(T);
static constexpr size_t value = NumFit == 0 ? 1 : NumFit;
