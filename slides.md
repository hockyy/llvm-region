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

What you should already be comfortable with before `RegionBranchOpInterface` and region-based control flow in MLIR.

<v-clicks>

- **LLVM (the codebase)** — how types and ADTs are used when reading MLIR/LLVM C++ headers
- **MLIR IR** — `Type`, `Value`, and `Operation` as the objects everything else hangs off

</v-clicks>

<!--
Order: LLVM vocabulary and ADTs first (how the implementation is written),
then MLIR’s IR model (what those APIs are describing).
-->

---

## LLVM: `Type`, classes, and ADTs

**Two different “types” in the room**

- **C++ `class` / templates** — how LLVM and MLIR *implement* the compiler (`APInt`, `SmallVector<…>`, visitors, etc.).
- **`llvm::Type` (and friends)** — the LLVM IR notion of a type (integer, pointer, struct, …) used in the classic LLVM representation.

When you read `include/llvm/` or `include/mlir/`, you are mostly in the **C++** and **LLVM ADT** world; when you read dialect ops and SSA, you are in the **MLIR IR** world. Both show up on the same line of code.

---

## ADT: `APInt`

**Arbitrary-precision integers** sized to a bit width — LLVM’s workhorse for constants and anything that must match IR integer types exactly.

- Holds **width** and **signedness** semantics as the IR expects (not “whatever `int` happens to be”).
- Used wherever bit-exact integer reasoning matters: widths, masks, alignment in bits, etc.

You do not need to memorize every API; you need to recognize **“this is not `int` / `long` — it’s IR-accurate.”**

Full header: `~/llvm/llvm-project/llvm/include/llvm/ADT/APInt.h`.

<<< @/snippets/llvm/ADT/APInt-class.h cpp {lines:true}{maxHeight:'120px'}

---

## ADT: `SmallVector` — what `<T, N>` allocates

`SmallVector<T, N>` is **`N` inline elements of `T`**, not `N` bytes.

- **`SmallVectorStorage<T, N>`** holds a **byte buffer** sized for exactly **`N` objects**: `InlineElts[N * sizeof(T)]`, **`alignas(T)`** so placement-new into that storage is legal.
- **`N == 0`**: explicit specialization — **no** `InlineElts` array (avoids useless padding); alignment is still correct so **`getFirstEl()`** pointer math stays valid.
- **`SmallVector<T, N>`** inherits that storage and passes **`N`** into **`SmallVectorImpl<T>(N)`** as the **initial inline capacity** (the “small” buffer the base may point at before `grow()`).
- Plain version: it tries to keep the first `N` elements inside the object itself (fast, no malloc). If you push past `N`, it spills into a normal growable heap array.

---

Full file: `~/llvm/llvm-project/llvm/include/llvm/ADT/SmallVector.h`.

<<< @/snippets/llvm/ADT/SmallVector-inline.h cpp {lines:true}{maxHeight:'160px'}


---
class: text-left
---

## ADT: `SmallVector` — default `N` when you write `SmallVector<T>`

If you omit **`N`**, LLVM targets a **~64-byte** `sizeof(SmallVector<T>)` via `kPreferredSmallVectorSizeof = 64`. Let:

- $K = 64$ (bytes)
- $H = \text{sizeof}(\texttt{SmallVector<T,0>})$ — header-only footprint (no inline `T` objects yet)
- $s = \text{sizeof}(T)$

Integer division matches LLVM’s `PreferredInlineBytes / sizeof(T)` with a **minimum of one** inline slot:

$$
N_{\text{default}}
= \max\!\left(1,\ \left\lfloor \frac{K - H}{s} \right\rfloor\right)
$$

So basically, without any specific declaration of inlined elements 

---

Huge $s$ can make the inner fraction $0$ — that’s why the code uses the $\max$ with $1$. Very large $s$ also trips the **`static_assert`** in `CalculateSmallVectorDefaultInlinedElements`; then you must choose **`SmallVector<T, N>`** explicitly.

**Example (LP64-style, numbers rounded for the slide):** take $K=64$, $H=24$, $s=8$ (e.g. pointer-sized `T`).

$$
\left\lfloor \frac{64 - 24}{8} \right\rfloor = \left\lfloor \frac{40}{8} \right\rfloor = 5
\quad\Rightarrow\quad
N_{\text{default}} = 5
$$

Same $K,H$ with $s=4$ gives $\lfloor 40/4 \rfloor = 10$ inline elements.

<<< @/snippets/llvm/ADT/SmallVector-default-N.h cpp {lines:true}{maxHeight:'140px'}

---

## ADT: equivalence classes

**Partition a set into disjoint groups** — “these things are the same for analysis purposes.”

LLVM’s **`EquivalenceClasses`** in `llvm/ADT/EquivalenceClasses.h` implements **Tarjan-style union–find**. Use it for **unification**, **value numbering**, etc.

<<< @/snippets/llvm/ADT/EquivalenceClasses-overview.h cpp {lines:true}{maxHeight:'100px'}

For MLIR/SSA work: think **“merge nodes that must agree”**, not just a `std::set`.

---

## MLIR: `Type`

A **`Type`** is a compile-time entity in MLIR’s type system: it describes what a **`Value`** may represent (shapes, element types, dialect-specific attributes, etc.).

- Types are **interned** and compared by identity in the usual MLIR style.
- Dialects extend the type system; the same IR layer (`Type`) spans all dialects.

---

## MLIR: `Value`

A **`Value`** is an SSA value: either a **result** of an operation or a **block argument** (including region entry arguments).

- **SSA**: each value has a single definition; use-def chains are explicit.
- Types flow with values: each `Value` has a `Type` you can query.

`RegionBranchOpInterface` eventually talks about **what values flow along region edges** — `Value` is that currency.

---

## MLIR: `Operation` and `Operation*`

An **`Operation`** is the unit of IR: **operands**, **results**, **attributes**, **regions**, and **nested blocks**.

- **`Operation*`** is the usual handle when walking or rewriting IR (passes, patterns, interfaces).
- Regions belong to ops; terminators inside regions connect to **successors** described by interfaces.

So: **`Type`** (what), **`Value`** (SSA data), **`Operation*`** (where structure and control flow live).

---

## Prerequisites — checklist

| Topic | You should be able to… |
| --- | --- |
| `APInt` | Say why fixed-width integer semantics live here, not in raw C++ ints |
| `SmallVector<T,N>` | Explain inline `N` elements (`N*sizeof(T)` bytes), default `N` (~64B object), and when the heap takes over |
| Equivalence classes | Read “merge these for analysis” as partition / union–find usage |
| MLIR `Type` / `Value` / `Operation*` | Follow operands, results, and regions when reading or debugging IR |

From here, region branch interfaces build on **regions + terminators + values crossing edges** — i.e. on this foundation.
