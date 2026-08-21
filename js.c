/* $Id$ */
/*
 * js.c: embedded JavaScript execution (QuickJS) with a minimal DOM shim.
 *
 * Only the pieces needed to make JS-required pages degrade gracefully are
 * implemented: document.write/writeln, document.title, location redirects
 * and common no-op stubs.  There is no DOM tree, no event loop, no timers
 * and no network access from scripts themselves.
 */
#include "fm.h"

#ifdef USE_JS

#include <time.h>
#include <js/quickjs.h>

#include "proto.h"

#define JS_MEMORY_LIMIT		(64 * 1024 * 1024)
#define JS_STACK_LIMIT		(1024 * 1024)
#define JS_SCRIPT_TIMEOUT	5	/* seconds per script */
#define JS_DOCWRITE_LIMIT	(1024 * 1024)

static JSRuntime *js_rt = NULL;
static JSContext *js_ctx = NULL;
static JSValue js_doc_obj;
static JSValue js_loc_obj;
static Buffer *js_buf = NULL;
static int js_depth = 0;
static time_t js_deadline;

static Str js_docwrite = NULL;
static char *js_title = NULL;
static char *js_redirect = NULL;
static int js_redirect_done = FALSE;

int
js_enabled(void)
{
    return js_ctx != NULL;
}

ParsedURL *
js_baseURL(void)
{
    if (js_buf == NULL)
	return NULL;
    return baseURL(js_buf);
}

static char *
js_page_url(void)
{
    if (js_buf == NULL)
	return "";
    return parsedURL2Str(&js_buf->currentURL)->ptr;
}

/*
 * document.write/writeln
 */
static JSValue
js_doc_write(JSContext *ctx, JSValueConst this_val, int argc,
	     JSValueConst *argv, int magic)
{
    int i;
    const char *s;

    if (js_docwrite == NULL)
	js_docwrite = Strnew();
    for (i = 0; i < argc; i++) {
	s = JS_ToCString(ctx, argv[i]);
	if (s == NULL)
	    continue;
	if (js_docwrite->length + (int)strlen(s) <= JS_DOCWRITE_LIMIT)
	    Strcat_charp(js_docwrite, s);
	JS_FreeCString(ctx, s);
    }
    if (magic)
	Strcat_char(js_docwrite, '\n');
    return JS_UNDEFINED;
}

static JSValue
js_doc_write_f(JSContext *ctx, JSValueConst this_val, int argc,
	       JSValueConst *argv)
{
    return js_doc_write(ctx, this_val, argc, argv, 0);
}

static JSValue
js_doc_writeln_f(JSContext *ctx, JSValueConst this_val, int argc,
		 JSValueConst *argv)
{
    return js_doc_write(ctx, this_val, argc, argv, 1);
}

/*
 * DOM stubs
 */
static JSValue
js_return_null(JSContext *ctx, JSValueConst this_val, int argc,
	       JSValueConst *argv)
{
    return JS_NULL;
}

static JSValue
js_return_empty_array(JSContext *ctx, JSValueConst this_val, int argc,
		      JSValueConst *argv)
{
    return JS_NewArray(ctx);
}

static JSValue
js_return_empty_object(JSContext *ctx, JSValueConst this_val, int argc,
		       JSValueConst *argv)
{
    return JS_NewObject(ctx);
}

static JSValue
js_noop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    return JS_UNDEFINED;
}

static JSValue
js_return_zero(JSContext *ctx, JSValueConst this_val, int argc,
	       JSValueConst *argv)
{
    return JS_NewInt32(ctx, 0);
}

/*
 * location
 */
static void
js_set_redirect(const char *url)
{
    if (js_redirect_done || url == NULL || *url == '\0')
	return;
    js_redirect_done = TRUE;
    js_redirect = Strnew_charp(url)->ptr;
}

static JSValue
js_loc_href_get(JSContext *ctx, JSValueConst this_val)
{
    return JS_NewString(ctx, js_page_url());
}

static JSValue
js_loc_href_set(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    const char *s;

    s = JS_ToCString(ctx, val);
    if (s) {
	js_set_redirect(s);
	JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

static JSValue
js_loc_go(JSContext *ctx, JSValueConst this_val, int argc,
	  JSValueConst *argv)
{
    if (argc > 0)
	js_loc_href_set(ctx, this_val, argv[0]);
    return JS_UNDEFINED;
}

/*
 * evaluation
 */
static int
js_interrupt(JSRuntime *rt, void *opaque)
{
    if (time(NULL) >= js_deadline) {
	if (js_ctx)
	    JS_ThrowInternalError(js_ctx, "script execution timed out");
	return 1;
    }
    return 0;
}

/* strip the legacy "<!-- ... // -->" comment hiding */
static char *
js_unhide(char *code)
{
    char *p = code, *q;
    size_t len;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
	p++;
    if (strncmp(p, "<!--", 4) == 0) {
	q = strchr(p, '\n');
	if (q)
	    p = q + 1;
	else
	    p += 4;
    }
    len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' ||
		       p[len - 1] == '\n' || p[len - 1] == '\r'))
	len--;
    if (len >= 3 && strncmp(p + len - 3, "-->", 3) == 0) {
	len -= 3;
	if (len >= 2 && strncmp(p + len - 2, "//", 2) == 0)
	    len -= 2;
    }
    p[len] = '\0';
    return p;
}

void
js_eval_source(Str body)
{
    JSValue val, exc;
    char *code;

    if (js_ctx == NULL || body == NULL || body->length == 0)
	return;
    code = js_unhide(body->ptr);
    if (*code == '\0')
	return;
    js_deadline = time(NULL) + JS_SCRIPT_TIMEOUT;
    val = JS_Eval(js_ctx, code, strlen(code), "<script>",
		  JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
	exc = JS_GetException(js_ctx);
	JS_FreeValue(js_ctx, exc);
    }
    JS_FreeValue(js_ctx, val);
    JS_RunGC(js_rt);
}

/*
 * pending results for the parser (file.c)
 */
char *
js_take_docwrite(void)
{
    Str s;

    if (js_docwrite == NULL || js_docwrite->length == 0) {
	js_docwrite = NULL;
	return NULL;
    }
    s = js_docwrite;
    js_docwrite = NULL;
    return s->ptr;
}

char *
js_take_title(void)
{
    JSValue v;
    const char *s;

    if (js_ctx == NULL)
	return NULL;
    v = JS_GetPropertyStr(js_ctx, js_doc_obj, "title");
    if (JS_IsException(v)) {
	JSValue exc = JS_GetException(js_ctx);
	JS_FreeValue(js_ctx, exc);
    }
    else if (JS_IsString(v)) {
	s = JS_ToCString(js_ctx, v);
	if (s && *s && (js_title == NULL || strcmp(js_title, s) != 0))
	    js_title = Strnew_charp(s)->ptr;
	if (s)
	    JS_FreeCString(js_ctx, s);
    }
    JS_FreeValue(js_ctx, v);
    if (js_title == NULL)
	return NULL;
    s = js_title;
    js_title = NULL;
    return (char *)s;
}

char *
js_take_redirect(void)
{
    char *u, *p;

    if (js_redirect == NULL)
	return NULL;
    u = js_redirect;
    js_redirect = NULL;
    if (js_buf == NULL)
	return u;
#ifdef USE_M17N
    p = url_encode(u, baseURL(js_buf), js_buf->document_charset);
#else
    p = url_encode(u, baseURL(js_buf), 0);
#endif
    return p;
}

/*
 * global environment
 */
static void
js_set_function(JSContext *ctx, JSValueConst obj, const char *name,
		JSCFunction *func)
{
    JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, func, name, 0));
}

static JSValue
js_new_getter(JSContext *ctx, const char *name,
	      JSValue (*g)(JSContext *, JSValueConst))
{
    JSCFunctionType ft;

    ft.getter = g;
    return JS_NewCFunction2(ctx, ft.generic, name, 0, JS_CFUNC_getter, 0);
}

static JSValue
js_new_setter(JSContext *ctx, const char *name,
	      JSValue (*s)(JSContext *, JSValueConst, JSValueConst))
{
    JSCFunctionType ft;

    ft.setter = s;
    return JS_NewCFunction2(ctx, ft.generic, name, 1, JS_CFUNC_setter, 0);
}

static void
js_install_globals(void)
{
    JSValue glob, nav, cons;

    glob = JS_GetGlobalObject(js_ctx);

    js_doc_obj = JS_NewObject(js_ctx);
    js_set_function(js_ctx, js_doc_obj, "write", js_doc_write_f);
    js_set_function(js_ctx, js_doc_obj, "writeln", js_doc_writeln_f);
    js_set_function(js_ctx, js_doc_obj, "getElementById", js_return_null);
    js_set_function(js_ctx, js_doc_obj, "querySelector", js_return_null);
    js_set_function(js_ctx, js_doc_obj, "querySelectorAll",
		    js_return_empty_array);
    js_set_function(js_ctx, js_doc_obj, "getElementsByTagName",
		    js_return_empty_array);
    js_set_function(js_ctx, js_doc_obj, "getElementsByName",
		    js_return_empty_array);
    js_set_function(js_ctx, js_doc_obj, "createElement",
		    js_return_empty_object);
    js_set_function(js_ctx, js_doc_obj, "createTextNode",
		    js_return_empty_object);
    js_set_function(js_ctx, js_doc_obj, "addEventListener", js_noop);
    js_set_function(js_ctx, js_doc_obj, "removeEventListener", js_noop);
    js_set_function(js_ctx, js_doc_obj, "close", js_noop);
    js_set_function(js_ctx, js_doc_obj, "open", js_return_null);
    JS_SetPropertyStr(js_ctx, js_doc_obj, "title",
		      JS_NewString(js_ctx, ""));
    JS_SetPropertyStr(js_ctx, js_doc_obj, "cookie",
		      JS_NewString(js_ctx, ""));
    JS_SetPropertyStr(js_ctx, js_doc_obj, "referrer",
		      JS_NewString(js_ctx, ""));
    JS_SetPropertyStr(js_ctx, js_doc_obj, "URL",
		      JS_NewString(js_ctx, js_page_url()));
    JS_SetPropertyStr(js_ctx, glob, "document", JS_DupValue(js_ctx, js_doc_obj));

    js_loc_obj = JS_NewObject(js_ctx);
    JS_DefinePropertyGetSet(js_ctx, js_loc_obj, JS_NewAtom(js_ctx, "href"),
			js_new_getter(js_ctx, "get href", js_loc_href_get),
			js_new_setter(js_ctx, "set href", js_loc_href_set),
			    JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE |
			    JS_PROP_GETSET);
    js_set_function(js_ctx, js_loc_obj, "replace", js_loc_go);
    js_set_function(js_ctx, js_loc_obj, "assign", js_loc_go);
    js_set_function(js_ctx, js_loc_obj, "reload", js_noop);
    JS_SetPropertyStr(js_ctx, js_doc_obj, "location",
		      JS_DupValue(js_ctx, js_loc_obj));
    JS_SetPropertyStr(js_ctx, glob, "location", JS_DupValue(js_ctx, js_loc_obj));

    nav = JS_NewObject(js_ctx);
    JS_SetPropertyStr(js_ctx, nav, "userAgent",
		      JS_NewString(js_ctx,
				   (UserAgent != NULL && *UserAgent != '\0')
				   ? UserAgent : w3m_version));
    JS_SetPropertyStr(js_ctx, nav, "appName",
		      JS_NewString(js_ctx, "w3m"));
    JS_SetPropertyStr(js_ctx, nav, "appVersion",
		      JS_NewString(js_ctx, w3m_version));
    JS_SetPropertyStr(js_ctx, nav, "language",
		      JS_NewString(js_ctx, "en"));
    JS_SetPropertyStr(js_ctx, glob, "navigator", nav);

    cons = JS_NewObject(js_ctx);
    js_set_function(js_ctx, cons, "log", js_noop);
    js_set_function(js_ctx, cons, "info", js_noop);
    js_set_function(js_ctx, cons, "warn", js_noop);
    js_set_function(js_ctx, cons, "error", js_noop);
    js_set_function(js_ctx, cons, "debug", js_noop);
    JS_SetPropertyStr(js_ctx, glob, "console", cons);

    js_set_function(js_ctx, glob, "setTimeout", js_return_zero);
    js_set_function(js_ctx, glob, "setInterval", js_return_zero);
    js_set_function(js_ctx, glob, "clearTimeout", js_noop);
    js_set_function(js_ctx, glob, "clearInterval", js_noop);
    js_set_function(js_ctx, glob, "requestAnimationFrame", js_return_zero);
    js_set_function(js_ctx, glob, "alert", js_noop);
    js_set_function(js_ctx, glob, "confirm", js_return_null);
    js_set_function(js_ctx, glob, "prompt", js_return_null);
    js_set_function(js_ctx, glob, "atob", js_return_null);
    js_set_function(js_ctx, glob, "btoa", js_return_null);

    JS_SetPropertyStr(js_ctx, glob, "window",
		      JS_DupValue(js_ctx, glob));
    JS_SetPropertyStr(js_ctx, glob, "self", JS_DupValue(js_ctx, glob));
    JS_SetPropertyStr(js_ctx, glob, "top", JS_DupValue(js_ctx, glob));

    JS_FreeValue(js_ctx, glob);
}

/*
 * lifecycle
 */
void
js_html_start(Buffer *buf)
{
    if (js_depth++ > 0)
	return;
    js_docwrite = NULL;
    js_title = NULL;
    js_redirect = NULL;
    js_redirect_done = FALSE;
    if (!use_javascript)
	return;
    js_rt = JS_NewRuntime();
    if (js_rt == NULL)
	return;
    JS_SetMemoryLimit(js_rt, JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(js_rt, JS_STACK_LIMIT);
    JS_SetInterruptHandler(js_rt, js_interrupt, NULL);
    js_ctx = JS_NewContext(js_rt);
    if (js_ctx == NULL) {
	JS_FreeRuntime(js_rt);
	js_rt = NULL;
	return;
    }
    js_buf = buf;
    js_install_globals();
}

void
js_html_end(void)
{
    if (--js_depth > 0)
	return;
    if (js_ctx) {
	JS_FreeValue(js_ctx, js_doc_obj);
	JS_FreeValue(js_ctx, js_loc_obj);
	JS_FreeContext(js_ctx);
	js_ctx = NULL;
    }
    if (js_rt) {
	JS_FreeRuntime(js_rt);
	js_rt = NULL;
    }
    js_buf = NULL;
    js_docwrite = NULL;
    js_title = NULL;
    js_redirect = NULL;
    js_redirect_done = FALSE;
}

#endif				/* USE_JS */

/* Local Variables:    */
/* c-basic-offset: 4   */
/* tab-width: 8        */
/* End:                */
/* vim: set expandtab ts=8: */
