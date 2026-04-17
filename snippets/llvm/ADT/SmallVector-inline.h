// llvm/ADT/SmallVector.h — inline buffer + wiring  (~/llvm/llvm-project/...)

template <typename T, unsigned N>
struct SmallVectorStorage { alignas(T) char InlineElts[N * sizeof(T)]; };
template <typename T> struct alignas(T) SmallVectorStorage<T, 0> {};

template <typename T, unsigned N = CalculateSmallVectorDefaultInlinedElements<T>::value>
class SmallVector : public SmallVectorImpl<T>, SmallVectorStorage<T, N> {
public:
  SmallVector() : SmallVectorImpl<T>(N) {}
};
