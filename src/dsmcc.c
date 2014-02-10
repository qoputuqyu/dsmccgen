
#include "dsmcc.h"

//fd_set master;   // master file descriptor list
//fd_set read_fds; // temp file descriptor list for select()
//int fdmax;        // maximum file descriptor number

gchar vsbuff[120];

gchar*
val_to_string( guint32 value, const value_string vs[] )
{
    int i;
    i = 0;
    while ( vs[i].strptr != NULL )
    {
        if ( value == vs[i].value )
            return vs[i].strptr;

        i++;
    }

    g_snprintf( vsbuff, 200, "Unknown value %i (%X)", value, value );
    return vsbuff;
}

void
print_dsmcc( gchar dsmccptr[], gint len )
{
    int i;
    for ( i = 0; i < len; i++ )
        g_printf( "%02X", dsmccptr[i] );
        
    g_printf( "\n" );
}
