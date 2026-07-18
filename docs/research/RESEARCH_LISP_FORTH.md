# Research: Lisp and Forth as Extensible Languages

Research summary as of July 2026. Sources are primary papers, standards,
official documentation, and practitioner accounts. No unauthorized repositories
were used.

## Conclusion

Lisp and Forth did not simply “fail.” Both had durable success where their
extension models matched the problem:

- Lisp became foundational in symbolic computing and AI, and remains central to
  Emacs, Racket, Clojure, and several language-research communities.
- Forth remains useful in firmware, instrumentation, boot environments, and
  machines where a tiny interactive implementation matters more than large-team
  static tooling.

They did not become the default for mainstream general-purpose development.
Their strongest feature—letting programmers reshape the language from within—
also made programs more dependent on local conventions, expansion behavior,
mutable environments, and specialist knowledge.

The lesson is not to reject extensibility. It is to preserve the parts users
love—small kernels, live feedback, domain vocabulary, structural abstraction—
while bounding syntax, phases, effects, and generated interfaces.

## Lisp

### Why Lisp is extensible

Lisp represents program structure using the same lists and symbols manipulated
by ordinary programs. A macro receives unevaluated syntax and produces syntax.
Because surface forms are structurally uniform, a small reader and macro
expander can express control forms, binding constructs, embedded languages, and
declarative APIs.

Paul Graham calls the resulting style “bottom-up programming”: instead of only
writing an application in a fixed language, programmers grow a vocabulary in
which the application becomes direct. The idea is genuinely powerful. It
compresses repetitive domain structure and can make the final application read
like its problem statement.

Source: Paul Graham,
[Programming Bottom-Up](https://paulgraham.com/progbot.html) (1993).

### What people liked

#### Structural macros

Lisp macros transform parsed structure, not strings. Parenthesized uniformity
makes the boundary of each form explicit and avoids much of the grammar
ambiguity encountered by languages with freely extensible concrete syntax.

Scheme's hygienic macro research strengthened this model by preserving lexical
binding automatically. Racket later demonstrated how hygienic syntax values,
modules, and expansion phases can support substantial language layers.

Sources:

- William Clinger and Jonathan Rees,
  [Macros That Work](https://scholarsbank.uoregon.edu/bitstreams/12fed7c3-fd49-4ae8-97e4-6bbb156aaba6/download)
- Matthew Flatt,
  [Binding as Sets of Scopes](https://www-old.cs.utah.edu/plt/publications/popl16-f.pdf)
- Matthias Felleisen et al.,
  [Languages as Libraries](https://www-old.cs.utah.edu/plt/publications/pldi11-tscff.pdf)

#### Interactive, image-based development

Classic Lisp systems emphasized a running environment in which code could be
compiled, inspected, redefined, debugged, and resumed. Interlisp integrated
editing, inspection, history, debugging, and program analysis unusually early.
Practitioners valued the short feedback loop and the ability to examine the
actual live system rather than reconstruct it from logs.

NASA's use of Lisp for remote-agent software is a useful counterexample to the
idea that Lisp was only academic. The attraction included interactive debugging,
rapid modification, and expressive symbolic programs. The account also notes
organizational and ecosystem friction, illustrating that technical productivity
does not guarantee institutional adoption.

Sources:

- Warren Teitelman and Larry Masinter,
  [The Interlisp Programming Environment](https://www.bitsavers.org/pdf/xerox/interlisp/Teitelman_Masinter_The_Interlisp_Programming_Environment_1978.pdf)
- Ron Garret,
  [Lisping at JPL](https://flownet.com/gat/jpl-lisp.html)

#### Domain vocabularies and abstraction

Macros let library authors hide protocol, resource, query, control, and data
declaration boilerplate behind names meaningful to the domain. Unlike a function,
a macro can control evaluation, introduce bindings, or generate declarations.
This is the feature worth preserving for an AI-oriented language: a model can
reuse compact domain forms while the compiler reduces them to a small canonical
language.

#### Macro inspection

Mature Lisps offer macro expansion tools. Being able to inspect one expansion or
the full expansion is essential: extensibility is tolerable only if users and
tools can recover the ordinary program beneath it.

### What did not work as well

#### Unhygienic binding in Common Lisp

Common Lisp macros are structurally safer than text substitution but do not
automatically preserve lexical scope. Authors use generated symbols and careful
conventions to prevent capture, and sometimes deliberately capture names. This
places a subtle proof obligation on every macro author and complicates
composition.

Common Lisp's package system controls symbol identity and external names, but it
is not a replacement for lexical macro hygiene.

Sources:

- [Common Lisp HyperSpec: `DEFMACRO`](https://www.lispworks.com/documentation/HyperSpec/Body/m_defmac.htm)
- [Common Lisp HyperSpec: packages](https://www.lispworks.com/documentation/HyperSpec/Body/11_.htm)

#### Reader macros create ambient grammar

The Common Lisp reader is programmable. Reader macros can make notation concise,
but reading a file may depend on mutable readtable state established elsewhere.
Reader-time evaluation (`#.`) can execute code while input is being read when
enabled. This harms security, reproducibility, editor independence, and local
understanding.

Clojure retains a Lisp reader but intentionally does not expose user-defined
reader macros; tagged literals provide a narrower extension point. That is a
strong practical vote for fixed tokenization with explicit data hooks.

Sources:

- [Common Lisp HyperSpec: reader algorithm](https://www.lispworks.com/documentation/HyperSpec/Body/02_b.htm)
- [Common Lisp HyperSpec: read-time evaluation](https://www.lispworks.com/documentation/HyperSpec/Body/02_dhf.htm)
- [Clojure reader reference](https://clojure.org/reference/reader)

#### Expansion can hide cost and inflate code

A compact macro call can expand into duplicated or deeply nested code. Separate
macros may repeatedly traverse or wrap the same form. The authored program's
size therefore stops predicting compile time, binary size, or run-time work.
An experience report on a Common Lisp system exceeding one million lines found
exponential expansion caused by macros that duplicated body forms. Refactoring
those macros reduced a reported full rebuild from about 33 minutes to about 5.5
minutes. This is one industrial case, not a general benchmark, but it establishes
that the failure mode occurs at consequential scale.

Source: Michael Wessel,
[Notes on Refactoring Exponential Macros in Common Lisp](https://www.sri.com/wp-content/uploads/2023/05/Notes-on-Refactoring-Exponential-Macros-in-Common-Lisp.pdf)
(2023).

#### Phase and diagnostic complexity

Once macros import helpers at expansion time, generate definitions, inspect
bindings, and deliberately change scopes, the language gains a second execution
world. Users must distinguish read time, expansion time, compile time, load time,
and run time. Errors can point into generated forms rather than authored code.
Racket handles these issues more rigorously than earlier Lisps, but the rules are
large.

#### Personalized languages and ecosystem fragmentation

Bottom-up design can produce a beautiful local language and a difficult external
codebase. Every project may invent control forms, object systems, naming
conventions, and build assumptions. Live images can also accumulate state that
is not fully represented by source files.

Richard Gabriel's historical and social analysis emphasizes that language
quality alone does not determine adoption; implementation availability,
standards, libraries, delivery constraints, and community convergence matter.
The frequently cited “Lisp Curse” essay makes a sharper version of the
fragmentation argument, but it is a practitioner opinion rather than empirical
research and should be weighed accordingly.

Sources:

- Richard Gabriel,
  [The Rise of Worse is Better](https://www.dreamsongs.com/RiseOfWorseIsBetter.html)
- Rudolf Winestock,
  [The Lisp Curse](http://www.winestockwebdesign.com/Essays/Lisp_Curse.html)

## Forth

### Why Forth is extensible

Forth programs consist of words resolved through a dictionary. New words extend
the vocabulary. Some words execute during compilation and can control how later
input is compiled. Defining words such as `CREATE ... DOES>` let a program define
new classes of words with custom construction and run-time behavior.

This is unusually deep extensibility: the compiler is not a distant tool but a
small set of ordinary language mechanisms available to the programmer.

Sources:

- Leo Brodie,
  [Starting Forth](https://www.forth.com/starting-forth/)
- [Gforth manual: defining words](https://gforth.org/manual/Defining-Words.html)
- [Forth 2012 standard](https://forth-standard.org/standard/words)

### What people liked

#### A tiny trusted implementation

Traditional Forth systems can be built from a small interpreter/compiler,
dictionary, stacks, and a compact set of primitives. Much of the environment is
then defined in Forth itself. This makes the system portable to unusual hardware
and comprehensible to one specialist.

#### Immediate feedback on the target

Forth is interactive even on constrained hardware. Engineers can inspect memory,
exercise devices, define a word, test it immediately, and keep the useful word
as part of the application. This makes it particularly effective for board bring
up, instrumentation, and firmware.

#### Domain-specific vocabularies

Short words can encode the concepts of a device or control process. Defining
words make repeated layouts or protocols concise. Like Lisp, Forth supports
bottom-up growth, but its primitives expose compilation and machine state more
directly.

#### Boot firmware and portability niches

Open Firmware/OpenBoot used a standardized Forth-derived environment to describe
devices and provide interactive boot facilities across hardware. This is a real
deployment success, not merely a pedagogical example.

Sources:

- [Open Firmware home and specifications](https://www.openfirmware.info/Welcome_to_Open_Firmware)
- Elizabeth Rather, Donald Colburn, and Charles Moore,
  [The Evolution of Forth](https://www.forth.com/resources/forth-programming-language/)

### What did not work as well

#### Stack effects are a human proof burden

In conventional Forth, values are mostly untyped cells and arguments are passed
on an implicit data stack. Good code documents stack effects, but reading a
sequence still requires mentally simulating stack shape, value meaning, return
stack use, and control flow. Rearrangement words make local changes concise while
making longer code harder to audit.

Static stack checking is possible for disciplined subsets, yet control flow,
polymorphic stack effects, return-stack manipulation, and dynamic compilation
make inference substantially harder. Ertl's work shows both the value and the
limits of automated checking for Forth-like code.

Source: M. Anton Ertl,
[Stack Effect Checking](https://www.complang.tuwien.ac.at/papers/ertl92.ps.gz)
(1992).

#### Compiler-extending words defeat ordinary tools

An immediate word can change compilation behavior based on arbitrary state.
Until that word executes, a generic parser or editor may not know the meaning of
following text. Refactoring, completion, static analysis, and reliable source
indexing therefore require running the program's compiler behavior.

This is similar to Lisp reader macros but more pervasive: the language can blur
interpretation, compilation, target execution, and metacompilation.

#### Portability versus implementation freedom

Forth culture values implementation access and hardware closeness. Programs can
depend on cell size, address layout, dictionary details, execution tokens,
compilation semantics, or vendor word sets. Standards improve portable source,
but fully exposing representation would prevent important implementation
techniques. The result is a persistent boundary between portable standard Forth
and system-specific Forth.

Sources:

- [Forth 2012 rationale](https://forth-standard.org/standard/rationale)
- [Gforth manual: ANS conformance](https://gforth.org/manual/ANS-conformance.html)

#### Ambient authority and safety

The same direct memory access and compiler control that make Forth excellent for
firmware also make isolation difficult. A malformed extension can corrupt the
dictionary, stacks, or target memory. This is inappropriate for untrusted package
macros or an AI that must safely explore unfamiliar dependencies.

#### Ecosystem and team scaling

Tiny implementations encouraged many dialects and local conventions. Libraries,
module systems, error reporting, source-level debugging, and package workflows
vary widely. Experts prize control and simplicity; newcomers face implicit state
and sparse semantic signposts. The economics favor mainstream languages once
hardware and memory can support their larger toolchains.

## Comparison

| Dimension | Lisp family | Forth family | Lesson |
|---|---|---|---|
| Extension input | Parsed list/syntax tree | Token stream plus dictionary state | Prefer structured syntax |
| Binding safety | Varies; strong in hygienic Scheme/Racket | Usually name/dictionary conventions | Make hygiene mandatory |
| Compile-time effects | Often general host-language effects | Often direct compiler/machine effects | Require pure bounded expansion |
| Grammar | Uniform lists, except programmable readers | Word-oriented, but immediate words alter compilation | Fix reader and tokenization |
| Type safety | Varies; commonly dynamic | Usually untyped cells | Type-check all expanded output |
| Tool recovery | Macro expansion can expose ordinary code | May require executing compiler words | Standardize canonical expansion |
| Main strength | Symbolic abstraction and DSLs | Tiny interactive target control | Preserve domain vocabulary and feedback |
| Scaling weakness | phase complexity and project-specific dialects | implicit stacks, dialects, ambient state | Standard profile and explicit dependencies |

## Why They Did Not Become the Default

No single technical cause explains adoption, and “failure” overstates the case.
The following factors reinforced one another:

1. Extensibility made separate projects feel like related but different
   languages.
2. Static tooling and ahead-of-time compilation were harder when compilation
   behavior was user-programmable.
3. Local concision sometimes transferred work from writers to readers.
4. Dynamic images or target state weakened source-only reproducibility.
5. Mainstream ecosystems accumulated larger standardized libraries, hiring
   pools, and vendor support.
6. Lisp's generality competed with simpler fixed languages; Forth's hardware
   intimacy became less necessary on larger machines.
7. Their syntax and workflows optimized for practitioners who accepted a steep
   conceptual shift, not for broad familiarity.

At the same time, their enduring niches show what genuinely worked: interactive
feedback, structural program generation, small semantic centers, and the power
to name domain ideas directly.

## Design Rules to Carry Forward

### Take from Lisp

- macros transform structured syntax;
- hygiene is automatic, with deliberate capture explicit and rare;
- generated nodes retain source provenance;
- expansion is inspectable and canonical;
- libraries can provide compact domain forms;
- a small Core, rather than surface uniformity alone, defines semantics.

### Take from Forth

- keep the trusted compiler/runtime boundary small;
- make compile-time evaluation immediate and understandable;
- permit users to build vocabulary from a few composable primitives;
- support direct inspection of lowered forms;
- keep the standard implementation usable without a huge toolchain.

### Do not take

- mutable readers or tokenizers;
- arbitrary compiler-mutating words;
- unhygienic identifier construction;
- ambient compile-time I/O or machine access;
- untyped implicit data stacks as the main program model;
- reliance on mutable images as the source of truth;
- transitive or invisible syntax imports;
- unspecified implementation internals as extension APIs.

### Make the language socially convergent

Technical controls alone are insufficient. A mandatory standard profile should
contain the ordinary control, data, error, iteration, and resource idioms so
projects do not reinvent them. Syntax imports should be explicit and
non-transitive. Packages should publish expanded interfaces and expansion-cost
metadata. Formatters and indexers should understand the fixed grammar without
executing dependency code.

This retains extensibility as a way to compress domain knowledge without making
every codebase a private language.
