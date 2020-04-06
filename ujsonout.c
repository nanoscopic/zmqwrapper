// Copyright (C) 2020 David Helkowski
// MIT License
// Copied from https://github.com/nanoscopic/ujsonout
char *json_str( char *in, int len, int *outlen );

typedef struct outnode_s outnode;
struct outnode_s {
    char *str;
    int len;
    outnode *next;
};

typedef struct output_s output;
struct output_s {
    outnode *first;
    outnode *last;
    int size;
};

void output__addstr( output *self, char *str, int len ) {
    self->size += len;
    outnode *node = calloc( sizeof( outnode ), 1 );
    node->str = str;
    node->len = len;
    self->last->next = node;
    self->last = node;
}

void output__addtime( output *self, time_t now ) {
    char *str = malloc(15);
    sprintf(str,"%ld",now);
    int len = strlen(str);
    self->size += len;
    outnode *node = calloc( sizeof( outnode ), 1 );
    node->str = str;
    node->len = len;
    self->last->next = node;
    self->last = node;
}

void output__addchar( output *self, char let ) {
    char *str = calloc( 2, 1 );
    str[0] = let;
    self->size++;
    outnode *node = calloc( sizeof( outnode ), 1 );
    node->str = str;
    node->len = 1;
    if( !self->last ) {
        self->last = self->first = node;
        return;
    }
    self->last->next = node;
    self->last = node;
}

void output__end( output *self ) {
    output__addchar( self, '}' );
}

output *output__new() {
    output *ret = calloc( sizeof( output ), 1 );
    output__addchar(ret,'{');
    return ret;
}

char *output__flat( output *self ) {
    char *flat = malloc( self->size + 1 );
    flat[ self->size ] = 0x00;
    int pos = 0;
    outnode *curnode = self->first;
    while( curnode ) {
        memcpy( &flat[ pos ], curnode->str, curnode->len );
        pos += curnode->len;
        curnode = curnode->next;
    }
    return flat;
}

void output__del( output *self ) {
    outnode *curnode = self->first;
    while( curnode ) {
        outnode *next = curnode->next;
        free( curnode->str );
        free( curnode );
        curnode = next;
    }
    free( self );
}

char *output__endflatdel( output *self ) {
    output__end( self );
    char *flat = output__flat( self );
    output__del( self );
    return flat;
}

void output__add_json_str( output *self, char *key, int keylen, char *val, int vallen, char last ) {
    // "key":"val"
    char *start = malloc( keylen + 4 );
    sprintf( start, "\"%.*s\":\"", keylen, key );
    output__addstr( self, start, keylen + 4 );
    int outlen;
    char *jsonStr = json_str( val, vallen, &outlen );
    output__addstr( self, jsonStr, outlen );
    output__addchar( self, '"' );
    if( !last ) output__addchar( self, ',' );
}

void output__add_json_time( output *self, char *key, int keylen, time_t when, char last ) {
    char *start = malloc( keylen + 3 );
    sprintf( start, "\"%.*s\":", keylen, key );
    output__addstr( self, start, keylen + 3 );
    output__addtime( self, when );
    if( !last ) output__addchar( self, ',' );
}

char *json_str( char *in, int len, int *outlen ) {
    const char map[32] = {
        0,0,0,0,0,0, // 0-5
        0,0,'b','t','l', // 6-10
        0,'f','r' }; // rest will be 0; C99
    if( !len ) len = strlen( in );
    int max = len + 100;
    int extra = 100;
    char *out = ( char * ) malloc( max );
    int pos = 0;
    for( int i=0;i<len;i++ ) {
        char let = in[i];
        if( let >= 0x20 ) {
            out[pos++] = let;
            continue;
        }
        char escape = map[ let ];
        
        // we will need 2 or 4 extra, just check once instead of repeating this code
        if( extra < 5 ) {
            max += 100;
            extra += 100;
            char *replace = ( char * ) malloc( max );
            memcpy( replace, out, pos );
            free( out );
            out = replace;
        }
        
        if( escape ) {
            out[pos++] = '\\';
            out[pos++] = escape;
            extra--;
            continue;
        }
        out[pos++] = '\\';
        out[pos++] = '0';
        out[pos++] = '0';
        int low = let % 16;
        int high = ( let - low ) / 16;
        out[pos++] = ( high < 10 ) ? ( high + '0' ) : ( high - 10 + 'a' );
        out[pos++] = ( low < 10 ) ? ( low + '0' ) : ( low - 10 + 'a' );
        extra -= 4;
    }
    out[pos] = 0x00;
    *outlen = pos;
    return out;
}