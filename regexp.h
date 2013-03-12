/*
 *  Copyright (C) 2013 Masatoshi Teruya
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
/*
 *  regexp.h
 */
#ifndef ___REGEXP___
#define ___REGEXP___

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "lua.h"
#include "oniguruma.h"

#define REGEXP_AGAIN        1
#define REGEXP_OK           ONIG_NORMAL
#define REGEXP_MISS         ONIG_MISMATCH
#define REGEXP_EINVAL       EINVAL
#define REGEXP_ENOMEM       ENOMEM
#define REGEXP_IS_ERROR(e)  (e != REGEXP_OK && e != REGEXP_MISS)
#define REGEXP_IS_ERROR_ONIG(e)  (e < ONIG_MISMATCH)

void regexp_global_init( void );
void regexp_global_dispose( void );

#define REGEXP_ERRSTR_LEN   ONIG_MAX_ERROR_MESSAGE_LEN
#define regexp_strerror(b,e,...)({ \
    char *s = NULL; \
    if( REGEXP_IS_ERROR_ONIG(e) ){ \
        onig_error_code_to_str( (unsigned char*)b, e, ##__VA_ARGS__ ); \
        s = b; \
    } \
    else { \
        s = strerror(e); \
    } \
    s; \
})

typedef struct {
	OnigRegex obj;
	OnigErrorInfo err;
} Regexp_t;

// retval: REGEXP_OK or other
int regexp_init( Regexp_t *re, const unsigned char *pattern, size_t len, 
                 OnigOptionType opt, OnigEncoding enc, OnigSyntaxType *syntax );
void regexp_dispose( Regexp_t *re );

int regexp_alloc( Regexp_t **newre, const unsigned char *pattern, size_t len, 
                  OnigOptionType opt, OnigEncoding enc, OnigSyntaxType *syntax );
void regexp_dealloc( Regexp_t *re );


#define regexp_ngroups(re)  onig_number_of_captures( re->obj )

typedef struct {
    size_t head;
    size_t len;
} RegexpIdx_t;

typedef struct {
    size_t head;
    size_t len;
    size_t num;
    RegexpIdx_t *group;
} RegexpMatch_t;

// retval: match index or REGEXP_MISS or less then REGEXP_MISS
int regexp_test( Regexp_t *re, const char *str, size_t len );
// retval: REGEXP_OK or REGEXP_MISS or less then REGEXP_MISS
int regexp_exec( Regexp_t *re, const char *str, size_t len, size_t lastIdx, 
                 RegexpMatch_t *idx );

typedef int(*REGEXP_CB)( const char*, OnigRegion*, void* );
// retval: REGEXP_OK or REGEXP_MISS or return val of callback
int regexp_exec_cb( Regexp_t *re, const char *str, size_t len, REGEXP_CB cb, 
                    void *arg );

// lua binding
LUALIB_API int luaopen_oniguruma( lua_State *L );

#endif

