#include<unistd.h>
#include<fcntl.h>
#include<string.h>
#include<sys/wait.h>
#include<stdlib.h>
#include<stdio.h>
//#include<zmq.h> 

char **shift_args( char *argsIn[], int count, int shift );
void run_cmd( void *sock, char **args, int argLen, long int *inLen, char *in );
void free_args( char **args, int argLen );

int main( int argc, char *argv[] ) {
    //void *sock = init_zmq( argv[0] );
    void *sock = NULL;
    
    char **newArgs = shift_args( argv, argc, 1 );
    run_cmd( sock, newArgs, argc-1, 0, NULL );
    
    free( newArgs );
    //free_args( newArgs, argc-1 );
    return 0;
}

void *init_zmq( char *spec ) {
    /*void *ctx = zmq_ctx_new();
    void *sock = zmq_socket( ctx, ZMQ_PUSH );
    int rc = zmq_connect( sock, spec ); //"tcp://*:5555" );
    if( rc != 0 ) {
        fprintf( stderr, "Cannot open zmq socket to send output" );
        exit(1);
    }
    return sock;*/
    return NULL;
}

void send_line( char *buffer, int len ) {
    printf( "Line[%.*s]\n", len, buffer );
}

void send_lines( int type, void *sock, char **outPtr, int *outPos, int *outSize, int *increase ) {
    char *out = *outPtr;
    // assume unix line endings 0x0a ( CR )
    int start = *outPos;
    int end = *outPos + *increase;
    int lineStart = 0;
    int sent = 0;
    for( int i=start;i<=end;i++ ) {
        char let = out[ i ];
        //printf("let:%c - %x\n", let, let );
        if( let == 0x0a ) {
            int lineLen = i - lineStart;
            send_line( out + lineStart, i - lineStart );
            lineStart = i+1;
            sent += lineLen + 1; // +1 to include the CR
            continue;
        }
    }
    // shift all the sent data
    int left = end - sent;
    if( left == 0 ) {
        *outPos = 0;
        *increase = 0;
        return;
    }
    if( left < sent ) {
        // small enough to just copy to the start
        memcpy( out, out + end, sent );
        *outPos = 0;
        *increase = 0;
        return;
    }
    // we can't shift everything in one go
    // so we'll just replace the whole buffer
    int newsize = left;
    if( newsize < 100 ) newsize = 100;
    
    char *newbuf = (char *) malloc( *outSize - sent );
    memcpy( newbuf, out + end, left );
    *outSize = newsize;
    *increase = 0;
    *outPos = left;
    free( out );
    *outPtr = newbuf;
}

void run_cmd( void *sock, char **args, int argLen, long int *inLen, char *in ) {
    // Run the cmd, saving the stdout and stderr of it
    int stdout_pipe[2];
    if( pipe( stdout_pipe ) == -1 ) {
        fprintf(stderr, "pipe err\n");
        return;
    }
    int stderr_pipe[2];
    if( pipe( stderr_pipe ) == -1 ) {
        fprintf(stderr, "pipe err\n");
        return;
    }
    
    pid_t pid;
    if( ( pid = fork() ) == -1 ) return;
    
    if(pid == 0) {
        dup2( stdout_pipe[1], STDOUT_FILENO ); close( stdout_pipe[0] ); close( stdout_pipe[1] );
        dup2( stderr_pipe[1], STDERR_FILENO ); close( stderr_pipe[0] ); close( stderr_pipe[1] );
        
        printf("Running command \"%s\" with %i arguments\n", args[0], argLen );
        execv( args[0], args );
        
        exit(1); // Should never be reached
    }
    
    close( stdout_pipe[1] );
    close( stderr_pipe[1] );
    
    fcntl( stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl( stderr_pipe[0], F_SETFL, O_NONBLOCK);   
    
    int outSize = 100;
    int errSize = 100;
    int outPos = 0;
    int errPos = 0;
    char *out = malloc( outSize );
    if( !out ) {
        fprintf(stderr,"Could not allocate %i out\n", outSize );
        exit(1);
    }
    char *err = malloc( errSize );
    if( !err ) {
        fprintf(stderr, "Could not allocate %i out\n", outSize );
        exit(1);
    }
        
    int resCode = 1000;
    int done = 0;
    while( done != 2 ) {
        // read a chunk of stdout
        int out_bytes = read( stdout_pipe[0], ( out + outPos ), outSize - outPos );
        if( out_bytes > 0 ) {
            //printf("out ptr: %p\n", out );
            //printf("chunk:[%.*s]\n", out_bytes, out + outPos );
            // scan for lines, sending them and shifting the buffer when found
            //if( outPos != outSize || *( out + outPos + out_bytes ) == 0x0d ) 
            send_lines( 0, sock, &out, &outPos, &outSize, &out_bytes );
                
            outPos += out_bytes;
        
            if( outPos == outSize ) {
                char *newbuf = malloc( outSize * 2 );
                memcpy( newbuf, out, outSize );
                free( out );
                out = newbuf;
                outSize *= 2;
            }
        }
        
        // read a chunk of stderr
        int err_bytes = read( stderr_pipe[0], ( err + errPos ), errSize - errPos );
        if( err_bytes > 0 ) {
            //printf("chunk:[%.*s]\n", err_bytes, err + errPos );
            if( outPos != outSize ) send_lines( 1, sock, &err, &errPos, &errSize, &err_bytes );
            
            errPos += err_bytes;
            if( errPos == errSize ) {
                char *newbuf = malloc( errSize * 2 );
                memcpy( newbuf, err, errSize );
                free( err );
                err = newbuf;
                errSize *= 2;
            }
        }
        
        if( out_bytes < 1 && err_bytes < 1 && done == 1 ) {
            done = 2;
        }
        
        if( !done ) {
            int status = 1111;
            int pid = waitpid( 0, &status, WNOHANG ); 
            
            if( pid != 0 ) {
                if( WIFEXITED( status ) ) {
                    //printf("exited\n");
                    resCode = WEXITSTATUS( status );
                    done = 1;
                }
                else if( WIFSIGNALED( status ) ) {
                    int signal = WTERMSIG( status );
                    // resCode is 1111 because we haven't changed it
                    resCode = signal;
                    done = 1;
                }
            }
        }
        
        // sleep for 1/100 of a second
        if( out_bytes < 1 && err_bytes < 1 ) usleep( 10000 );
    }
    
    /*cmd_res *res = calloc( sizeof( cmd_res ), 1 );
    res->outLen = outPos;
    res->errLen = errPos;
    res->out = out;
    res->err = err;
    res->errorLevel = resCode;*/
    //return res;
}


char **shift_args( char *argsIn[], int count, int shift ) {
    char **args;
    count -= shift;
    args = malloc( sizeof( void * ) * ( count + 1 ) );
    args[ count ] = NULL;
    
    for( int i=0;i<count;i++ ) {
        args[ i ] = argsIn[ i + shift ];
    }
    
    return args;
}

void free_args( char **args, int argLen ) {
    for( int i=1;i<argLen;i++ ) {
        char *ptr = args[ i ];
        free( ptr );
    }
    free( args );
}