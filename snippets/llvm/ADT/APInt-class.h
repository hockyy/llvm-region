// llvm/ADT/APInt.h  (~~/llvm/llvm-project/llvm/include/...)

class [[nodiscard]] APInt {
public:
  APInt(unsigned numBits, uint64_t val, bool isSigned = false, bool implicitTrunc = false);
};
