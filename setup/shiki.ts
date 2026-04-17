import { defineShikiSetup } from '@slidev/types'

export default defineShikiSetup(() => ({
  langs: {
    mlir: async () => {
      const moduleUrl = new URL(
        '../node_modules/@shikijs/vitepress-twoslash/node_modules/@shikijs/langs/dist/llvm.mjs',
        import.meta.url,
      )
      const loaded = await import(moduleUrl.href)
      const llvmLanguage = Array.isArray(loaded.default)
        ? loaded.default[0]
        : loaded.default

      // Reuse LLVM IR grammar as a pragmatic MLIR highlighter fallback.
      return {
        ...llvmLanguage,
        name: 'mlir',
      }
    },
  },
}))
