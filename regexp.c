/*
 *  regexp.c
 *  Copyright 2013 Masatoshi Teruya All rights reserved.
 */
#include "regexp.h"
#include <string.h>
#include <libbuf.h>
#include <lauxlib.h>

// memory alloc/dealloc
#define palloc(t)       ((t*)malloc(sizeof(t)))
#define pnalloc(n,t)    ((t*)malloc(n*sizeof(t)))
#define pcalloc(n,t)    ((t*)calloc(n,sizeof(t)))
#define pdealloc(p)     (free((void*)p))

void regexp_global_init( void )
{
    onig_init();
}
void regexp_global_dispose( void )
{
    onig_end();
}

int regexp_init( Regexp_t *re, const unsigned char *pattern, size_t len, 
                 OnigOptionType opt, OnigEncoding enc, OnigSyntaxType *syntax )
{
    // new regexp
    return onig_new( &re->obj, pattern, pattern + len, opt, enc, 
                     syntax, &re->err );
}

void regexp_dispose( Regexp_t *re )
{
    if( re->obj ){
        onig_free( re->obj );
    }
}

int regexp_alloc( Regexp_t **newre, const unsigned char *pattern, size_t len, 
                  OnigOptionType opt, OnigEncoding enc, OnigSyntaxType *syntax )
{
    int rc = REGEXP_OK;
    Regexp_t *re = palloc( Regexp_t );
    
    if( !re ){
        return REGEXP_ENOMEM;
    }
    // new regexp
    else if( ( rc = regexp_init( re, pattern, len, opt, enc, syntax ) ) != ONIG_NORMAL ){
        pdealloc( re );
        return rc;
    }
    // init error info
    *newre = re;
            
    return REGEXP_OK;
}

void regexp_dealloc( Regexp_t *re )
{
    regexp_dispose( re );
    pdealloc( re );
}

int regexp_test( Regexp_t *re, const char *str, size_t len )
{
    unsigned char *src = (unsigned char*)str;
    
    return onig_search( re->obj, src, src + len, src, src + len, NULL, 
                        ONIG_OPTION_NONE );
}


int regexp_exec( Regexp_t *re, const char *str, size_t len, size_t lastIdx, 
                 RegexpMatch_t *idx )
{
    int rc = ONIG_NORMAL;
    OnigRegion region;
    unsigned char *src = (unsigned char*)str;
    unsigned char *start = src + lastIdx;
    unsigned char *end = src + len;
	unsigned char *range = end;
    
    onig_region_init( &region );
    idx->head = idx->len = idx->num = 0;
    if( ( rc = onig_search( re->obj, src, end, start, range, &region, 
                            ONIG_OPTION_NONE ) ) > ONIG_MISMATCH )
    {
        int group = 1;
        int gidx = 0;
        
        idx->head = region.beg[0];
        idx->len = region.end[0] - region.beg[0];
        idx->num = region.num_regs - 1;
        for( ; group < region.num_regs; group++ )
        {
            gidx = group - 1;
            // not empty group
            if( region.beg[group] != -1 ){
                idx->group[gidx].head = region.beg[group];
                idx->group[gidx].len = region.end[group] - region.beg[group];
            }
            else {
                idx->group[gidx].head = -1;
                idx->group[gidx].len = -1;
            }
        }
        
        rc = REGEXP_OK;
    }
    
	onig_region_free( &region, 0 );
   
	return rc;
}

int regexp_exec_cb( Regexp_t *re, const char *str, size_t len, REGEXP_CB cb, 
                    void *arg )
{
    int rc = REGEXP_OK;
    OnigRegion region;
    unsigned char *src = (unsigned char*)str;
    unsigned char *start = src;
    unsigned char *end = src + len;
	unsigned char *range = end;
    
    onig_region_init( &region );
    while( ( rc = onig_search( re->obj, src, end, start, range, &region, 
                            ONIG_OPTION_NONE ) ) > ONIG_MISMATCH )
    {
        start = src + region.end[0];
        if( ( rc = cb( str, &region, arg ) ) != REGEXP_AGAIN ){
            break;
        }
    }
    
    onig_region_free( &region, 0 );
	
	return ( rc == ONIG_MISMATCH ) ? REGEXP_OK : rc;
}


// MARK: lua binding
#define REGEXP_LUA        "Regexp"
#define REGEXP_LUA_MAX_GROUPS  255

typedef struct {
    Regexp_t *re;
    int global;
    size_t lastIdx;
    buf_strfmt_t *fmt;
} RegexpLua_t;

typedef struct {
    lua_State *L;
    RegexpLua_t *wrap;
    buf_t *buf;
    buf_strfmt_t *fmt;
    ssize_t diff;
} ReplaceLuaCbCtx_t;

// open table to table-field(nested table)
#define openL_ttable(L,k) ( \
    lua_pushstring(L,k), \
    lua_newtable(L))
// close table
#define closeL_ttable(L)    lua_rawset(L,-3)

// set function at table-field
#define setL_tfunction(L,k,v) ( \
    lua_pushstring(L,k), \
    lua_pushcfunction(L,v), \
    lua_rawset(L,-3))

// set string at table-index
#define setL_tinstring(L,i,v,n) ( \
    lua_pushnumber(L,i), \
    lua_pushlstring(L,v,n), \
    lua_rawset(L,-3))

static int regexp_test_lua( lua_State *L )
{
    RegexpLua_t *wrap = luaL_checkudata( L, 1, REGEXP_LUA );
    size_t len = 0;
    const char *str = luaL_checklstring( L, 2, &len );
    int match = regexp_test( wrap->re, str, len );
    
    if( match > REGEXP_MISS ){
        lua_pushboolean( L, 1 );
    }
    else if( match == REGEXP_MISS ){
        lua_pushboolean( L, 0 );
    }
    else if( REGEXP_IS_ERROR_ONIG( match ) ){
        char buf[REGEXP_ERRSTR_LEN] = {0};
        return luaL_error( L, "%s", regexp_strerror( buf, match ) );
    }
    
    return 1;
}


static int regexp_exec_lua( lua_State *L )
{
    RegexpLua_t *wrap = luaL_checkudata( L, 1, REGEXP_LUA );
    size_t len = 0;
    const char *str = luaL_checklstring( L, 2, &len );
    RegexpMatch_t idx;
    int match = regexp_exec( wrap->re, str, len, 
                             ( wrap->global && wrap->lastIdx <= len ) ? 
                             wrap->lastIdx : 0, &idx );
    
    if( match > REGEXP_MISS )
    {
        RegexpIdx_t grp;
        
        lua_newtable( L );
        setL_tinstring( L, 1, str + idx.head, idx.len );
        for( len = 0; len < idx.num; len++ ){
            grp = idx.group[len];
            setL_tinstring( L, len + 2, str + grp.head, grp.len );
        }
        if( wrap->global ){
            wrap->lastIdx = idx.head + idx.len;
        }
    }
    else if( match == REGEXP_MISS ){
        wrap->lastIdx = 0;
        lua_pushnil( L );
    }
    else if( REGEXP_IS_ERROR_ONIG( match ) ){
        char buf[REGEXP_ERRSTR_LEN] = {0};
        return luaL_error( L, "%s", regexp_strerror( buf, match ) );
    }
    
    return 1;
}

static int regexp_replace_lua_cb( const char *src, OnigRegion *region, void *arg )
{
    int rc = REGEXP_OK;
    ReplaceLuaCbCtx_t *ctx = (ReplaceLuaCbCtx_t*)arg;
    char *sub = NULL;
    size_t len = 0;
    
    if( region->num_regs > 1 )
    {
        int nreg = region->num_regs - 1;
        const char *subs[nreg];
        int i = 1;
        
        for(; i < region->num_regs; i++ ){
            len = region->end[i] - region->beg[i];
            sub = alloca( ( sizeof( char ) * len ) );
            memcpy( sub, src + region->beg[i], len );
            sub[len] = 0;
            subs[i-1] = sub;
        }
        
        if( !( sub = buf_strfmt( ctx->fmt, nreg, subs, &len ) ) ){
            return errno;
        }
    }
    else if( !( sub = buf_strfmt( ctx->fmt, 0, NULL, &len ) ) ){
        return errno;
    }
    
    rc = buf_strnsub_range( ctx->buf, region->beg[0] + ctx->diff, 
                            region->end[0] + ctx->diff, sub, len );
    ctx->diff += len - ( region->end[0] - region->beg[0] );
    free( (void*)sub );
    
    if( rc == BUF_OK && ctx->wrap->global ){
        rc = REGEXP_AGAIN;
    }
    
    return rc;
}

static int regexp_replace_lua( lua_State *L )
{
    int rc = 1;
    size_t len = 0;
    const char *src = luaL_checklstring( L, 2, &len );
    
    if( len )
    {
        int argc = lua_gettop( L );
        RegexpLua_t *wrap = luaL_checkudata( L, 1, REGEXP_LUA );
        buf_t buf;
        buf_strfmt_t fmt;
        buf_strfmt_t *fmtPtr = &fmt;
        
        // passed format-string
        if( argc > 2 )
        {
            size_t flen = 0;
            const char *fmtstr = luaL_checklstring( L, 3, &flen );
            
            if( buf_strfmt_init( fmtPtr, ( flen ) ? fmtstr : "", flen, 
                                 regexp_ngroups( wrap->re ) ) != BUF_OK ){
                return luaL_error( L, "failed to regexp.replace() - %s", 
                                   strerror( errno ) );
            }
        }
        // compiled formatter
        else if( wrap->fmt ){
            fmtPtr = wrap->fmt;
        }
        // undefined formatter
        else {
            return luaL_error( L, 
                        "failed to regexp.replace() - "
                        "arguments#2 undefined replace string, %s", 
                        strerror(EINVAL) );
        }
        
        if( buf_init( &buf, sizeof( char ) * len ) == BUF_OK )
        {
            if( buf_strnset( &buf, src, len ) == BUF_OK )
            {
                ReplaceLuaCbCtx_t ctx = {
                    .L = L,
                    .wrap = wrap,
                    .buf = &buf,
                    .fmt = fmtPtr,
                    .diff = 0
                };
                
                rc = regexp_exec_cb( wrap->re, src, len, regexp_replace_lua_cb, 
                                     (void*)&ctx );
                
                if( rc == REGEXP_OK ){
                    lua_pushlstring( L, buf_mem( &buf ), buf_used( &buf ) );
                    rc = 1;
                }
                else {
                    char errbuf[REGEXP_ERRSTR_LEN] = {0};
                    rc = luaL_error( L, "%s", regexp_strerror( errbuf, rc ) );
                }
            }
            else {
                rc = luaL_error( L, "%s", strerror( errno ) );
            }
            buf_dispose( &buf );
        }
        else {
            rc = luaL_error( L, "%s", strerror( rc ) );
        }
        
        if( fmtPtr != wrap->fmt ){
            buf_strfmt_dispose( fmtPtr );
        }
    }
    else {
        lua_pushnil( L );
    }
    
    return rc;
}

static int regexp_alloc_lua( lua_State *L )
{
    int rc = 0;
    int argc = lua_gettop( L );
    size_t plen = 0;
    const char *pattern = luaL_checklstring( L, 1, &plen );
    OnigOptionType opts = ONIG_OPTION_NONE;
    OnigEncoding enc = ONIG_ENCODING_UTF8;
    RegexpLua_t *wrap = NULL;
    Regexp_t *re = NULL;
    buf_strfmt_t *fmt = NULL;
    size_t flen = 0;
    const char *format = NULL;
    int global = 0;
    
    // check flags
    if( argc > 1 )
    {
        const char *flags = luaL_checkstring( L, 2 );
        char *ptr = (char*)flags;
        
        while( *ptr )
        {
            switch ( *ptr ) {
                case 'g':
                    global = 1;
                break;
                case 'i':
                    opts |= ONIG_OPTION_IGNORECASE;
                break;
                case 'm':
                    opts |= ONIG_OPTION_MULTILINE;
                break;
                default:
                    return luaL_error( L, 
                            "failed to regexp.new() - arguments#2:%s, %s", 
                            flags, strerror( EINVAL ) );
            }
            ptr++;
        }
    }
    // check substitution-format
    if( argc > 2 )
    {
        format = luaL_checklstring( L, 3, &flen );
        if( flen && !( fmt = palloc( buf_strfmt_t ) ) ){
            return luaL_error( L, "failed to regexp.new() - arguments#2: %s", 
                               strerror( errno ) );
        }
    }
    
    if( ( rc = regexp_alloc( &re, (unsigned char*)pattern, plen, 
                        opts, enc, ONIG_SYNTAX_PERL ) ) != REGEXP_OK ){
        char errbuf[REGEXP_ERRSTR_LEN] = {0};
        return luaL_error( L, "failed to regexp.new() - %s", 
                           regexp_strerror( errbuf, rc ) );
    }
    else if( regexp_ngroups( re ) > REGEXP_LUA_MAX_GROUPS ){
        regexp_dealloc( re );
        return luaL_error( L, 
                    "failed to regexp.new() - too many capture groups: %d > %d", 
                    regexp_ngroups( re ), REGEXP_LUA_MAX_GROUPS );
    }
    else if( fmt && buf_strfmt_init( fmt, format, flen, regexp_ngroups( re) ) != BUF_OK ){
        regexp_dealloc( re );
        pdealloc( fmt );
        return luaL_error( L, "failed to regexp.new() - %s", strerror(errno) );
    }
    else if( !( wrap = lua_newuserdata( L, sizeof( RegexpLua_t ) ) ) ){
        regexp_dealloc( re );
        buf_strfmt_dispose( fmt );
        pdealloc( fmt );
        return luaL_error( L, "failed to regexp.new() - %s", lua_tostring( L, -1 ) );
    }

    wrap->re = re;
    wrap->fmt = fmt;
    wrap->global = global;
    wrap->lastIdx = 0;
    luaL_getmetatable( L, REGEXP_LUA );
    lua_setmetatable( L, -2 );
    rc = 1;
    
    return rc;
}

static int regexp_dealloc_lua( lua_State *L )
{
    RegexpLua_t *wrap = lua_touserdata( L, 1 );
    
    regexp_dealloc( wrap->re );
    if( wrap->fmt ){
        buf_strfmt_dispose( wrap->fmt );
        pdealloc( wrap->fmt );
    }
    return 0;
}

// make error
static int const_newindex( lua_State *L ){
    return luaL_error( L, "attempting to change protected module" );
}

LUALIB_API int luaopen_onig( lua_State *L )
{
    int top = lua_gettop( L );

    // create metatable
    luaL_newmetatable( L, REGEXP_LUA );
    setL_tfunction( L, "__gc", regexp_dealloc_lua );
    openL_ttable( L, "__index" );
    setL_tfunction( L, "test", regexp_test_lua );
    setL_tfunction( L, "exec", regexp_exec_lua );
    setL_tfunction( L, "replace", regexp_replace_lua );
    closeL_ttable( L );
    setL_tfunction( L, "__newindex", const_newindex );
    
    lua_settop( L, top );
    lua_newtable( L );
    // register: functions
    setL_tfunction( L, "create", regexp_alloc_lua );
    
    return 1;
}



