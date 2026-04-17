---
# try also 'default' to start simple
theme: default
fonts:
  mono: Comic Mono
  provider: none
# random image from a curated Unsplash collection by Anthony
# like them? see https://unsplash.com/collections/94734566/slidev
# some information about your slides (markdown enabled)
title: RegionBranchOpInterface
info: |
  ## Slidev Starter Template
  Presentation slides for developers.

  Learn more at [Sli.dev](https://sli.dev)
# apply UnoCSS classes to the current slide
class: text-center
# https://sli.dev/features/drawing
drawings:
  persist: false
# slide transition: https://sli.dev/guide/animations.html#slide-transitions
transition: slide-left
# enable Comark Syntax: https://comark.dev/syntax/markdown
comark: true
# duration of the presentation
duration: 35min
---

# {{$frontmatter.title}}


<div @click="$slidev.nav.next" class="mt-12 py-1" hover:bg="white op-10">
  Press Space for next page <carbon:arrow-right />
</div>

<div class="abs-br m-6 text-xl">
  <button @click="$slidev.nav.openInEditor()" title="Open in Editor" class="slidev-icon-btn">
    <carbon:edit />
  </button>
  <a href="https://github.com/slidevjs/slidev" target="_blank" class="slidev-icon-btn">
    <carbon:logo-github />
  </a>
</div>

---

## Prerequisites

Stuff you should already be comfortable with before we jump into `RegionBranchOpInterface` and region control flow in MLIR.

<v-clicks>

- **LLVM (the codebase)** — how types and ADTs show up when you read MLIR/LLVM C++ headers
- **MLIR IR** — `Type`, `Value`, and `Operation`, since everything else hangs off these

</v-clicks>

<!--
Order: LLVM vocabulary and ADTs first (how the implementation is written),
then MLIR’s IR model (what those APIs are describing).
-->

---

## LLVM: `Type`, classes, and ADTs

**Two different meanings of “type”**

- **C++ `class` / templates** — how LLVM and MLIR *build* the compiler (`APInt`, `SmallVector<...>`, visitors, etc.).
- **`llvm::Type` (and friends)** — LLVM IR's idea of a type (integer, pointer, struct, ...) in classic LLVM IR.

When you read `include/llvm/` or `include/mlir/`, you are mostly in **C++ / LLVM ADT** land. When you read dialect ops and SSA, you are in **MLIR IR** land. In practice, both often appear in the same line of code.

---

## ADT: `APInt`

**Arbitrary-precision integers** with an explicit bit width - LLVM's go-to integer type for constants and anything that must match IR integer types exactly.

- Carries **width** and **signedness** the way IR expects (not "whatever `int` is on this platform").
- Used whenever exact bit-level behavior matters: widths, masks, alignment in bits, etc.
- Common helpers you will see: `getBitWidth()`, `isNegative()`, `isZero()`, and value extractors like `getZExtValue()` / `getSExtValue()`.
---

## ADT: `APInt`

- `zext` = **zero-extend** (pad with `0` bits), `sext` = **sign-extend** (copy the sign bit). Same raw bits, different interpretation.
- Two's complement example:
  - 8-bit `11111111` to 16-bit 
  - `zext` is `00000000 11111111` (255);
  - `sext` is `11111111 11111111` (-1).
- Declaring one is usually width + value, e.g. `APInt v(8, 255)` means "8-bit value with raw bits `11111111`".
- For signed meaning, you are still storing raw bits; signedness shows up when you *interpret* or *extract* (`isNegative()`, `getSExtValue()`, `getZExtValue()`).

---

## ADT: `APInt`

You do not need to memorize every API; just recognize **"this is not `int` / `long` - this is IR-accurate."**

Full header: `~/llvm/llvm-project/llvm/include/llvm/ADT/APInt.h`.

<<< @/snippets/llvm/ADT/APInt-class.h cpp {lines:true}{maxHeight:'120px'}

---

## ADT: `SmallVector` — what `<T, N>` allocates

`SmallVector<T, N>` means **`N` inline elements of `T`**, not `N` bytes.

- **`SmallVectorStorage<T, N>`** keeps a **raw byte buffer** for exactly **`N` objects**: `InlineElts[N * sizeof(T)]`, with **`alignas(T)`** so placement-new is safe.
- **`N == 0`**: special case - there is **no** `InlineElts` array (saves padding), but alignment still keeps **`getFirstEl()`** math valid.
- **`SmallVector<T, N>`** inherits that storage and passes **`N`** into **`SmallVectorImpl<T>(N)`** as starting inline capacity.
- Plain version: first `N` elements stay inside the object (fast, no malloc). Go past `N`, and it spills into a normal growable heap array (LLVM's own grow logic, not `std::vector` internally).

---

Full file: `~/llvm/llvm-project/llvm/include/llvm/ADT/SmallVector.h`.

<<< @/snippets/llvm/ADT/SmallVector-inline.h cpp {lines:true}{maxHeight:'160px'}


---
class: text-left
---

## ADT: `SmallVector` — default `N` when you write `SmallVector<T>`

If you leave out **`N`**, LLVM aims for about a **64-byte** `sizeof(SmallVector<T>)` using `kPreferredSmallVectorSizeof = 64`. Let:

- $K = 64$ (bytes)
- $H = \text{sizeof}(\texttt{SmallVector<T,0>})$ — header-only footprint (no inline `T` objects yet)
- $s = \text{sizeof}(T)$

This is basically LLVM's `PreferredInlineBytes / sizeof(T)`, with a **minimum of one** inline slot:

$$
N_{\text{default}}
= \max\!\left(1,\ \left\lfloor \frac{K - H}{s} \right\rfloor\right)
$$

So if you do not pick `N` yourself, LLVM chooses a default `N` from the type size and that ~64-byte target.

---

$$
N_{\text{default}}
= \max\!\left(1,\ \left\lfloor \frac{K - H}{s} \right\rfloor\right)
$$

- If $s$ is big, the inner fraction can become $0$ - that is why the code wraps it in $\max(..., 1)$.
- If $s$ is *DAMN* big, you can hit the **`static_assert`** in `CalculateSmallVectorDefaultInlinedElements`, and then you must choose **`SmallVector<T, N>`** explicitly.

**Example (LP64-ish, rounded for the slide):** take $K=64$, $H=24$, $s=8$ (for example, pointer-sized `T`).

$$
\left\lfloor \frac{64 - 24}{8} \right\rfloor = \left\lfloor \frac{40}{8} \right\rfloor = 5
\quad\Rightarrow\quad
N_{\text{default}} = 5
$$

With the same $K,H$ but $s=4$, you get $\lfloor 40/4 \rfloor = 10$ inline elements.

<<< @/snippets/llvm/ADT/SmallVector-default-N.h cpp {lines:true}{maxHeight:'140px'}

---

## ADT: equivalence classes

**Split things into disjoint groups** - "these belong together for analysis."

LLVM's **`EquivalenceClasses`** in `llvm/ADT/EquivalenceClasses.h` is a **Tarjan-style union-find**. Use it for **unification**, **value numbering**, and similar problems.

<<< @/snippets/llvm/ADT/EquivalenceClasses-overview.h cpp {lines:true}{maxHeight:'100px'}

For MLIR/SSA work, think **"merge nodes that must agree"**, not just "store stuff in a `std::set`."

---

## MLIR: `Type`

A **`Type`** is MLIR's compile-time type object: it describes what a **`Value`** can represent (shapes, element types, dialect-specific attributes, etc.).

- Types are **interned**, so identity comparison is the usual MLIR pattern.
- Dialects can extend the type system, but the same `Type` layer is shared across dialects.

---

## MLIR: `Value`

A **`Value`** is an SSA value: either an operation **result** or a **block argument** (including region entry arguments).

- **SSA**: each value has one definition, and use-def chains are explicit.
- Types travel with values: each `Value` has a queryable `Type`.

`RegionBranchOpInterface` eventually talks about **what values flow across region edges** - `Value` is the thing that flows.

---

## MLIR: `Operation` and `Operation*`

An **`Operation`** is the core IR unit: **operands**, **results**, **attributes**, **regions**, and **nested blocks** all live there.

- **`Operation*`** is the normal handle when walking or rewriting IR (passes, patterns, interfaces).
- Regions belong to ops, and region terminators connect to **successors** described by interfaces.

So: **`Type`** = what it is, **`Value`** = SSA data, **`Operation*`** = where structure and control flow live.

---

## Prerequisites — checklist

| Topic | You should be able to... |
| --- | --- |
| `APInt` | Explain why fixed-width integer semantics live here, not in plain C++ ints |
| `SmallVector<T,N>` | Explain inline `N` elements (`N*sizeof(T)` bytes), default `N` (~64B object), and when it moves to heap storage |
| Equivalence classes | Read "merge these for analysis" as partition / union-find usage |
| MLIR `Type` / `Value` / `Operation*` | Follow operands, results, and regions while reading or debugging IR |

From here, region branch interfaces build directly on **regions + terminators + values crossing edges** - this is the foundation.
