#include "sloph/context.h"
#include "sloph/syntax.h"

#include <assert.h>
#include <string.h>

static SlophContext *context(void) {
    SlophContext *value = NULL;
    assert(sloph_context_create(NULL, &value) == SLOPH_STATUS_OK);
    return value;
}

static void basic_v0(void) {
    static const unsigned char source[] =
        "module example::main; public value main:Int { primitive int.add(20,22) }";
    static const char expected[] =
        "module example::main;\n\npublic value main: Int {\n"
        "  primitive int.add(20, 22)\n}\n";
    static const char expected_json[] =
        "{\"module\":{\"functions\":[],\"imports\":[],\"kind\":\"Module\","
        "\"name\":\"example::main\",\"span\":{\"end\":72,\"start\":0},"
        "\"types\":[],\"values\":[{\"kind\":\"ValueDecl\",\"name\":\"main\","
        "\"public\":true,\"span\":{\"end\":72,\"start\":22},\"type\":{"
        "\"kind\":\"IntType\",\"span\":{\"end\":43,\"start\":40}},"
        "\"value\":{\"bindings\":[],\"kind\":\"Block\",\"result\":{"
        "\"arguments\":[{\"kind\":\"IntExpr\",\"span\":{\"end\":66,"
        "\"start\":64},\"value\":\"20\"},{\"kind\":\"IntExpr\",\"span\":{"
        "\"end\":69,\"start\":67},\"value\":\"22\"}],\"kind\":"
        "\"PrimitiveExpr\",\"name\":\"int.add\",\"span\":{\"end\":70,"
        "\"start\":46}},\"span\":{\"end\":72,\"start\":44}}}]},"
        "\"schema\":\"sloph.syntax\",\"version\":0}\n";
    SlophContext *ctx = context();
    SlophSyntaxModule *module = NULL;
    SlophSyntaxText text = {0};
    assert(sloph_syntax_parse(ctx, source, sizeof(source) - 1u, 0u, &module) ==
           SLOPH_STATUS_OK);
    assert(sloph_syntax_format(ctx, module, &text) == SLOPH_STATUS_OK);
    assert(text.length == sizeof(expected) - 1u);
    assert(memcmp(text.data, expected, text.length) == 0);
    sloph_syntax_text_free(ctx, &text);
    assert(sloph_syntax_to_json(ctx, module, &text) == SLOPH_STATUS_OK);
    assert(strcmp(text.data, expected_json) == 0);
    {
        SlophSyntaxModule *decoded = NULL;
        SlophSyntaxText roundtrip = {0};
        assert(sloph_syntax_from_json(ctx, (const unsigned char *)text.data,
                                     text.length, 0u, &decoded) ==
               SLOPH_STATUS_OK);
        assert(sloph_syntax_to_json(ctx, decoded, &roundtrip) ==
               SLOPH_STATUS_OK);
        assert(strcmp(roundtrip.data, expected_json) == 0);
        sloph_syntax_text_free(ctx, &roundtrip);
        sloph_syntax_module_free(decoded);
    }
    sloph_syntax_text_free(ctx, &text);
    sloph_syntax_module_free(module);
    sloph_context_destroy(ctx);
}

static void v1_transform(void) {
    static const unsigned char source[] =
        "module transform_case; const main: Int { unless 1 < 2 { 10 } else { 42 } }";
    SlophContext *ctx = context(); SlophSyntaxModule *module = NULL;
    SlophSyntaxText json = {0};
    assert(sloph_syntax_parse(ctx, source, sizeof(source)-1u, 1u, &module) == SLOPH_STATUS_OK);
    assert(sloph_syntax_to_json(ctx, module, &json) == SLOPH_STATUS_OK);
    assert(strstr(json.data, "\"kind\":\"IfExpr\"") != NULL);
    sloph_syntax_text_free(ctx, &json); sloph_syntax_module_free(module); sloph_context_destroy(ctx);
}

static void rejects_v0_zero_parameters(void) {
    static const unsigned char source[] = "module invalid; fn zero() -> Int { 0 }";
    SlophContext *ctx=context();SlophSyntaxModule*module=NULL;
    assert(sloph_syntax_parse(ctx,source,sizeof(source)-1u,0u,&module)==SLOPH_STATUS_INVALID_ARGUMENT);
    assert(module==NULL);assert(sloph_context_diagnostic_count(ctx)==1u);sloph_context_destroy(ctx);
}

static void rich_json_roundtrip(void) {
    static const unsigned char source[] =
        "module rich;\n"
        "owned type Box[Item] { Box(item: Item); }\n"
        "intrinsic type Token;\n"
        "intrinsic fn identity(x: Int) -> Int = int.identity;\n"
        "foreign fn raw(x: Int) -> Int = foreign.raw;\n"
        "fn choose(x: Int) -> Int { defer identity(x); case Bool::True() -> Int {\n"
        "Bool::True() => { x } Bool::False() => { 0 } } }\n"
        "const answer: Int { 42 }\n";
    SlophContext *ctx=context();SlophSyntaxModule *parsed=NULL,*decoded=NULL;
    SlophSyntaxText first={0},second={0};
    assert(sloph_syntax_parse(ctx,source,sizeof(source)-1u,1u,&parsed)==SLOPH_STATUS_OK);
    assert(sloph_syntax_to_json(ctx,parsed,&first)==SLOPH_STATUS_OK);
    assert(sloph_syntax_from_json(ctx,(const unsigned char*)first.data,first.length,1u,&decoded)==SLOPH_STATUS_OK);
    assert(sloph_syntax_to_json(ctx,decoded,&second)==SLOPH_STATUS_OK);
    assert(first.length==second.length&&memcmp(first.data,second.data,first.length)==0);
    sloph_syntax_text_free(ctx,&first);sloph_syntax_text_free(ctx,&second);
    sloph_syntax_module_free(parsed);sloph_syntax_module_free(decoded);sloph_context_destroy(ctx);
}

int main(void) { basic_v0(); v1_transform(); rejects_v0_zero_parameters(); rich_json_roundtrip(); return 0; }
