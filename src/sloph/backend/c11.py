"""A deliberately small, first-order C11 backend for Core v0.

The accepted profile contains data globals and top-level functions represented by
direct lambda chains.  Every call is a saturated call to one of those functions;
function values never enter the generated program.
"""

from __future__ import annotations

from dataclasses import dataclass

from sloph.core.canonical import decimal_string
from sloph.core.diagnostics import Span, fail
from sloph.core.model import (
    AppExpr,
    Binder,
    CaseExpr,
    ConExpr,
    CoreType,
    CoreUnit,
    Definition,
    Expr,
    FunctionType,
    GlobalExpr,
    IntExpr,
    LamExpr,
    LetExpr,
    LocalExpr,
    PrimExpr,
)
from sloph.core.validate import validate


MAX_INTEGER_BITS = 16_384


@dataclass(frozen=True, slots=True)
class _Function:
    definition: Definition
    parameters: tuple[Binder, ...]
    body: Expr


def validate_profile(unit: CoreUnit, symbol: str) -> None:
    """Validate *unit* and the restrictions of the C11 first-order profile.

    Failures use the normal ``DiagnosticError`` mechanism with phase ``backend``.
    The selected symbol must be printable data rather than a function.
    """

    validate(unit)
    definitions = {item.name: item for item in unit.definitions}
    selected = definitions.get(symbol)
    if selected is None:
        fail(
            "backend.c11.unknown_symbol",
            "backend",
            f"unknown backend entry symbol {symbol!r}",
            unit.span,
            symbol=symbol,
        )
    functions = _functions(unit)
    if symbol in functions:
        fail(
            "backend.c11.function_entry",
            "backend",
            "the C11 backend entry symbol must be a data definition",
            selected.span,
            symbol=symbol,
        )
    for definition in sorted(unit.definitions, key=lambda item: item.name):
        if definition.name in functions:
            function = functions[definition.name]
            for binder in function.parameters:
                if isinstance(binder.type, FunctionType):
                    _unsupported_type(binder.type, binder.span, "function parameter")
            if isinstance(_function_result(definition.type), FunctionType):
                _unsupported_type(definition.type, definition.span, "function result")
            _check_expression(function.body, definitions, functions)
        else:
            _check_data_type(definition.type, definition.span, "data global")
            _check_expression(definition.value, definitions, functions)


def emit_c(unit: CoreUnit, symbol: str) -> str:
    """Return deterministic C11 source that prints *symbol* canonically."""

    validate_profile(unit, symbol)
    return _Emitter(unit, symbol).emit()


def _functions(unit: CoreUnit) -> dict[str, _Function]:
    result: dict[str, _Function] = {}
    for definition in sorted(unit.definitions, key=lambda item: item.name):
        arity = _function_arity(definition.type)
        if not arity:
            continue
        parameters: list[Binder] = []
        body = definition.value
        while isinstance(body, LamExpr):
            parameters.append(body.binder)
            body = body.body
        if len(parameters) != arity:
            fail(
                "backend.c11.function_shape",
                "backend",
                "a backend function must be a direct top-level lambda chain",
                definition.value.span,
                definition=definition.name,
                expected_arity=arity,
                lambda_count=len(parameters),
            )
        result[definition.name] = _Function(definition, tuple(parameters), body)
    return result


def _function_arity(type_: CoreType) -> int:
    count = 0
    while isinstance(type_, FunctionType):
        count += 1
        type_ = type_.result
    return count


def _function_result(type_: CoreType) -> CoreType:
    while isinstance(type_, FunctionType):
        type_ = type_.result
    return type_


def _check_data_type(type_: CoreType, span: Span, role: str) -> None:
    if isinstance(type_, FunctionType):
        _unsupported_type(type_, span, role)


def _unsupported_type(type_: CoreType, span: Span, role: str) -> None:
    fail(
        "backend.c11.higher_order_type",
        "backend",
        f"the C11 first-order profile rejects function-typed {role}",
        span,
        role=role,
    )


def _flatten_application(expression: AppExpr) -> tuple[Expr, tuple[Expr, ...]]:
    arguments: list[Expr] = []
    current: Expr = expression
    while isinstance(current, AppExpr):
        arguments.append(current.argument)
        current = current.function
    arguments.reverse()
    return current, tuple(arguments)


def _check_expression(
    expression: Expr,
    definitions: dict[str, Definition],
    functions: dict[str, _Function],
) -> None:
    if isinstance(expression, IntExpr):
        if abs(expression.value).bit_length() > MAX_INTEGER_BITS:
            fail(
                "backend.c11.integer_bits",
                "backend",
                f"integer literal exceeds the {MAX_INTEGER_BITS}-bit C11 limit",
                expression.span,
                configured=MAX_INTEGER_BITS,
            )
        return
    if isinstance(expression, LocalExpr):
        return
    if isinstance(expression, GlobalExpr):
        if expression.name in functions:
            fail(
                "backend.c11.function_escape",
                "backend",
                "top-level functions may only appear as direct saturated call targets",
                expression.span,
                function=expression.name,
            )
        return
    if isinstance(expression, LamExpr):
        fail(
            "backend.c11.nested_lambda",
            "backend",
            "the C11 first-order profile rejects nested function values",
            expression.span,
        )
    if isinstance(expression, AppExpr):
        target, arguments = _flatten_application(expression)
        if not isinstance(target, GlobalExpr) or target.name not in functions:
            fail(
                "backend.c11.dynamic_call",
                "backend",
                "calls must directly target a top-level function",
                expression.function.span,
            )
        expected = len(functions[target.name].parameters)
        if len(arguments) != expected:
            kind = "partial_call" if len(arguments) < expected else "oversaturated_call"
            fail(
                f"backend.c11.{kind}",
                "backend",
                f"call to {target.name!r} must supply exactly {expected} arguments",
                expression.span,
                function=target.name,
                expected=expected,
                actual=len(arguments),
            )
        for argument in arguments:
            _check_expression(argument, definitions, functions)
        return
    if isinstance(expression, LetExpr):
        _check_data_type(expression.binder.type, expression.binder.span, "let binding")
        _check_expression(expression.value, definitions, functions)
        _check_expression(expression.body, definitions, functions)
        return
    if isinstance(expression, PrimExpr):
        for argument in expression.arguments:
            _check_expression(argument, definitions, functions)
        return
    if isinstance(expression, ConExpr):
        for field in expression.fields:
            _check_expression(field, definitions, functions)
        return
    if isinstance(expression, CaseExpr):
        _check_data_type(expression.result_type, expression.span, "case result")
        _check_expression(expression.scrutinee, definitions, functions)
        for alternative in expression.alternatives:
            for binder in alternative.binders:
                _check_data_type(binder.type, binder.span, "case binder")
            _check_expression(alternative.body, definitions, functions)
        return
    raise AssertionError(f"unsupported expression {type(expression)!r}")


_RUNTIME = r'''#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SL_MAX_LIMBS 512u
#define SL_DECIMAL_CHUNKS 549u
#define SL_MAX_VALUES 100000u
#define SL_MAX_OUTPUT 1048576u
#define SL_MAX_ARENA 268435456u
#define SL_MAX_WORK 10000000u
#define SL_MAX_DEPTH 4096u

typedef struct SlValue SlValue;
typedef struct SlBig SlBig;
typedef struct SlChunk SlChunk;

struct SlBig { int sign; size_t len; uint32_t limb[]; };
typedef struct { uint32_t tag; size_t count; SlValue **field; } SlCon;
struct SlValue { uint32_t kind; union { SlBig *integer; SlCon con; } as; };
struct SlChunk { SlChunk *next; size_t used; size_t cap; max_align_t align; unsigned char data[]; };

static SlChunk *sl_arena = NULL;
static size_t sl_value_count = 0u;
static size_t sl_output_left = SL_MAX_OUTPUT;
static size_t sl_arena_bytes = 0u;
static size_t sl_work_left = SL_MAX_WORK;
static size_t sl_eval_depth = 0u;
static size_t sl_print_depth = 0u;

static void sl_die(const char *message) {
    fprintf(stderr, "sloph C11 runtime error: %s\n", message);
    exit(2);
}

static void sl_write(const char *data, size_t size) {
    if (size > sl_output_left) sl_die("output exceeds 1048576 bytes");
    if (size && fwrite(data, 1u, size, stdout) != size) sl_die("stdout write failed");
    sl_output_left -= size;
}

static void sl_text(const char *text) { sl_write(text, strlen(text)); }
static void sl_char(char value) { sl_write(&value, 1u); }

static void sl_print_u32(uint32_t value, int padded) {
    char buffer[16];
    int size = snprintf(buffer, sizeof(buffer), padded ? "%09" PRIu32 : "%" PRIu32, value);
    if (size < 0 || (size_t)size >= sizeof(buffer)) sl_die("integer formatting failed");
    sl_write(buffer, (size_t)size);
}

static void sl_charge(size_t amount) {
    if (amount > sl_work_left) sl_die("work limit exceeded (10000000)");
    sl_work_left -= amount;
}

static void sl_eval_enter(void) {
    if (++sl_eval_depth > SL_MAX_DEPTH) sl_die("evaluation depth exceeds 4096");
}

static void sl_eval_leave(void) { --sl_eval_depth; }

static void *sl_alloc(size_t size) {
    const size_t alignment = _Alignof(max_align_t);
    size = (size + alignment - 1u) & ~(alignment - 1u);
    if (sl_arena == NULL || size > sl_arena->cap - sl_arena->used) {
        size_t cap = size > 65536u ? size : 65536u;
        if (cap > SL_MAX_ARENA - sl_arena_bytes) sl_die("arena exceeds 268435456 bytes");
        SlChunk *chunk = (SlChunk *)malloc(sizeof(SlChunk) + cap);
        if (chunk == NULL) sl_die("out of memory");
        chunk->next = sl_arena; chunk->used = 0u; chunk->cap = cap; sl_arena = chunk; sl_arena_bytes += cap;
    }
    void *result = sl_arena->data + sl_arena->used;
    sl_arena->used += size;
    memset(result, 0, size);
    return result;
}

static void sl_destroy(void) {
    while (sl_arena != NULL) { SlChunk *next = sl_arena->next; free(sl_arena); sl_arena = next; }
}

static SlBig *sl_big(size_t len) {
    if (len > SL_MAX_LIMBS) sl_die("integer exceeds 16384 bits");
    SlBig *value = (SlBig *)sl_alloc(sizeof(SlBig) + len * sizeof(uint32_t));
    value->len = len;
    return value;
}

static SlValue *sl_int_wrap(SlBig *integer) {
    if (++sl_value_count > SL_MAX_VALUES) sl_die("value-node limit exceeded (100000)");
    SlValue *value = (SlValue *)sl_alloc(sizeof(SlValue));
    value->kind = 0u; value->as.integer = integer; return value;
}

static SlBig *sl_normalize(SlBig *value) {
    while (value->len && value->limb[value->len - 1u] == 0u) --value->len;
    if (!value->len) value->sign = 0;
    return value;
}

static SlValue *sl_int_literal(const char *text) {
    uint32_t limbs[SL_MAX_LIMBS] = {0}; size_t len = 0u; int sign = 1;
    if (*text == '-') { sign = -1; ++text; }
    for (; *text; ++text) {
        sl_charge(1u + len);
        uint32_t digit = (uint32_t)(*text - '0'); uint64_t carry = digit;
        for (size_t i = 0u; i < len; ++i) {
            uint64_t current = (uint64_t)limbs[i] * 10u + carry;
            limbs[i] = (uint32_t)current; carry = current >> 32u;
        }
        if (carry) { if (len == SL_MAX_LIMBS) sl_die("integer exceeds 16384 bits"); limbs[len++] = (uint32_t)carry; }
        if (!len && digit) { limbs[len++] = digit; }
    }
    SlBig *result = sl_big(len); result->sign = len ? sign : 0;
    if (len) memcpy(result->limb, limbs, len * sizeof(uint32_t));
    return sl_int_wrap(result);
}

static int sl_mag_cmp(const SlBig *a, const SlBig *b) {
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    for (size_t i = a->len; i-- > 0u;) if (a->limb[i] != b->limb[i]) return a->limb[i] < b->limb[i] ? -1 : 1;
    return 0;
}

static SlBig *sl_mag_add(const SlBig *a, const SlBig *b) {
    size_t n = a->len > b->len ? a->len : b->len;
    SlBig *r = sl_big(n + (n < SL_MAX_LIMBS ? 1u : 0u)); uint64_t carry = 0u;
    for (size_t i = 0u; i < n; ++i) {
        uint64_t x = i < a->len ? a->limb[i] : 0u, y = i < b->len ? b->limb[i] : 0u;
        uint64_t sum = x + y + carry; r->limb[i] = (uint32_t)sum; carry = sum >> 32u;
    }
    r->len = n;
    if (carry) { if (n == SL_MAX_LIMBS) sl_die("integer exceeds 16384 bits"); r->limb[r->len++] = (uint32_t)carry; }
    return r;
}

static SlBig *sl_mag_sub(const SlBig *a, const SlBig *b) {
    SlBig *r = sl_big(a->len); uint64_t borrow = 0u;
    for (size_t i = 0u; i < a->len; ++i) {
        uint64_t x = a->limb[i], y = (i < b->len ? b->limb[i] : 0u) + borrow;
        r->limb[i] = (uint32_t)(x - y); borrow = x < y;
    }
    r->len = a->len; return sl_normalize(r);
}

static SlValue *sl_int_add_signed(SlValue *av, SlValue *bv, int subtract) {
    if (av->kind != 0u || bv->kind != 0u) sl_die("integer primitive received non-integer value");
    SlBig *a = av->as.integer, *b = bv->as.integer; int bs = subtract ? -b->sign : b->sign; SlBig *r;
    sl_charge(1u + (a->len > b->len ? a->len : b->len));
    if (!a->sign) { r = sl_big(b->len); r->sign = bs; if (b->len) memcpy(r->limb,b->limb,b->len*4u); return sl_int_wrap(r); }
    if (!bs) { r = sl_big(a->len); r->sign = a->sign; if (a->len) memcpy(r->limb,a->limb,a->len*4u); return sl_int_wrap(r); }
    if (a->sign == bs) { r = sl_mag_add(a,b); r->sign = a->sign; }
    else { int cmp = sl_mag_cmp(a,b); if (!cmp) r = sl_big(0u); else if (cmp > 0) { r=sl_mag_sub(a,b); r->sign=a->sign; } else { r=sl_mag_sub(b,a); r->sign=bs; } }
    return sl_int_wrap(r);
}

static SlValue *sl_int_add(SlValue *a, SlValue *b) { return sl_int_add_signed(a,b,0); }
static SlValue *sl_int_sub(SlValue *a, SlValue *b) { return sl_int_add_signed(a,b,1); }

static SlValue *sl_int_mul(SlValue *av, SlValue *bv) {
    if (av->kind != 0u || bv->kind != 0u) sl_die("integer primitive received non-integer value");
    SlBig *a=av->as.integer, *b=bv->as.integer; uint32_t out[SL_MAX_LIMBS+1u] = {0};
    sl_charge(1u + a->len * b->len);
    if (!a->sign || !b->sign) return sl_int_wrap(sl_big(0u));
    if (a->len + b->len > SL_MAX_LIMBS + 1u) sl_die("integer exceeds 16384 bits");
    for (size_t i=0u;i<a->len;++i) {
        uint64_t carry=0u;
        for(size_t j=0u;j<b->len;++j) {
            size_t k=i+j; uint64_t cur=(uint64_t)a->limb[i]*b->limb[j]+out[k]+carry;
            out[k]=(uint32_t)cur; carry=cur>>32u;
        }
        size_t k=i+b->len;
        while(carry){
            if(k>=SL_MAX_LIMBS+1u)sl_die("integer exceeds 16384 bits");
            uint64_t cur=(uint64_t)out[k]+carry;out[k]=(uint32_t)cur;carry=cur>>32u;++k;
        }
    }
    size_t len=a->len+b->len; while(len && !out[len-1u])--len; if(len>SL_MAX_LIMBS)sl_die("integer exceeds 16384 bits");
    SlBig *r=sl_big(len);r->sign=a->sign*b->sign;if(len)memcpy(r->limb,out,len*4u);return sl_int_wrap(r);
}

static SlValue *sl_con(uint32_t tag, size_t count, SlValue **fields) {
    if (++sl_value_count > SL_MAX_VALUES) sl_die("value-node limit exceeded (100000)");
    SlValue *value=(SlValue *)sl_alloc(sizeof(SlValue));value->kind=1u;value->as.con.tag=tag;value->as.con.count=count;
    if(count){value->as.con.field=(SlValue **)sl_alloc(count*sizeof(SlValue *));memcpy(value->as.con.field,fields,count*sizeof(SlValue *));}
    return value;
}

static void sl_print_big(const SlBig *value) {
    if (!value->sign) { sl_text("0"); return; }
    uint32_t work[SL_MAX_LIMBS], chunks[SL_DECIMAL_CHUNKS]; size_t len=value->len,count=0u; memcpy(work,value->limb,len*4u);
    while(len){uint64_t rem=0u;for(size_t i=len;i-- >0u;){uint64_t cur=(rem<<32u)|work[i];work[i]=(uint32_t)(cur/1000000000u);rem=cur%1000000000u;}chunks[count++]=(uint32_t)rem;while(len&&!work[len-1u])--len;}
    if(value->sign<0)sl_char('-');sl_print_u32(chunks[--count],0);while(count)sl_print_u32(chunks[--count],1);
}
'''


class _Emitter:
    def __init__(self, unit: CoreUnit, symbol: str):
        self.unit = unit
        self.symbol = symbol
        self.definitions = {item.name: item for item in unit.definitions}
        self.functions = _functions(unit)
        names = sorted(self.definitions)
        self.global_ids = {name: index for index, name in enumerate(names)}
        constructors = sorted(
            constructor.name
            for enum in unit.types
            for constructor in enum.constructors
        )
        self.constructor_ids = {name: index for index, name in enumerate(constructors)}
        self.temp = 0

    def emit(self) -> str:
        output = [_RUNTIME]
        output.append(self._declarations())
        output.append(self._printer())
        for name in sorted(self.functions):
            output.append(self._function(self.functions[name]))
        for definition in sorted(self.unit.definitions, key=lambda item: item.name):
            if definition.name not in self.functions:
                output.append(self._global(definition))
        entry = self._gid(self.symbol)
        output.append(
            f"int main(void) {{ (void)&sl_int_add; (void)&sl_int_sub; (void)&sl_int_mul; (void)&sl_con; SlValue *result=sl_g{entry}(); sl_print_value(result); sl_char('\\n'); if(fflush(stdout)!=0||ferror(stdout))sl_die(\"stdout write failed\"); sl_destroy(); return 0; }}\n"
        )
        return "\n".join(output)

    def _declarations(self) -> str:
        lines: list[str] = []
        for name in sorted(self.functions):
            parameters = ", ".join(
                f"SlValue *a{i}" for i, _ in enumerate(self.functions[name].parameters)
            ) or "void"
            lines.append(f"static SlValue *sl_f{self._gid(name)}({parameters});")
        for name in sorted(self.definitions):
            if name not in self.functions:
                lines.append(f"static SlValue *sl_g{self._gid(name)}(void);")
        return "\n".join(lines) + "\n"

    def _printer(self) -> str:
        cases = []
        for name, tag in sorted(self.constructor_ids.items(), key=lambda item: item[1]):
            escaped = _c_string(name)
            cases.append(f'case {tag}u: sl_text("(con {escaped}"); break;')
        switch = " ".join(cases)
        return (
            "static void sl_print_node(SlValue *value) {\n"
            "  if(++sl_print_depth>SL_MAX_DEPTH)sl_die(\"print depth exceeds 4096\");\n"
            "  if(value->kind==0u){sl_text(\"(int \");sl_print_big(value->as.integer);sl_char(')');--sl_print_depth;return;}\n"
            f"  switch(value->as.con.tag){{{switch} default:sl_die(\"invalid constructor tag\");}}\n"
            "  for(size_t i=0u;i<value->as.con.count;++i){sl_char(' ');sl_print_node(value->as.con.field[i]);}sl_char(')');--sl_print_depth;\n"
            "}\n"
            "static void sl_print_value(SlValue *value){sl_text(\"(value 0 \");sl_print_node(value);sl_char(')');}\n"
        )

    def _function(self, function: _Function) -> str:
        environment = {
            binder.name: f"a{index}" for index, binder in enumerate(function.parameters)
        }
        parameters = ", ".join(
            f"SlValue *a{i}" for i, _ in enumerate(function.parameters)
        ) or "void"
        lines: list[str] = []
        lines.append("  sl_eval_enter();")
        lines.append("  sl_charge(1u);")
        for index, _ in enumerate(function.parameters):
            lines.append(f"  (void)a{index};")
        result = self._expr(function.body, environment, lines, "  ")
        lines.append("  sl_eval_leave();")
        lines.append(f"  return {result};")
        return f"static SlValue *sl_f{self._gid(function.definition.name)}({parameters}) {{\n" + "\n".join(lines) + "\n}\n"

    def _global(self, definition: Definition) -> str:
        gid = self._gid(definition.name)
        lines: list[str] = []
        result = self._expr(definition.value, {}, lines, "  ")
        body = "\n".join(lines)
        return (
            f"static SlValue *sl_g{gid}_cache=NULL; static unsigned sl_g{gid}_state=0u;\n"
            f"static SlValue *sl_g{gid}(void) {{\n"
            f"  if(sl_g{gid}_state==2u)return sl_g{gid}_cache;if(sl_g{gid}_state==1u)sl_die(\"cyclic data global\");sl_g{gid}_state=1u;sl_eval_enter();\n"
            f"{body}\n  sl_g{gid}_cache={result};sl_g{gid}_state=2u;sl_eval_leave();return {result};\n}}\n"
        )

    def _new(self) -> str:
        value = f"t{self.temp}"
        self.temp += 1
        return value

    def _gid(self, name: str) -> int:
        return self.global_ids[name]

    def _expr(self, expression: Expr, environment: dict[str, str], lines: list[str], indent: str) -> str:
        if isinstance(expression, IntExpr):
            result=self._new(); lines.append(f'{indent}SlValue *{result}=sl_int_literal("{decimal_string(expression.value)}");'); return result
        if isinstance(expression, LocalExpr): return environment[expression.name]
        if isinstance(expression, GlobalExpr):
            result=self._new(); lines.append(f"{indent}SlValue *{result}=sl_g{self._gid(expression.name)}();"); return result
        if isinstance(expression, AppExpr):
            target,args=_flatten_application(expression); values=[self._expr(item,environment,lines,indent) for item in args]
            result=self._new(); lines.append(f"{indent}SlValue *{result}=sl_f{self._gid(target.name)}({', '.join(values)});"); return result
        if isinstance(expression, LetExpr):
            value=self._expr(expression.value,environment,lines,indent); lines.append(f"{indent}(void){value};"); local=dict(environment); local[expression.binder.name]=value; return self._expr(expression.body,local,lines,indent)
        if isinstance(expression, PrimExpr):
            values=[self._expr(item,environment,lines,indent) for item in expression.arguments]; op={"int.add":"add","int.sub":"sub","int.mul":"mul"}[expression.name]
            result=self._new(); lines.append(f"{indent}SlValue *{result}=sl_int_{op}({values[0]}, {values[1]});"); return result
        if isinstance(expression, ConExpr):
            values=[self._expr(item,environment,lines,indent) for item in expression.fields]; result=self._new()
            if values:
                array=self._new(); lines.append(f"{indent}SlValue *{array}[]={{"+', '.join(values)+"};"); lines.append(f"{indent}SlValue *{result}=sl_con({self.constructor_ids[expression.constructor]}u,{len(values)}u,{array});")
            else: lines.append(f"{indent}SlValue *{result}=sl_con({self.constructor_ids[expression.constructor]}u,0u,NULL);")
            return result
        if isinstance(expression, CaseExpr):
            scrutinee=self._expr(expression.scrutinee,environment,lines,indent); result=self._new(); lines.append(f"{indent}if({scrutinee}->kind!=1u)sl_die(\"case received non-constructor value\");"); lines.append(f"{indent}SlValue *{result}=NULL;"); lines.append(f"{indent}switch({scrutinee}->as.con.tag){{")
            for alternative in sorted(
                expression.alternatives, key=lambda item: item.constructor
            ):
                lines.append(f"{indent}case {self.constructor_ids[alternative.constructor]}u: {{"); local=dict(environment)
                for index,binder in enumerate(alternative.binders):
                    name=self._new(); lines.append(f"{indent}  SlValue *{name}={scrutinee}->as.con.field[{index}u];"); lines.append(f"{indent}  (void){name};"); local[binder.name]=name
                branch=self._expr(alternative.body,local,lines,indent+"  "); lines.append(f"{indent}  {result}={branch};break;\n{indent}}}")
            lines.append(f"{indent}default:sl_die(\"invalid case constructor\");}}")
            return result
        raise AssertionError(f"unsupported expression {type(expression)!r}")


def _c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')
